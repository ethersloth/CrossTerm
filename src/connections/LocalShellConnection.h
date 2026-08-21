#pragma once

#include "IConnection.h"

#ifdef Q_OS_WIN
class WindowsPty;
#endif

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

private:
#ifdef Q_OS_WIN
    WindowsPty *m_pty = nullptr;
#else
    void readProcessOutput();
    void restoreTerminalMode();

    int m_masterFd = -1;
    int m_slavePid = -1;
    QByteArray m_originalTermiosSettings;
#endif
    int m_rows = 24;
    int m_columns = 80;
};
