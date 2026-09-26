#include "BackupArchive.h"
#include "../profiles/ProfileManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSysInfo>

namespace {
constexpr int kPayloadVersion = 1;
constexpr qint64 kMaxKeyFileSize = 1024 * 1024;
constexpr qint64 kMaxKnownHostsSize = 8 * 1024 * 1024;
constexpr int kMaxRecentSessions = 8;

const QString kFormatName = QStringLiteral("crossterm-backup");
const QString kKeyProperty = QStringLiteral("ssh_private_key");
const QString kDownloadDirProperty = QStringLiteral("download_directory");
const QString kLogPathProperty = QStringLiteral("session_log_path");
const QString kRecentSetting = QStringLiteral("session/recentNames");
// Global settings that hold a directory on the machine that made the backup.
const QStringList kDirectorySettings = {
    QStringLiteral("global/downloadDirectory"),
    QStringLiteral("global/logDirectory"),
};

bool directoryExists(const QString &path)
{
    return QFileInfo(BackupArchive::expandHome(path)).isDir();
}

QString uniqueKeyPath(const QDir &dir, const QString &fileName, const QByteArray &data)
{
    QString base = fileName;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
        base.prepend(QStringLiteral("key"));

    for (int attempt = 1;; ++attempt) {
        const QString name = attempt == 1 ? base : QStringLiteral("%1-%2").arg(base).arg(attempt);
        const QString path = dir.filePath(name);
        QFile existing(path);
        if (!existing.exists())
            return path;
        // Reuse an identical key restored by an earlier import.
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data)
            return path;
    }
}

bool writePrivateFile(const QString &path, const QByteArray &data, QFileDevice::Permissions permissions)
{
    QFile file(path);
    // Created with the final permissions so the key is never world-readable,
    // even briefly. Windows ignores these bits; the per-user data folder's
    // ACL applies there.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate, permissions))
        return false;
    const bool ok = file.write(data) == data.size();
    file.setPermissions(permissions);
    return ok;
}

QList<QByteArray> knownHostLines(const QByteArray &data)
{
    QList<QByteArray> lines;
    for (QByteArray line : data.split('\n')) {
        line = line.trimmed(); // also drops Windows "\r"
        if (!line.isEmpty() && !line.startsWith('#'))
            lines.append(line);
    }
    return lines;
}

// Appends backup lines not already present. Returns the number added, or -1.
int mergeKnownHosts(const QString &path, const QByteArray &incoming)
{
    const QList<QByteArray> incomingLines = knownHostLines(incoming);
    if (incomingLines.isEmpty())
        return 0;

    const QFileInfo info(path);
    if (!info.dir().exists()) {
        if (!QDir().mkpath(info.absolutePath()))
            return -1;
        QFile::setPermissions(info.absolutePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    QByteArray existing;
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            return -1;
        existing = file.readAll();
        file.close();
    }
    const QList<QByteArray> existingList = knownHostLines(existing);
    QSet<QByteArray> present(existingList.cbegin(), existingList.cend());

    QByteArray toAppend;
    int added = 0;
    for (const auto &line : incomingLines) {
        if (present.contains(line))
            continue;
        present.insert(line);
        toAppend += line + '\n';
        ++added;
    }
    if (added == 0)
        return 0;
    if (!existing.isEmpty() && !existing.endsWith('\n'))
        toAppend.prepend('\n');

    const bool created = !file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return -1;
    if (file.write(toAppend) != toAppend.size())
        return -1;
    if (created)
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return added;
}

QStringList mergedRecents(const QStringList &first, const QStringList &second)
{
    QStringList merged;
    for (const auto &list : {first, second}) {
        for (const auto &name : list) {
            if (!merged.contains(name) && merged.size() < kMaxRecentSessions)
                merged.append(name);
        }
    }
    return merged;
}
}

namespace BackupArchive {

QString defaultKnownHostsPath()
{
    return QDir::homePath() + QStringLiteral("/.ssh/known_hosts");
}

int knownHostCount(const QByteArray &knownHosts)
{
    return int(knownHostLines(knownHosts).size());
}

QString expandHome(const QString &path)
{
    if (path == QStringLiteral("~"))
        return QDir::homePath();
    if (path.startsWith(QStringLiteral("~/")) || path.startsWith(QStringLiteral("~\\")))
        return QDir::homePath() + path.mid(1);
    return path;
}

Contents collect(const ProfileManager &profiles, const QSettings &settings, const QString &knownHostsPath)
{
    Contents contents;
    contents.createdAt = QDateTime::currentDateTimeUtc();
    contents.appVersion = QCoreApplication::applicationVersion();
    contents.sourceHost = QSysInfo::machineHostName();
    contents.sourcePlatform = QSysInfo::prettyProductName();
    contents.profiles = profiles.allProfiles();
    contents.folders = profiles.folders();

    for (const auto &key : settings.allKeys())
        contents.settings.insert(key, settings.value(key));

    QSet<QString> seen;
    for (const auto &profile : std::as_const(contents.profiles)) {
        const QString keyPath = profile.property(kKeyProperty).trimmed();
        if (keyPath.isEmpty() || seen.contains(keyPath))
            continue;
        seen.insert(keyPath);

        const QString resolved = expandHome(keyPath);
        QFile file(resolved);
        if (file.size() > kMaxKeyFileSize || !file.open(QIODevice::ReadOnly)) {
            contents.missingKeys.append(keyPath);
            continue;
        }

        KeyFile keyFile;
        keyFile.originalPath = keyPath;
        keyFile.fileName = QFileInfo(resolved).fileName();
        keyFile.data = file.readAll();
        QFile publicFile(resolved + QStringLiteral(".pub"));
        if (publicFile.size() <= kMaxKeyFileSize && publicFile.open(QIODevice::ReadOnly))
            keyFile.publicData = publicFile.readAll();
        contents.keyFiles.append(keyFile);
    }

    QFile knownHosts(knownHostsPath);
    if (knownHosts.size() <= kMaxKnownHostsSize && knownHosts.open(QIODevice::ReadOnly))
        contents.knownHosts = knownHosts.readAll();
    return contents;
}

QByteArray serialize(const Contents &contents)
{
    QJsonArray profilesArray;
    for (const auto &profile : contents.profiles)
        profilesArray.append(profile.toJson());

    QJsonObject settingsObject;
    for (auto it = contents.settings.cbegin(); it != contents.settings.cend(); ++it)
        settingsObject.insert(it.key(), QJsonValue::fromVariant(it.value()));

    QJsonArray keysArray;
    for (const auto &key : contents.keyFiles) {
        QJsonObject keyObject;
        keyObject.insert(QStringLiteral("originalPath"), key.originalPath);
        keyObject.insert(QStringLiteral("fileName"), key.fileName);
        keyObject.insert(QStringLiteral("data"), QString::fromLatin1(key.data.toBase64()));
        if (!key.publicData.isEmpty())
            keyObject.insert(QStringLiteral("publicData"), QString::fromLatin1(key.publicData.toBase64()));
        keysArray.append(keyObject);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormatName);
    root.insert(QStringLiteral("version"), kPayloadVersion);
    root.insert(QStringLiteral("createdAt"), contents.createdAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("appVersion"), contents.appVersion);
    root.insert(QStringLiteral("sourceHost"), contents.sourceHost);
    root.insert(QStringLiteral("sourcePlatform"), contents.sourcePlatform);
    root.insert(QStringLiteral("profiles"), profilesArray);
    root.insert(QStringLiteral("folders"), QJsonArray::fromStringList(contents.folders));
    root.insert(QStringLiteral("settings"), settingsObject);
    root.insert(QStringLiteral("keyFiles"), keysArray);
    root.insert(QStringLiteral("missingKeys"), QJsonArray::fromStringList(contents.missingKeys));
    if (!contents.knownHosts.isEmpty())
        root.insert(QStringLiteral("knownHosts"), QString::fromLatin1(contents.knownHosts.toBase64()));
    return qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9);
}

std::optional<Contents> deserialize(const QByteArray &data, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<Contents> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    const QByteArray json = qUncompress(data);
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonObject root = doc.object();
    if (json.isEmpty() || !doc.isObject() || root.value(QStringLiteral("format")).toString() != kFormatName)
        return fail(QStringLiteral("The backup contents could not be read."));
    if (root.value(QStringLiteral("version")).toInt() > kPayloadVersion)
        return fail(QStringLiteral("This backup was made by a newer version of CrossTerm. Update CrossTerm and try again."));

    Contents contents;
    contents.createdAt = QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    contents.appVersion = root.value(QStringLiteral("appVersion")).toString();
    contents.sourceHost = root.value(QStringLiteral("sourceHost")).toString();
    contents.sourcePlatform = root.value(QStringLiteral("sourcePlatform")).toString();

    for (const auto &value : root.value(QStringLiteral("profiles")).toArray()) {
        const ConnectionProfile profile = ConnectionProfile::fromJson(value.toObject());
        if (!profile.name().isEmpty())
            contents.profiles.append(profile);
    }
    for (const auto &value : root.value(QStringLiteral("folders")).toArray())
        contents.folders.append(value.toString());

    const QJsonObject settingsObject = root.value(QStringLiteral("settings")).toObject();
    for (auto it = settingsObject.constBegin(); it != settingsObject.constEnd(); ++it)
        contents.settings.insert(it.key(), it.value().toVariant());

    for (const auto &value : root.value(QStringLiteral("keyFiles")).toArray()) {
        const QJsonObject keyObject = value.toObject();
        KeyFile key;
        key.originalPath = keyObject.value(QStringLiteral("originalPath")).toString();
        key.fileName = keyObject.value(QStringLiteral("fileName")).toString();
        key.data = QByteArray::fromBase64(keyObject.value(QStringLiteral("data")).toString().toLatin1());
        key.publicData = QByteArray::fromBase64(keyObject.value(QStringLiteral("publicData")).toString().toLatin1());
        if (!key.originalPath.isEmpty() && !key.data.isEmpty())
            contents.keyFiles.append(key);
    }
    for (const auto &value : root.value(QStringLiteral("missingKeys")).toArray())
        contents.missingKeys.append(value.toString());
    contents.knownHosts = QByteArray::fromBase64(root.value(QStringLiteral("knownHosts")).toString().toLatin1());
    return contents;
}

ImportResult apply(const Contents &contents, ProfileManager &profiles, QSettings &settings,
                   ImportMode mode, const QString &keyDirectory, const QString &knownHostsPath)
{
    ImportResult result;

    const QString profilesPath = profiles.profilesPath();
    if (QFile::exists(profilesPath)) {
        const QString base = QStringLiteral("%1.before-import-%2")
                                 .arg(profilesPath,
                                      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        result.safetyCopyPath = base;
        for (int n = 2; QFile::exists(result.safetyCopyPath); ++n)
            result.safetyCopyPath = QStringLiteral("%1-%2").arg(base).arg(n);
        if (!QFile::copy(profilesPath, result.safetyCopyPath)) {
            result.error = QStringLiteral("Could not save a safety copy of the current sessions to %1. Nothing was changed.")
                               .arg(result.safetyCopyPath);
            result.safetyCopyPath.clear();
            return result;
        }
    }

    // Keys first, so profiles can be pointed at their new locations.
    QHash<QString, QString> restoredKeyPaths;
    if (!contents.keyFiles.isEmpty()) {
        QDir dir(keyDirectory);
        if (!dir.mkpath(QStringLiteral("."))) {
            result.error = QStringLiteral("Could not create the key folder %1. Nothing was changed.").arg(keyDirectory);
            return result;
        }
        QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        for (const auto &key : contents.keyFiles) {
            const QString path = uniqueKeyPath(dir, key.fileName, key.data);
            if (!writePrivateFile(path, key.data, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
                result.error = QStringLiteral("Could not write the key file %1. Your sessions and settings were not changed.").arg(path);
                return result;
            }
            if (!key.publicData.isEmpty()) {
                writePrivateFile(path + QStringLiteral(".pub"), key.publicData,
                                 QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }
            restoredKeyPaths.insert(key.originalPath, QDir::fromNativeSeparators(path));
            ++result.keysRestored;
        }
    }

    if (mode == ImportMode::Replace) {
        QSet<QString> incoming;
        for (const auto &profile : contents.profiles)
            incoming.insert(profile.name());
        for (const auto &name : profiles.profileNames()) {
            if (!incoming.contains(name)) {
                profiles.removeProfile(name);
                ++result.removed;
            }
        }
        profiles.setFolders(contents.folders);
    } else {
        for (const auto &folder : contents.folders)
            profiles.addFolder(folder);
    }

    for (ConnectionProfile profile : contents.profiles) {
        QMap<QString, QString> props = profile.allProperties();
        const QString keyPath = props.value(kKeyProperty).trimmed();
        if (restoredKeyPaths.contains(keyPath))
            props.insert(kKeyProperty, restoredKeyPaths.value(keyPath));

        const QString downloadDir = props.value(kDownloadDirProperty);
        if (!downloadDir.isEmpty() && !directoryExists(downloadDir)) {
            props.remove(kDownloadDirProperty);
            ++result.pathsReset;
        }
        const QString logPath = props.value(kLogPathProperty);
        if (!logPath.isEmpty() && !QFileInfo(expandHome(logPath)).absoluteDir().exists()) {
            props.remove(kLogPathProperty);
            ++result.pathsReset;
        }
        profile.setAllProperties(props);

        if (profiles.hasProfile(profile.name()))
            ++result.updated;
        else
            ++result.added;
        profiles.updateProfile(profile);
        profiles.addFolder(profile.folder());
    }

    for (auto it = contents.settings.cbegin(); it != contents.settings.cend(); ++it) {
        const QString &key = it.key();
        if (kDirectorySettings.contains(key) && !directoryExists(it.value().toString())) {
            ++result.pathsReset;
            continue;
        }
        if (key == kRecentSetting) {
            const QStringList incoming = it.value().toStringList();
            settings.setValue(key, mode == ImportMode::Merge
                                       ? mergedRecents(incoming, settings.value(key).toStringList())
                                       : incoming);
        } else {
            settings.setValue(key, it.value());
        }
        ++result.settingsApplied;
    }
    settings.sync();

    const int knownHostsAdded = mergeKnownHosts(knownHostsPath, contents.knownHosts);
    if (knownHostsAdded < 0)
        result.error = QStringLiteral("Could not update %1.").arg(QDir::toNativeSeparators(knownHostsPath));
    else
        result.knownHostsAdded = knownHostsAdded;

    if (!profiles.saveProfiles())
        result.error = QStringLiteral("Settings were imported, but saving sessions to %1 failed.").arg(profilesPath);
    return result;
}

}
