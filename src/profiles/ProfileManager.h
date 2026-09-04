#pragma once

#include "ConnectionProfile.h"

#include <QString>
#include <QList>
#include <memory>

/**
 * Manages saved connection profiles.
 * Handles loading and saving profiles from/to disk.
 */
class ProfileManager
{
public:
    ProfileManager();

    // Profile operations
    void addProfile(const ConnectionProfile &profile);
    void updateProfile(const ConnectionProfile &profile);
    void removeProfile(const QString &name);
    ConnectionProfile profile(const QString &name) const;
    QList<ConnectionProfile> allProfiles() const { return m_profiles; }
    QStringList profileNames() const;
    bool hasProfile(const QString &name) const;

    // Session folder operations. Folder paths are "/"-separated (e.g. "Work/Prod").
    QStringList folders() const { return m_folders; }
    void setFolders(const QStringList &folders);
    void addFolder(const QString &path);
    void removeFolder(const QString &path);
    void renameFolder(const QString &oldPath, const QString &newPath);
    bool hasFolder(const QString &path) const { return m_folders.contains(path); }

    // File I/O
    bool loadProfiles();
    bool saveProfiles();

    // Configuration
    QString profilesPath() const { return m_profilesPath; }
    void setProfilesPath(const QString &path) { m_profilesPath = path; }

private:
    QString getDefaultProfilesPath() const;

    QList<ConnectionProfile> m_profiles;
    QStringList m_folders;
    QString m_profilesPath;
};
