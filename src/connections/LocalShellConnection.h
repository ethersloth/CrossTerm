#pragma once

#include "IConnection.h"
#include <QProcess>

class LocalShellConnection final : public IConnection
{
    Q_OBJECT
public:
    explicit LocalShellConnection(QObject *parent = nullptr);
    ~LocalShellConnection() override;

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
    void checkProcessStatus();
private:
    void setupTerminalMode();
    void restoreTerminalMode();
    void setupRawMode();

#ifdef Q_OS_WIN
    QProcess m_process;
#else
    mutable int m_masterFd = -1;   // Master side of PTY
    mutable int m_slavePid = -1;   // Child process PID
    QByteArray m_originalTermiosSettings;  // Saved terminal settings
#endif
    int m_rows = 24;
    int m_columns = 80;
};

