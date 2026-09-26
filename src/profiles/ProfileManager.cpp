#include "ProfileManager.h"
#include "SecretStore.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace {
const QString kPasswordProperty = QStringLiteral("ssh_password");
// Written instead of the password when it lives in the secret store.
const QString kPasswordStoreProperty = QStringLiteral("ssh_password_store");
const QString kPasswordStoreKeychain = QStringLiteral("keychain");
}

ProfileManager::ProfileManager()
{
    m_profilesPath = getDefaultProfilesPath();
}

void ProfileManager::addProfile(const ConnectionProfile &profile)
{
    if (!hasProfile(profile.name())) {
        m_profiles.append(profile);
    }
}

void ProfileManager::updateProfile(const ConnectionProfile &profile)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].name() == profile.name()) {
            m_profiles[i] = profile;
            return;
        }
    }
    m_profiles.append(profile);
}

void ProfileManager::removeProfile(const QString &name)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].name() == name) {
            m_profiles.removeAt(i);
            return;
        }
    }
}

ConnectionProfile ProfileManager::profile(const QString &name) const
{
    for (const auto &profile : m_profiles) {
        if (profile.name() == name) {
            return profile;
        }
    }
    return ConnectionProfile();
}

QStringList ProfileManager::profileNames() const
{
    QStringList names;
    for (const auto &profile : m_profiles) {
        names << profile.name();
    }
    return names;
}

bool ProfileManager::hasProfile(const QString &name) const
{
    for (const auto &profile : m_profiles) {
        if (profile.name() == name) {
            return true;
        }
    }
    return false;
}

namespace {
bool isPathOrDescendant(const QString &path, const QString &ancestor)
{
    return path == ancestor || path.startsWith(ancestor + QStringLiteral("/"));
}
}

void ProfileManager::setFolders(const QStringList &folders)
{
    m_folders.clear();
    for (const auto &path : folders) {
        if (!path.isEmpty() && !m_folders.contains(path))
            m_folders.append(path);
    }
}

void ProfileManager::addFolder(const QString &path)
{
    if (path.isEmpty())
        return;

    // Also register any implied parent folders so they show up even if empty.
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current;
    for (const auto &part : parts) {
        current = current.isEmpty() ? part : current + QStringLiteral("/") + part;
        if (!m_folders.contains(current))
            m_folders.append(current);
    }
}

void ProfileManager::removeFolder(const QString &path)
{
    if (path.isEmpty())
        return;

    // Sessions inside the removed folder (or its subfolders) move to the top level.
    for (auto &profile : m_profiles) {
        if (isPathOrDescendant(profile.folder(), path))
            profile.setFolder(QString());
    }

    QStringList kept;
    for (const auto &f : std::as_const(m_folders)) {
        if (!isPathOrDescendant(f, path))
            kept.append(f);
    }
    m_folders = kept;
}

void ProfileManager::renameFolder(const QString &oldPath, const QString &newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath)
        return;

    for (auto &profile : m_profiles) {
        const QString f = profile.folder();
        if (f == oldPath) {
            profile.setFolder(newPath);
        } else if (f.startsWith(oldPath + QStringLiteral("/"))) {
            profile.setFolder(newPath + f.mid(oldPath.length()));
        }
    }

    for (auto &f : m_folders) {
        if (f == oldPath) {
            f = newPath;
        } else if (f.startsWith(oldPath + QStringLiteral("/"))) {
            f = newPath + f.mid(oldPath.length());
        }
    }

    addFolder(newPath);
}

bool ProfileManager::loadProfiles()
{
    QFile file(m_profilesPath);
    if (!file.exists())
        return true;  // No profiles yet - not an error

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_profiles.clear();
    m_folders.clear();

    QJsonArray profilesArray;
    if (doc.isArray()) {
        // Legacy format: a bare array of profiles, no folders.
        profilesArray = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        profilesArray = root[QStringLiteral("profiles")].toArray();
        const QJsonArray foldersArray = root[QStringLiteral("folders")].toArray();
        for (const auto &value : foldersArray) {
            const QString path = value.toString();
            if (!path.isEmpty() && !m_folders.contains(path))
                m_folders.append(path);
        }
    } else {
        return false;
    }

    for (const auto &value : profilesArray) {
        if (value.isObject()) {
            m_profiles.append(ConnectionProfile::fromJson(value.toObject()));
        }
    }

    m_storedSecrets.clear();
    m_unreadableSecrets.clear();
    m_secretWarning.clear();
    const bool storeAvailable = m_secretStore && m_secretStore->isAvailable();
    bool hasPlainTextPasswords = false;
    for (auto &profile : m_profiles) {
        if (profile.property(kPasswordStoreProperty) != kPasswordStoreKeychain) {
            hasPlainTextPasswords = hasPlainTextPasswords || !profile.sshPassword().isEmpty();
            continue;
        }
        const std::optional<QString> secret =
            storeAvailable ? m_secretStore->read(profile.name()) : std::nullopt;
        if (!secret) {
            // Keep the marker so the entry is neither lost nor deleted.
            m_unreadableSecrets.insert(profile.name());
            continue;
        }
        QMap<QString, QString> props = profile.allProperties();
        props.remove(kPasswordStoreProperty);
        props.insert(kPasswordProperty, *secret);
        profile.setAllProperties(props);
        m_storedSecrets.insert(profile.name(), *secret);
    }
    if (!m_unreadableSecrets.isEmpty()) {
        m_secretWarning = QStringLiteral("Could not read saved passwords for %1 session(s) from the system keychain. "
                                         "Those sessions will ask for a password.")
                              .arg(m_unreadableSecrets.size());
    }

    // First run with a keychain: move plain-text passwords out of the file.
    if (storeAvailable && hasPlainTextPasswords)
        saveProfiles();

    return true;
}

bool ProfileManager::saveProfiles()
{
    // Ensure directory exists
    QDir dir(QFileInfo(m_profilesPath).absolutePath());
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral(".")))
            return false;
    }

    const bool storeAvailable = m_secretStore && m_secretStore->isAvailable();
    int plainTextFallbacks = 0;
    QSet<QString> stillStored;

    QJsonArray profilesArray;
    for (const auto &profile : m_profiles) {
        QJsonObject json = profile.toJson();
        const QString password = profile.sshPassword();
        if (storeAvailable && !password.isEmpty()) {
            const auto stored = m_storedSecrets.constFind(profile.name());
            const bool upToDate = stored != m_storedSecrets.constEnd() && *stored == password;
            if (upToDate || m_secretStore->write(profile.name(), password)) {
                m_storedSecrets.insert(profile.name(), password);
                m_unreadableSecrets.remove(profile.name());
                stillStored.insert(profile.name());
                QJsonObject props = json[QStringLiteral("properties")].toObject();
                props.remove(kPasswordProperty);
                props.insert(kPasswordStoreProperty, kPasswordStoreKeychain);
                json[QStringLiteral("properties")] = props;
            } else {
                ++plainTextFallbacks;
            }
        }
        profilesArray.append(json);
    }

    // Drop keychain entries for sessions that were deleted, renamed, or had
    // their password cleared.
    if (storeAvailable) {
        for (auto it = m_storedSecrets.begin(); it != m_storedSecrets.end();) {
            if (!stillStored.contains(it.key()) && m_secretStore->remove(it.key()))
                it = m_storedSecrets.erase(it);
            else
                ++it;
        }
    }

    if (plainTextFallbacks > 0) {
        m_secretWarning = QStringLiteral("Could not save %1 password(s) to %2; they were kept in the profiles file instead.")
                              .arg(plainTextFallbacks)
                              .arg(m_secretStore->name());
    } else if (m_unreadableSecrets.isEmpty()) {
        m_secretWarning.clear();
    }

    QJsonArray foldersArray;
    for (const auto &folder : m_folders) {
        foldersArray.append(folder);
    }

    QJsonObject root;
    root[QStringLiteral("profiles")] = profilesArray;
    root[QStringLiteral("folders")] = foldersArray;

    QJsonDocument doc(root);

    // Owner-only: the file can still hold passwords when no keychain is available.
    QFile file(m_profilesPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QByteArray data = doc.toJson();
    const bool written = file.write(data) == data.size();
    file.close();

    return written;
}

QString ProfileManager::getDefaultProfilesPath() const
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return configPath + QStringLiteral("/profiles.json");
}
