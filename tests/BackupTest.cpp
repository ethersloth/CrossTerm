#include "../src/backup/BackupArchive.h"
#include "../src/backup/BackupCrypto.h"
#include "../src/profiles/ProfileManager.h"
#include "../src/profiles/SecretStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QHash>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

// Unlike assert(), stays active in Release builds.
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {
// Cheap parameters keep the tests fast; production uses the defaults.
constexpr BackupCrypto::KdfParams kFastParams{64, 1, 1};

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(data) == data.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void testCrypto()
{
    using BackupCrypto::Error;
    const QByteArray plain("profiles and keys \x00\x01\x02 go here", 34);

    const QByteArray sealed = BackupCrypto::encrypt(plain, QStringLiteral("correct horse"), kFastParams);
    CHECK(BackupCrypto::looksLikeBackup(sealed));
    CHECK(!sealed.contains("profiles and keys"));

    Error error = Error::None;
    const auto opened = BackupCrypto::decrypt(sealed, QStringLiteral("correct horse"), &error);
    CHECK(opened.has_value() && *opened == plain && error == Error::None);

    CHECK(!BackupCrypto::decrypt(sealed, QStringLiteral("wrong horse"), &error));
    CHECK(error == Error::WrongPasswordOrDamaged);

    // Two encryptions of the same data differ (fresh salt and nonce).
    CHECK(BackupCrypto::encrypt(plain, QStringLiteral("correct horse"), kFastParams) != sealed);

    // Tampering anywhere (ciphertext, salt, nonce, MAC) is detected.
    for (const int offset : {24, 40, 64, 80, int(sealed.size()) - 1}) {
        QByteArray tampered = sealed;
        tampered[offset] = char(tampered[offset] ^ 0x01);
        CHECK(!BackupCrypto::decrypt(tampered, QStringLiteral("correct horse"), &error));
        CHECK(error == Error::WrongPasswordOrDamaged);
    }

    QByteArray truncated = sealed.left(sealed.size() - 5);
    CHECK(!BackupCrypto::decrypt(truncated, QStringLiteral("correct horse"), &error));
    CHECK(error == Error::WrongPasswordOrDamaged);

    CHECK(!BackupCrypto::decrypt(QByteArray("not a backup at all"), QStringLiteral("x"), &error));
    CHECK(error == Error::NotABackup);

    // A header asking for absurd Argon2 memory is refused before allocating.
    QByteArray greedy = sealed;
    greedy[12] = char(0xff);
    greedy[13] = char(0xff);
    greedy[14] = char(0xff);
    greedy[15] = char(0x7f);
    CHECK(!BackupCrypto::decrypt(greedy, QStringLiteral("correct horse"), &error));
    CHECK(error == Error::BadParameters);

    QByteArray future = sealed;
    future[8] = char(2);
    CHECK(!BackupCrypto::decrypt(future, QStringLiteral("correct horse"), &error));
    CHECK(error == Error::UnsupportedVersion);

    // Composed vs precomposed "é" derive the same key.
    const QByteArray nfc = BackupCrypto::encrypt(plain, QString::fromUtf8("caf\xc3\xa9"), kFastParams);
    CHECK(BackupCrypto::decrypt(nfc, QString::fromUtf8("cafe\xcc\x81")).has_value());

    // Production parameters work end to end.
    const QByteArray strong = BackupCrypto::encrypt(plain, QStringLiteral("pw"));
    CHECK(BackupCrypto::decrypt(strong, QStringLiteral("pw")) == plain);
}

void testArchiveRoundTrip()
{
    QTemporaryDir source;
    QTemporaryDir target;
    CHECK(source.isValid() && target.isValid());

    // --- Source machine ---
    const QString keyPath = source.filePath(QStringLiteral("id_ed25519"));
    writeFile(keyPath, "-----BEGIN OPENSSH PRIVATE KEY-----\nsecret\n");
    writeFile(keyPath + QStringLiteral(".pub"), "ssh-ed25519 AAAA test");

    ProfileManager sourceProfiles;
    sourceProfiles.setProfilesPath(source.filePath(QStringLiteral("profiles.json")));
    ConnectionProfile server(QStringLiteral("Server"), ConnectionProfile::ConnectionType::SSH);
    server.setSshHost(QStringLiteral("10.0.0.5"));
    server.setSshPassword(QStringLiteral("hunter2"));
    server.setSshPrivateKey(keyPath);
    server.setFolder(QStringLiteral("Work/Prod"));
    server.setSavedCommands({{QStringLiteral("Disk"), QStringLiteral("df -h")}});
    server.setProperty(QStringLiteral("download_directory"), QStringLiteral("/no/such/dir/on/target"));
    sourceProfiles.addProfile(server);
    ConnectionProfile brokenKey(QStringLiteral("Broken"), ConnectionProfile::ConnectionType::SSH);
    brokenKey.setSshPrivateKey(source.filePath(QStringLiteral("missing_key")));
    sourceProfiles.addProfile(brokenKey);
    sourceProfiles.addFolder(QStringLiteral("Work/Prod"));
    CHECK(sourceProfiles.saveProfiles());

    QSettings sourceSettings(source.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    sourceSettings.setValue(QStringLiteral("global/theme"), QStringLiteral("light"));
    sourceSettings.setValue(QStringLiteral("global/fontSize"), 14);
    sourceSettings.setValue(QStringLiteral("global/downloadDirectory"), QStringLiteral("/no/such/dir"));
    sourceSettings.setValue(QStringLiteral("session/recentNames"), QStringList{QStringLiteral("Server")});
    sourceSettings.sync();

    const QString sourceKnownHosts = source.filePath(QStringLiteral("known_hosts"));
    writeFile(sourceKnownHosts, "nas ssh-ed25519 AAAA1\r\n# comment\nrouter ssh-rsa BBBB2\n");

    const BackupArchive::Contents collected = BackupArchive::collect(sourceProfiles, sourceSettings, sourceKnownHosts);
    CHECK(BackupArchive::knownHostCount(collected.knownHosts) == 2);
    CHECK(collected.profiles.size() == 2);
    CHECK(collected.keyFiles.size() == 1);
    CHECK(collected.missingKeys == QStringList{source.filePath(QStringLiteral("missing_key"))});

    const QByteArray file = BackupCrypto::encrypt(BackupArchive::serialize(collected),
                                                  QStringLiteral("backup-pass"), kFastParams);
    CHECK(!file.contains("hunter2"));
    CHECK(!file.contains("secret"));

    // --- Target machine: merge ---
    const auto plain = BackupCrypto::decrypt(file, QStringLiteral("backup-pass"));
    CHECK(plain.has_value());
    const auto contents = BackupArchive::deserialize(*plain);
    CHECK(contents.has_value());
    CHECK(contents->keyFiles.size() == 1 && contents->keyFiles.first().publicData == "ssh-ed25519 AAAA test");

    ProfileManager targetProfiles;
    targetProfiles.setProfilesPath(target.filePath(QStringLiteral("profiles.json")));
    targetProfiles.addProfile(ConnectionProfile(QStringLiteral("LocalOnly")));
    CHECK(targetProfiles.saveProfiles());
    QSettings targetSettings(target.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    targetSettings.setValue(QStringLiteral("session/recentNames"), QStringList{QStringLiteral("LocalOnly")});

    const QString keyDir = target.filePath(QStringLiteral("keys"));
    const QString targetKnownHosts = target.filePath(QStringLiteral("ssh/known_hosts"));
    CHECK(QDir().mkpath(target.filePath(QStringLiteral("ssh"))));
    writeFile(targetKnownHosts, "router ssh-rsa BBBB2"); // no trailing newline
    const auto merged = BackupArchive::apply(*contents, targetProfiles, targetSettings,
                                             BackupArchive::ImportMode::Merge, keyDir, targetKnownHosts);
    CHECK(merged.error.isEmpty());
    CHECK(merged.added == 2 && merged.updated == 0 && merged.removed == 0);
    CHECK(merged.keysRestored == 1);
    CHECK(merged.pathsReset == 2); // profile download dir + global download dir
    CHECK(QFile::exists(merged.safetyCopyPath));
    CHECK(merged.knownHostsAdded == 1);
    CHECK(readFile(targetKnownHosts) == "router ssh-rsa BBBB2\nnas ssh-ed25519 AAAA1\n");

    CHECK(targetProfiles.hasProfile(QStringLiteral("LocalOnly")));
    const ConnectionProfile restored = targetProfiles.profile(QStringLiteral("Server"));
    CHECK(restored.sshPassword() == QStringLiteral("hunter2"));
    CHECK(restored.folder() == QStringLiteral("Work/Prod"));
    CHECK(restored.savedCommands().size() == 1);
    CHECK(!restored.hasProperty(QStringLiteral("download_directory")));
    CHECK(targetProfiles.hasFolder(QStringLiteral("Work")) && targetProfiles.hasFolder(QStringLiteral("Work/Prod")));

    const QString restoredKey = restored.sshPrivateKey();
    CHECK(restoredKey.startsWith(QDir::fromNativeSeparators(keyDir)));
    CHECK(readFile(restoredKey) == "-----BEGIN OPENSSH PRIVATE KEY-----\nsecret\n");
    CHECK(readFile(restoredKey + QStringLiteral(".pub")) == "ssh-ed25519 AAAA test");
#ifndef Q_OS_WIN
    CHECK(QFile::permissions(restoredKey) == (QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              | QFileDevice::ReadUser | QFileDevice::WriteUser));
#endif

    CHECK(targetSettings.value(QStringLiteral("global/theme")).toString() == QStringLiteral("light"));
    CHECK(targetSettings.value(QStringLiteral("global/fontSize")).toInt() == 14);
    CHECK(!targetSettings.contains(QStringLiteral("global/downloadDirectory")));
    CHECK(targetSettings.value(QStringLiteral("session/recentNames")).toStringList()
          == (QStringList{QStringLiteral("Server"), QStringLiteral("LocalOnly")}));

    // Saved to disk, not just in memory.
    ProfileManager reloaded;
    reloaded.setProfilesPath(targetProfiles.profilesPath());
    CHECK(reloaded.loadProfiles());
    CHECK(reloaded.profileNames().size() == 3);

    // Importing the same backup again reuses the identical key file.
    const auto again = BackupArchive::apply(*contents, targetProfiles, targetSettings,
                                            BackupArchive::ImportMode::Merge, keyDir, targetKnownHosts);
    CHECK(again.error.isEmpty() && again.updated == 2 && again.added == 0);
    CHECK(again.knownHostsAdded == 0);
    CHECK(targetProfiles.profile(QStringLiteral("Server")).sshPrivateKey() == restoredKey);
    CHECK(QDir(keyDir).entryList(QDir::Files).size() == 2); // key + .pub

    // --- Replace drops sessions that are not in the backup ---
    const auto replaced = BackupArchive::apply(*contents, targetProfiles, targetSettings,
                                               BackupArchive::ImportMode::Replace, keyDir, targetKnownHosts);
    CHECK(replaced.error.isEmpty() && replaced.removed == 1);
    CHECK(!targetProfiles.hasProfile(QStringLiteral("LocalOnly")));
    CHECK(targetSettings.value(QStringLiteral("session/recentNames")).toStringList()
          == QStringList{QStringLiteral("Server")});

    CHECK(!BackupArchive::deserialize(QByteArray("garbage")).has_value());
}

class FakeStore final : public SecretStore
{
public:
    bool available = true;
    bool failWrites = false;
    int writes = 0;
    QHash<QString, QString> entries;

    bool isAvailable() override { return available; }
    std::optional<QString> read(const QString &account) override
    {
        if (!available || !entries.contains(account))
            return std::nullopt;
        return entries.value(account);
    }
    bool write(const QString &account, const QString &secret) override
    {
        if (!available || failWrites)
            return false;
        ++writes;
        entries.insert(account, secret);
        return true;
    }
    bool remove(const QString &account) override
    {
        if (!available)
            return false;
        entries.remove(account);
        return true;
    }
    QString name() const override { return QStringLiteral("fake store"); }
};

void testSecretStore()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("profiles.json"));

    // An existing file from an older version, with a plain-text password.
    writeFile(path, R"({"folders":[],"profiles":[
        {"name":"Nas","type":1,"properties":{"ssh_host":"nas","ssh_password":"pw-1"}},
        {"name":"KeyOnly","type":1,"properties":{"ssh_host":"k"}}]})");

    FakeStore store;
    ProfileManager profiles;
    profiles.setProfilesPath(path);
    profiles.setSecretStore(&store);
    CHECK(profiles.loadProfiles());

    // Migrated on load: out of the file, into the store, still usable.
    CHECK(store.entries == (QHash<QString, QString>{{QStringLiteral("Nas"), QStringLiteral("pw-1")}}));
    CHECK(!readFile(path).contains("pw-1"));
    CHECK(readFile(path).contains("\"ssh_password_store\": \"keychain\""));
    CHECK(profiles.profile(QStringLiteral("Nas")).sshPassword() == QStringLiteral("pw-1"));
    CHECK(profiles.secretStorageWarning().isEmpty());
#ifndef Q_OS_WIN
    CHECK(QFile::permissions(path) == (QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ReadUser | QFileDevice::WriteUser));
#endif

    // Unchanged passwords are not rewritten on every save.
    const int writesBefore = store.writes;
    CHECK(profiles.saveProfiles());
    CHECK(store.writes == writesBefore);

    // A fresh load reads it back from the store.
    ProfileManager reloaded;
    reloaded.setProfilesPath(path);
    reloaded.setSecretStore(&store);
    CHECK(reloaded.loadProfiles());
    CHECK(reloaded.profile(QStringLiteral("Nas")).sshPassword() == QStringLiteral("pw-1"));
    CHECK(!reloaded.profile(QStringLiteral("Nas")).hasProperty(QStringLiteral("ssh_password_store")));

    // Rename: new entry written, old one removed.
    ConnectionProfile renamed = reloaded.profile(QStringLiteral("Nas"));
    reloaded.removeProfile(QStringLiteral("Nas"));
    renamed.setName(QStringLiteral("Nas2"));
    reloaded.updateProfile(renamed);
    CHECK(reloaded.saveProfiles());
    CHECK(store.entries == (QHash<QString, QString>{{QStringLiteral("Nas2"), QStringLiteral("pw-1")}}));

    // Changed password is rewritten; cleared password removes the entry.
    ConnectionProfile changed = reloaded.profile(QStringLiteral("Nas2"));
    changed.setSshPassword(QStringLiteral("pw-2"));
    reloaded.updateProfile(changed);
    CHECK(reloaded.saveProfiles());
    CHECK(store.entries.value(QStringLiteral("Nas2")) == QStringLiteral("pw-2"));
    changed.setSshPassword(QString());
    reloaded.updateProfile(changed);
    CHECK(reloaded.saveProfiles());
    CHECK(store.entries.isEmpty());
    CHECK(!readFile(path).contains("ssh_password_store"));

    // Keychain refuses writes: password stays in the file, with a warning.
    changed.setSshPassword(QStringLiteral("pw-3"));
    reloaded.updateProfile(changed);
    store.failWrites = true;
    CHECK(reloaded.saveProfiles());
    CHECK(readFile(path).contains("pw-3"));
    CHECK(!reloaded.secretStorageWarning().isEmpty());
    store.failWrites = false;
    CHECK(reloaded.saveProfiles());
    CHECK(!readFile(path).contains("pw-3"));
    CHECK(reloaded.secretStorageWarning().isEmpty());

    // Keychain unreachable at startup: marker kept, stored password never deleted.
    store.available = false;
    ProfileManager offline;
    offline.setProfilesPath(path);
    offline.setSecretStore(&store);
    CHECK(offline.loadProfiles());
    CHECK(offline.profile(QStringLiteral("Nas2")).sshPassword().isEmpty());
    CHECK(!offline.secretStorageWarning().isEmpty());
    CHECK(offline.saveProfiles());
    store.available = true;
    CHECK(store.entries.value(QStringLiteral("Nas2")) == QStringLiteral("pw-3"));
    CHECK(readFile(path).contains("ssh_password_store"));
    ProfileManager backOnline;
    backOnline.setProfilesPath(path);
    backOnline.setSecretStore(&store);
    CHECK(backOnline.loadProfiles());
    CHECK(backOnline.profile(QStringLiteral("Nas2")).sshPassword() == QStringLiteral("pw-3"));

    // No store at all (e.g. tests, unsupported platform): plain file as before.
    ProfileManager plain;
    plain.setProfilesPath(dir.filePath(QStringLiteral("plain.json")));
    ConnectionProfile p(QStringLiteral("P"), ConnectionProfile::ConnectionType::SSH);
    p.setSshPassword(QStringLiteral("pw-plain"));
    plain.addProfile(p);
    CHECK(plain.saveProfiles());
    CHECK(readFile(plain.profilesPath()).contains("pw-plain"));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    testCrypto();
    testArchiveRoundTrip();
    testSecretStore();
    std::puts("BackupTest: all checks passed");
    return 0;
}
