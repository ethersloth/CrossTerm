#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>

/**
 * Represents a saved connection profile.
 * Can be used to quickly create new sessions with saved settings.
 */
class ConnectionProfile
{
public:
    enum class ConnectionType {
        LocalShell,
        SSH,
        Serial,
        Telnet,
    };

    ConnectionProfile() = default;
    explicit ConnectionProfile(const QString &name, ConnectionType type = ConnectionType::LocalShell);

    // Basic info
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    ConnectionType type() const { return m_type; }
    void setType(ConnectionType type) { m_type = type; }

    // Generic property storage
    QString property(const QString &key, const QString &defaultValue = QString()) const;
    void setProperty(const QString &key, const QString &value);
    bool hasProperty(const QString &key) const;
    QMap<QString, QString> allProperties() const { return m_properties; }
    void setAllProperties(const QMap<QString, QString> &props) { m_properties = props; }

    // Common SSH properties
    QString sshHost() const { return property(QStringLiteral("ssh_host")); }
    void setSshHost(const QString &host) { setProperty(QStringLiteral("ssh_host"), host); }

    int sshPort() const { return property(QStringLiteral("ssh_port"), QStringLiteral("22")).toInt(); }
    void setSshPort(int port) { setProperty(QStringLiteral("ssh_port"), QString::number(port)); }

    QString sshUsername() const { return property(QStringLiteral("ssh_username")); }
    void setSshUsername(const QString &user) { setProperty(QStringLiteral("ssh_username"), user); }

    QString sshPassword() const { return property(QStringLiteral("ssh_password")); }
    void setSshPassword(const QString &pwd) { setProperty(QStringLiteral("ssh_password"), pwd); }

    QString sshPrivateKey() const { return property(QStringLiteral("ssh_private_key")); }
    void setSshPrivateKey(const QString &keyPath) { setProperty(QStringLiteral("ssh_private_key"), keyPath); }

    // Common Serial properties
    QString serialPort() const { return property(QStringLiteral("serial_port")); }
    void setSerialPort(const QString &port) { setProperty(QStringLiteral("serial_port"), port); }

    int serialBaud() const { return property(QStringLiteral("serial_baud"), QStringLiteral("9600")).toInt(); }
    void setSerialBaud(int baud) { setProperty(QStringLiteral("serial_baud"), QString::number(baud)); }

    int serialDataBits() const { return property(QStringLiteral("serial_databits"), QStringLiteral("8")).toInt(); }
    void setSerialDataBits(int bits) { setProperty(QStringLiteral("serial_databits"), QString::number(bits)); }

    // Common Telnet properties
    QString telnetHost() const { return property(QStringLiteral("telnet_host")); }
    void setTelnetHost(const QString &host) { setProperty(QStringLiteral("telnet_host"), host); }

    int telnetPort() const { return property(QStringLiteral("telnet_port"), QStringLiteral("23")).toInt(); }
    void setTelnetPort(int port) { setProperty(QStringLiteral("telnet_port"), QString::number(port)); }

    QString fontFamily() const { return property(QStringLiteral("font_family"), QStringLiteral("Consolas")); }
    void setFontFamily(const QString &family) { setProperty(QStringLiteral("font_family"), family); }

    int fontSize() const { return property(QStringLiteral("font_size"), QStringLiteral("12")).toInt(); }
    void setFontSize(int pointSize) { setProperty(QStringLiteral("font_size"), QString::number(pointSize)); }

    QString terminalType() const { return property(QStringLiteral("terminal_type"), QStringLiteral("xterm")); }
    void setTerminalType(const QString &terminalType) { setProperty(QStringLiteral("terminal_type"), terminalType); }

    QString downloadDirectory() const { return property(QStringLiteral("download_directory")); }
    void setDownloadDirectory(const QString &directory) { setProperty(QStringLiteral("download_directory"), directory); }

    // Serialization
    QJsonObject toJson() const;
    static ConnectionProfile fromJson(const QJsonObject &obj);

private:
    QString m_name;
    ConnectionType m_type = ConnectionType::LocalShell;
    QMap<QString, QString> m_properties;
};
