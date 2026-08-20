#include "LocalShellConnection.h"

#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSocketNotifier>
#include <QTimer>

#ifndef Q_OS_WIN
#include <unistd.h>
#include <pty.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#endif

#ifdef Q_OS_WIN
LocalShellConnection::LocalShellConnection(QObject *parent)
    : IConnection(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyRead,
            this, &LocalShellConnection::readProcessOutput);
    connect(&m_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &LocalShellConnection::processFinished);
    // Note: QProcess::error() is a getter, errorOccurred() is the signal
    connect(&m_process, 
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this,
            &LocalShellConnection::processError);
}
#else
LocalShellConnection::LocalShellConnection(QObject *parent)
    : IConnection(parent)
{
}
#endif

LocalShellConnection::~LocalShellConnection()
{
    disconnectSession();
    restoreTerminalMode();
}

QString LocalShellConnection::displayName() const
{
    return QStringLiteral("Local Shell");
}

bool LocalShellConnection::isConnected() const
{
#ifdef Q_OS_WIN
    return m_process.state() != QProcess::NotRunning;
#else
    return m_masterFd != -1;
#endif
}

void LocalShellConnection::connectSession()
{
    if (isConnected())
        return;

#ifdef Q_OS_WIN
    // Start a single interactive shell process. Nesting PowerShell can mangle input parsing.
    QString program = QStringLiteral("pwsh.exe");
    QStringList arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile")};

    // Fallback for systems without PowerShell 7 on PATH.
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        program = QStringLiteral("powershell.exe");
        arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile")};
    }

    m_process.start(program, arguments);

    if (!m_process.waitForStarted(3000)) {
        emit errorOccurred(QStringLiteral("Unable to start local shell"));
        return;
    }

    emit connected();
#else
    // Unix: Use PTY for proper terminal emulation
    int master_fd;
    
    // Create PTY
    pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);
    
    if (pid == -1) {
        emit errorOccurred(QStringLiteral("Failed to create PTY"));
        return;
    }
    
    if (pid == 0) {
        // Child process - execute shell
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString shell = env.value(QStringLiteral("SHELL"), QStringLiteral("/bin/bash"));
        
        // Execute shell
        const char *shell_cstr = shell.toUtf8().constData();
        execl(shell_cstr, shell_cstr, "-i", nullptr);
        
        // If we get here, execl failed
        exit(127);
    }
    
    // Parent process - set up master FD
    m_masterFd = master_fd;
    m_slavePid = pid;

    setTerminalSize(m_rows, m_columns);
    
    // Set master FD to non-blocking
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Use a socket notifier to read from the PTY
    QSocketNotifier *notifier = new QSocketNotifier(master_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &LocalShellConnection::readProcessOutput);
    
    emit connected();
#endif
}

void LocalShellConnection::disconnectSession()
{
    if (!isConnected())
        return;

#ifdef Q_OS_WIN
    m_process.terminate();
    if (!m_process.waitForFinished(1000)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
#else
    if (m_masterFd != -1) {
        close(m_masterFd);
        m_masterFd = -1;
    }
    
    // Kill the child process
    if (m_slavePid > 0) {
        kill(m_slavePid, SIGTERM);
        
        // Wait for it to terminate
        int status;
        int waitCount = 0;
        while (waitpid(m_slavePid, &status, WNOHANG) == 0 && waitCount < 100) {
            usleep(10000);  // 10ms
            waitCount++;
        }
        
        // If still running, force kill
        if (waitpid(m_slavePid, &status, WNOHANG) == 0) {
            kill(m_slavePid, SIGKILL);
            waitpid(m_slavePid, &status, 0);
        }
        
        m_slavePid = -1;
    }
#endif
    
    emit disconnected();
}

void LocalShellConnection::writeData(const QByteArray &data)
{
    if (!isConnected())
        return;

#ifdef Q_OS_WIN
    m_process.write(data);
#else
    if (m_masterFd != -1) {
        ssize_t written = write(m_masterFd, data.constData(), data.size());
        (void)written;  // Avoid unused variable warning
    }
#endif
}

void LocalShellConnection::setTerminalSize(int rows, int columns)
{
    if (rows < 1 || columns < 1)
        return;

    m_rows = rows;
    m_columns = columns;

#ifndef Q_OS_WIN
    if (m_masterFd == -1)
        return;

    struct winsize ws;
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(m_masterFd, TIOCSWINSZ, &ws) == 0 && m_slavePid > 0) {
        kill(m_slavePid, SIGWINCH);
    }
#endif
}

void LocalShellConnection::readProcessOutput()
{
#ifdef Q_OS_WIN
    if (m_process.canReadLine() || m_process.bytesAvailable() > 0) {
        emit dataReceived(m_process.readAll());
    }
#else
    if (m_masterFd != -1) {
        char buffer[4096];
        ssize_t n = read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            emit dataReceived(QByteArray(buffer, n));
        } else if (n <= 0 && (errno == EIO || errno == EBADF)) {
            // PTY closed
            disconnectSession();
        }
    }
#endif
}

void LocalShellConnection::processFinished(int, QProcess::ExitStatus)
{
#ifdef Q_OS_WIN
    emit disconnected();
#endif
}

void LocalShellConnection::processError(QProcess::ProcessError)
{
#ifdef Q_OS_WIN
    emit errorOccurred(m_process.errorString());
#endif
}

void LocalShellConnection::checkProcessStatus()
{
#ifndef Q_OS_WIN
    if (m_slavePid > 0) {
        int status;
        pid_t result = waitpid(m_slavePid, &status, WNOHANG);
        if (result == m_slavePid) {
            // Child has exited
            if (m_masterFd != -1) {
                close(m_masterFd);
                m_masterFd = -1;
            }
            m_slavePid = -1;
            emit disconnected();
        }
    }
#endif
}

void LocalShellConnection::setupTerminalMode()
{
#ifndef Q_OS_WIN
    // Save current terminal settings
    struct termios tios;
    if (tcgetattr(STDIN_FILENO, &tios) == 0) {
        m_originalTermiosSettings = QByteArray(reinterpret_cast<const char*>(&tios), sizeof(tios));
    }
#endif
}

void LocalShellConnection::restoreTerminalMode()
{
#ifndef Q_OS_WIN
    if (!m_originalTermiosSettings.isEmpty()) {
        const struct termios *tios = reinterpret_cast<const struct termios*>(m_originalTermiosSettings.constData());
        tcsetattr(STDIN_FILENO, TCSANOW, tios);
    }
#endif
}

void LocalShellConnection::setupRawMode()
{
#ifndef Q_OS_WIN
    if (m_masterFd == -1)
        return;

    struct termios tios;
    if (tcgetattr(m_masterFd, &tios) != 0)
        return;

    // Save original settings
    m_originalTermiosSettings = QByteArray(reinterpret_cast<const char*>(&tios), sizeof(tios));

    // Set raw mode
    cfmakeraw(&tios);
    // Keep these settings to match typical terminal behavior
    tios.c_cc[VMIN] = 0;   // Non-blocking read
    tios.c_cc[VTIME] = 0;
    
    tcsetattr(m_masterFd, TCSANOW, &tios);
#endif
}
