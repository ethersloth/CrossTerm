// Private-key permission handling, checked against the real ssh-keygen where
// one is installed. On Windows this uses the built-in Win32-OpenSSH, which
// applies the same key-permission check as the ssh.exe CrossTerm launches.

#include "../src/backup/BackupArchive.h"
#include "../src/profiles/ProfileManager.h"
#include "../src/security/PrivateKeyPermissions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#endif

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {
void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(data) == data.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Gives a folder the kind of broad, inherited access that must not reach
// keys: BUILTIN\Users read on Windows, group/other read on Unix.
void makeBroadlyReadable(const QString &path, bool isDirectory)
{
#ifdef Q_OS_WIN
    BYTE usersSid[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(usersSid);
    CHECK(CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSid, &size));

    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    CHECK(GetNamedSecurityInfoW(native.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                nullptr, nullptr, &oldDacl, nullptr, &descriptor) == ERROR_SUCCESS);
    EXPLICIT_ACCESS_W entry{};
    entry.grfAccessPermissions = FILE_GENERIC_READ;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = isDirectory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(usersSid);
    PACL newDacl = nullptr;
    CHECK(SetEntriesInAclW(1, &entry, oldDacl, &newDacl) == ERROR_SUCCESS);
    CHECK(SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                nullptr, nullptr, newDacl, nullptr) == ERROR_SUCCESS);
    LocalFree(newDacl);
    LocalFree(descriptor);
#else
    const auto mode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                      | QFileDevice::ReadOther
                      | (isDirectory ? QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther
                                     : QFileDevice::Permissions());
    CHECK(QFile::setPermissions(path, mode));
#endif
}

QString findSshKeygen()
{
#ifdef Q_OS_WIN
    // Prefer Windows' own OpenSSH over e.g. Git's MSYS build on PATH.
    const QString builtIn = qEnvironmentVariable("SystemRoot") + QStringLiteral("\\System32\\OpenSSH\\ssh-keygen.exe");
    if (QFileInfo::exists(builtIn))
        return builtIn;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
}

struct Run {
    int exitCode = -1;
    QString output;
};

Run run(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    CHECK(process.waitForStarted(10000));
    CHECK(process.waitForFinished(60000));
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            QString::fromLocal8Bit(process.readAll())};
}

// ssh-keygen -y loads the private key with OpenSSH's permission check.
Run loadKey(const QString &keygen, const QString &keyPath)
{
    return run(keygen, {QStringLiteral("-y"), QStringLiteral("-f"), QDir::toNativeSeparators(keyPath)});
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CrossTermTest"));
    QCoreApplication::setApplicationName(QStringLiteral("KeyPermissionsTest"));

    QTemporaryDir temp;
    CHECK(temp.isValid());
    // Spaces on purpose: ACL and command-line handling must cope.
    const QString root = temp.filePath(QStringLiteral("Cross Term Test"));
    const QString broad = root + QStringLiteral("/broad parent");
    CHECK(QDir().mkpath(broad));
    makeBroadlyReadable(broad, true);

    // A file created under a broadly readable folder inherits that access
    // (the negative control), and securing it removes the inherited access.
    const QString loose = broad + QStringLiteral("/inherits access");
    writeFile(loose, "not a real key\n");
#ifndef Q_OS_WIN
    makeBroadlyReadable(loose, false);
#endif
    QString details;
    CHECK(!PrivateKeyPermissions::isSecure(loose, &details));
    std::printf("negative control (file in broad folder): %s\n", qPrintable(details));
    CHECK(PrivateKeyPermissions::secureFile(loose, &details));
    CHECK(PrivateKeyPermissions::isSecure(loose, &details));
    CHECK(readFile(loose) == "not a real key\n"); // still readable by us
    CHECK(PrivateKeyPermissions::secureFile(loose)); // idempotent
    CHECK(PrivateKeyPermissions::isSecure(loose));

    // Real keys, if ssh-keygen is available.
    const QString keygen = findSshKeygen();
    std::printf("ssh-keygen: %s\n", keygen.isEmpty() ? "not found; skipping OpenSSH checks" : qPrintable(keygen));
    const QString generated = root + QStringLiteral("/generated");
    CHECK(QDir().mkpath(generated));

    struct KeySpec {
        QString fileName;
        QString type;
    };
    const QList<KeySpec> specs = {
        {QStringLiteral("id_ed25519"), QStringLiteral("ed25519")},
        {QStringLiteral("id_rsa"), QStringLiteral("rsa")},
        {QStringLiteral("key.pem"), QStringLiteral("ecdsa")},
        {QStringLiteral("linode_gwhitlock"), QStringLiteral("ed25519")},
    };

    BackupArchive::Contents contents;
    for (const auto &spec : specs) {
        BackupArchive::KeyFile key;
        key.fileName = spec.fileName;
        key.originalPath = QStringLiteral("C:/Users/someone/.ssh/") + spec.fileName;
        if (!keygen.isEmpty()) {
            const QString out = generated + QLatin1Char('/') + spec.fileName;
            const Run made = run(keygen, {QStringLiteral("-q"), QStringLiteral("-t"), spec.type,
                                          QStringLiteral("-N"), QString(), QStringLiteral("-C"),
                                          QStringLiteral("crossterm-test"), QStringLiteral("-f"),
                                          QDir::toNativeSeparators(out)});
            if (made.exitCode != 0)
                std::fprintf(stderr, "ssh-keygen output: %s\n", qPrintable(made.output));
            CHECK(made.exitCode == 0);
            key.data = readFile(out);
            key.publicData = readFile(out + QStringLiteral(".pub"));
        } else {
            key.data = "-----BEGIN OPENSSH PRIVATE KEY-----\nplaceholder\n";
        }
        contents.keyFiles.append(key);

        ConnectionProfile profile(QStringLiteral("Session ") + spec.fileName, ConnectionProfile::ConnectionType::SSH);
        profile.setSshHost(QStringLiteral("example.invalid"));
        profile.setSshPrivateKey(key.originalPath);
        contents.profiles.append(profile);
    }

    if (!keygen.isEmpty()) {
        // Prove OpenSSH enforces the check here: the same key with broad
        // access must be refused, or passing below would mean nothing.
        const QString exposed = broad + QStringLiteral("/exposed key");
        writeFile(exposed, contents.keyFiles.first().data);
#ifndef Q_OS_WIN
        makeBroadlyReadable(exposed, false);
#endif
        const Run refused = loadKey(keygen, exposed);
        std::printf("negative control (ssh-keygen on exposed key): exit %d\n%s\n", refused.exitCode,
                    qPrintable(refused.output.trimmed()));
        CHECK(refused.exitCode != 0);
        CHECK(refused.output.contains(QStringLiteral("UNPROTECTED PRIVATE KEY FILE"))
              || refused.output.contains(QStringLiteral("bad permissions"), Qt::CaseInsensitive));
    }

    // Import through the same path as File > Import Backup, into a key folder
    // that starts out inheriting broad access.
    const QString keyDir = broad + QStringLiteral("/keys folder");
    CHECK(QDir().mkpath(keyDir));
    ProfileManager profiles;
    profiles.setProfilesPath(root + QStringLiteral("/profiles.json"));
    QSettings settings(root + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    const auto result = BackupArchive::apply(contents, profiles, settings, BackupArchive::ImportMode::Merge,
                                             keyDir, root + QStringLiteral("/known_hosts"));
    if (!result.error.isEmpty())
        std::fprintf(stderr, "import error: %s\n", qPrintable(result.error));
    CHECK(result.error.isEmpty());
    CHECK(result.keysRestored == specs.size());
    CHECK(PrivateKeyPermissions::isSecure(keyDir, &details));

    const auto checkImportedKeys = [&](const char *when) {
        for (const auto &profile : profiles.allProfiles()) {
            const QString keyPath = profile.sshPrivateKey();
            CHECK(keyPath.startsWith(QDir::fromNativeSeparators(keyDir)));
            if (!PrivateKeyPermissions::isSecure(keyPath, &details))
                std::fprintf(stderr, "%s: %s\n", qPrintable(keyPath), qPrintable(details));
            CHECK(PrivateKeyPermissions::isSecure(keyPath));
            if (!keygen.isEmpty()) {
                const Run loaded = loadKey(keygen, keyPath);
                const bool publicKey = loaded.output.startsWith(QStringLiteral("ssh-"))
                                       || loaded.output.startsWith(QStringLiteral("ecdsa-"));
                if (loaded.exitCode != 0 || !publicKey)
                    std::fprintf(stderr, "ssh-keygen -y %s:\n%s\n", qPrintable(keyPath), qPrintable(loaded.output));
                CHECK(loaded.exitCode == 0);
                CHECK(publicKey);
            }
        }
        std::printf("%s: %lld imported keys secure%s\n", when, qlonglong(profiles.allProfiles().size()),
                    keygen.isEmpty() ? "" : " and accepted by ssh-keygen");
    };
    checkImportedKeys("after import");

    // "Restart": reload from disk and check again; securing again is a no-op.
    ProfileManager reloaded;
    reloaded.setProfilesPath(profiles.profilesPath());
    CHECK(reloaded.loadProfiles());
    for (const auto &profile : reloaded.allProfiles())
        CHECK(PrivateKeyPermissions::secureFile(profile.sshPrivateKey()));
    checkImportedKeys("after reload");

    // Only CrossTerm's own key folder counts as managed.
    const QString managed = PrivateKeyPermissions::keyDirectory() + QStringLiteral("/id_ed25519");
    CHECK(PrivateKeyPermissions::isInKeyDirectory(managed));
    CHECK(!PrivateKeyPermissions::isInKeyDirectory(QDir::homePath() + QStringLiteral("/.ssh/id_ed25519")));
    CHECK(!PrivateKeyPermissions::isInKeyDirectory(PrivateKeyPermissions::keyDirectory() + QStringLiteral("-other/k")));
#ifdef Q_OS_WIN
    CHECK(PrivateKeyPermissions::isInKeyDirectory(managed.toUpper()));
#endif

    std::puts("KeyPermissionsTest: all checks passed");
    return 0;
}
