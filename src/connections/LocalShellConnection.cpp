#include "LocalShellConnection.h"

#include <QStandardPaths>

#ifdef Q_OS_WIN
#include "WindowsPty.h"
#else
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <unistd.h>
#include <pty.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#endif

LocalShellConnection::LocalShellConnection(QObject *parent)
    : IConnection(parent)
{
#ifdef Q_OS_WIN
    m_pty = new WindowsPty(this);
    connect(m_pty, &WindowsPty::dataReceived, this, &IConnection::dataReceived);
    connect(m_pty, &WindowsPty::exited, this, &LocalShellConnection::disconnectSession);
#endif
}

LocalShellConnection::~LocalShellConnection()
{
#ifdef Q_OS_WIN
    m_pty->stop();
#else
    disconnectSession();
    restoreTerminalMode();
#endif
}

QString LocalShellConnection::displayName() const
{
    return QStringLiteral("Local Shell");
}

bool LocalShellConnection::isConnected() const
{
#ifdef Q_OS_WIN
    return m_pty->isRunning();
#else
    return m_masterFd != -1;
#endif
}

#ifdef Q_OS_WIN

void LocalShellConnection::connectSession()
{
    if (isConnected())
        return;

    QString programPath = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
    if (programPath.isEmpty())
        programPath = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (programPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("No PowerShell executable found on PATH"));
        return;
    }

    const QString commandLine = QStringLiteral("\"%1\" -NoLogo -NoProfile").arg(programPath);

    QString error;
    if (!m_pty->start(commandLine, m_rows, m_columns, &error)) {
        emit errorOccurred(error);
        return;
    }

    emit connected();
}

void LocalShellConnection::disconnectSession()
{
    if (!isConnected())
        return;

    m_pty->stop();
    emit disconnected();
}

void LocalShellConnection::writeData(const QByteArray &data)
{
    m_pty->write(data);
}

void LocalShellConnection::setTerminalSize(int rows, int columns)
{
    if (rows < 1 || columns < 1)
        return;

    m_rows = rows;
    m_columns = columns;
    m_pty->resize(rows, columns);
}

#else // !Q_OS_WIN

void LocalShellConnection::connectSession()
{
    if (isConnected())
        return;

    int masterFd = -1;
    const pid_t pid = forkpty(&masterFd, nullptr, nullptr, nullptr);
    if (pid == -1) {
        emit errorOccurred(QStringLiteral("Failed to create PTY"));
        return;
    }

    if (pid == 0) {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QByteArray shell = env.value(QStringLiteral("SHELL"), QStringLiteral("/bin/bash")).toUtf8();
        execl(shell.constData(), shell.constData(), "-i", nullptr);
        _exit(127);
    }

    m_masterFd = masterFd;
    m_slavePid = pid;

    setTerminalSize(m_rows, m_columns);

    const int flags = fcntl(masterFd, F_GETFL, 0);
    fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

    auto *notifier = new QSocketNotifier(masterFd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &LocalShellConnection::readProcessOutput);

    emit connected();
}

void LocalShellConnection::disconnectSession()
{
    if (!isConnected())
        return;

    if (m_masterFd != -1) {
        close(m_masterFd);
        m_masterFd = -1;
    }

    if (m_slavePid > 0) {
        kill(m_slavePid, SIGTERM);

        int status = 0;
        for (int i = 0; i < 100 && waitpid(m_slavePid, &status, WNOHANG) == 0; ++i)
            usleep(10000);

        if (waitpid(m_slavePid, &status, WNOHANG) == 0) {
            kill(m_slavePid, SIGKILL);
            waitpid(m_slavePid, &status, 0);
        }

        m_slavePid = -1;
    }

    emit disconnected();
}

void LocalShellConnection::writeData(const QByteArray &data)
{
    if (m_masterFd == -1)
        return;

    const ssize_t written = write(m_masterFd, data.constData(), data.size());
    (void)written;
}

void LocalShellConnection::setTerminalSize(int rows, int columns)
{
    if (rows < 1 || columns < 1)
        return;

    m_rows = rows;
    m_columns = columns;

    if (m_masterFd == -1)
        return;

    struct winsize ws{};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);

    if (ioctl(m_masterFd, TIOCSWINSZ, &ws) == 0 && m_slavePid > 0)
        kill(m_slavePid, SIGWINCH);
}

void LocalShellConnection::readProcessOutput()
{
    if (m_masterFd == -1)
        return;

    char buffer[4096];
    const ssize_t n = read(m_masterFd, buffer, sizeof(buffer));
    if (n > 0)
        emit dataReceived(QByteArray(buffer, static_cast<int>(n)));
    else if (n <= 0 && (errno == EIO || errno == EBADF))
        disconnectSession();
}

void LocalShellConnection::restoreTerminalMode()
{
    if (m_originalTermiosSettings.isEmpty())
        return;

    const auto *tios = reinterpret_cast<const struct termios *>(m_originalTermiosSettings.constData());
    tcsetattr(STDIN_FILENO, TCSANOW, tios);
}

#endif // Q_OS_WIN
