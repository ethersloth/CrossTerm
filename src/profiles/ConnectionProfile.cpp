#include "ConnectionProfile.h"
#include <QJsonObject>
#include <QJsonArray>

ConnectionProfile::ConnectionProfile(const QString &name, ConnectionType type)
    : m_name(name), m_type(type)
{
}

QString ConnectionProfile::property(const QString &key, const QString &defaultValue) const
{
    return m_properties.value(key, defaultValue);
}

void ConnectionProfile::setProperty(const QString &key, const QString &value)
{
    m_properties[key] = value;
}

bool ConnectionProfile::hasProperty(const QString &key) const
{
    return m_properties.contains(key);
}

QList<QPair<QString, QString>> ConnectionProfile::savedCommands() const
{
    QList<QPair<QString, QString>> commands;
    const QJsonDocument document = QJsonDocument::fromJson(
        property(QStringLiteral("saved_commands")).toUtf8());
    for (const auto &value : document.array()) {
        const QJsonObject commandObject = value.toObject();
        const QString name = commandObject.value(QStringLiteral("name")).toString().trimmed();
        const QString command = commandObject.value(QStringLiteral("command")).toString().trimmed();
        if (!name.isEmpty() && !command.isEmpty())
            commands.append(qMakePair(name, command));
    }
    return commands;
}

void ConnectionProfile::setSavedCommands(const QList<QPair<QString, QString>> &commands)
{
    QJsonArray commandArray;
    for (const auto &[name, command] : commands) {
        const QString trimmedName = name.trimmed();
        const QString trimmedCommand = command.trimmed();
        if (trimmedName.isEmpty() || trimmedCommand.isEmpty())
            continue;

        QJsonObject commandObject;
        commandObject[QStringLiteral("name")] = trimmedName;
        commandObject[QStringLiteral("command")] = trimmedCommand;
        commandArray.append(commandObject);
    }
    setProperty(QStringLiteral("saved_commands"),
                QString::fromUtf8(QJsonDocument(commandArray).toJson(QJsonDocument::Compact)));
}

QJsonObject ConnectionProfile::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("type")] = static_cast<int>(m_type);

    QJsonObject propsObj;
    for (auto it = m_properties.begin(); it != m_properties.end(); ++it) {
        propsObj[it.key()] = it.value();
    }
    obj[QStringLiteral("properties")] = propsObj;

    return obj;
}

ConnectionProfile ConnectionProfile::fromJson(const QJsonObject &obj)
{
    ConnectionProfile profile;
    profile.m_name = obj[QStringLiteral("name")].toString();
    profile.m_type = static_cast<ConnectionType>(obj[QStringLiteral("type")].toInt());

    QJsonObject propsObj = obj[QStringLiteral("properties")].toObject();
    for (auto it = propsObj.begin(); it != propsObj.end(); ++it) {
        profile.m_properties[it.key()] = it.value().toString();
    }

    return profile;
}
