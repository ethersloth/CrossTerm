#include "ProfileManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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

    QJsonArray profilesArray;
    for (const auto &profile : m_profiles) {
        profilesArray.append(profile.toJson());
    }

    QJsonArray foldersArray;
    for (const auto &folder : m_folders) {
        foldersArray.append(folder);
    }

    QJsonObject root;
    root[QStringLiteral("profiles")] = profilesArray;
    root[QStringLiteral("folders")] = foldersArray;

    QJsonDocument doc(root);

    QFile file(m_profilesPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    file.write(doc.toJson());
    file.close();

    return true;
}

QString ProfileManager::getDefaultProfilesPath() const
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return configPath + QStringLiteral("/profiles.json");
}
