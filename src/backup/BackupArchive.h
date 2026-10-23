#pragma once

#include "../profiles/ConnectionProfile.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

class ProfileManager;
class QSettings;

// Everything a CrossTerm backup carries, and how it is gathered on one
// machine and applied on another. Encryption lives in BackupCrypto; this is
// the plaintext payload only.
namespace BackupArchive {

struct KeyFile {
    QString originalPath; // exactly as the profile stored it (may start with "~/")
    QString fileName;
    QByteArray data;
    QByteArray publicData; // matching ".pub", if one was next to the key
};

struct Contents {
    QDateTime createdAt;
    QString appVersion;
    QString sourceHost;
    QString sourcePlatform;
    QList<ConnectionProfile> profiles;
    QStringList folders;
    QVariantMap settings;
    QList<KeyFile> keyFiles;
    // Contents of ~/.ssh/known_hosts, so hosts need not be re-confirmed.
    QByteArray knownHosts;
    // Key paths referenced by profiles that could not be read at export time.
    QStringList missingKeys;
};

enum class ImportMode {
    Merge,   // add new sessions, overwrite same-named ones, keep the rest
    Replace, // remove sessions and folders that are not in the backup
};

struct ImportResult {
    int added = 0;
    int updated = 0;
    int removed = 0;
    int keysRestored = 0;
    int pathsReset = 0;
    int settingsApplied = 0;
    int knownHostsAdded = 0;
    QString safetyCopyPath;
    QString error;
};

QString defaultKnownHostsPath();
int knownHostCount(const QByteArray &knownHosts);

Contents collect(const ProfileManager &profiles, const QSettings &settings,
                 const QString &knownHostsPath = defaultKnownHostsPath());
QByteArray serialize(const Contents &contents);
std::optional<Contents> deserialize(const QByteArray &data, QString *error = nullptr);

// Applies a backup. Restored private keys are written under keyDirectory
// with owner-only permissions and profiles are pointed at them. Folder
// paths that do not exist on this machine are dropped so the local defaults
// apply. The current profiles file is copied aside first. Known-hosts lines
// missing from knownHostsPath are appended; existing lines are never changed.
ImportResult apply(const Contents &contents, ProfileManager &profiles, QSettings &settings,
                   ImportMode mode, const QString &keyDirectory,
                   const QString &knownHostsPath = defaultKnownHostsPath());

QString expandHome(const QString &path);

}
