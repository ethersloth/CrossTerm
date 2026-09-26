#pragma once

#include "ConnectionProfile.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>

class SecretStore;

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

    // With a store set, SSH passwords are kept there instead of in the
    // profiles file (existing plain-text ones are moved on load). Without
    // one, or if the store fails, passwords stay in the file. Not owned.
    void setSecretStore(SecretStore *store) { m_secretStore = store; }
    // Non-empty when the last load or save had to fall back or could not
    // read a stored password; suitable for showing to the user.
    QString secretStorageWarning() const { return m_secretWarning; }

private:
    QString getDefaultProfilesPath() const;

    QList<ConnectionProfile> m_profiles;
    QStringList m_folders;
    QString m_profilesPath;

    SecretStore *m_secretStore = nullptr;
    // Profile name -> password as currently held by the store, so saves only
    // touch the keychain when something changed and can drop stale entries.
    QHash<QString, QString> m_storedSecrets;
    // Profiles marked as keychain-backed whose password could not be read.
    // Their marker is preserved and their stored entry is never deleted.
    QSet<QString> m_unreadableSecrets;
    QString m_secretWarning;
};
