#pragma once

#include "IConnection.h"
#include <QProcess>

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

public slots:
    void connectSession() override;
    void disconnectSession() override;
    void writeData(const QByteArray &data) override;
    void setTerminalSize(int rows, int columns) override;

private slots:
    void readProcessOutput();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(QProcess::ProcessError error);

private:
#ifndef Q_OS_WIN
    QString buildTarget() const;
#endif

    QString m_host;
    int m_port = 22;
    QString m_username;
    QString m_privateKey;

#ifdef Q_OS_WIN
    QProcess m_process;
#else
    int m_masterFd = -1;
    int m_childPid = -1;
#endif

    int m_rows = 24;
    int m_columns = 80;
};
