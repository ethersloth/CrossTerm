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

bool ProfileManager::loadProfiles()
{
    QFile file(m_profilesPath);
    if (!file.exists())
        return true;  // No profiles yet - not an error

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray())
        return false;

    m_profiles.clear();
    QJsonArray array = doc.array();
    for (const auto &value : array) {
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

    QJsonArray array;
    for (const auto &profile : m_profiles) {
        array.append(profile.toJson());
    }

    QJsonDocument doc(array);

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
