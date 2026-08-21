#pragma once

#include "IConnection.h"

#ifdef Q_OS_WIN
class WindowsPty;
#endif

class SshConnection final : public IConnection
{
    Q_OBJECT
public:
    explicit SshConnection(const QString &host,
                           int port,
                           const QString &username,
                           const QString &privateKey,
                           QObject *parent = nullptr);
    ~SshConnection() override;

    QString displayName() const override;
    bool isConnected() const override;

    QString host() const { return m_host; }
    int port() const { return m_port; }
    QString username() const { return m_username; }
    QString privateKey() const { return m_privateKey; }

public slots:
    void connectSession() override;
    void disconnectSession() override;
    void writeData(const QByteArray &data) override;
    void setTerminalSize(int rows, int columns) override;

private:
    QString buildTarget() const;

    QString m_host;
    int m_port = 22;
    QString m_username;
    QString m_privateKey;

#ifdef Q_OS_WIN
    WindowsPty *m_pty = nullptr;
#else
    void readProcessOutput();

    int m_masterFd = -1;
    int m_childPid = -1;
#endif

    int m_rows = 24;
    int m_columns = 80;
};
