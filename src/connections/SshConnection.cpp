#include "SshConnection.h"

#ifdef Q_OS_WIN
#include "WindowsPty.h"
#include <QDir>
#else
#include <QSocketNotifier>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#endif

SshConnection::SshConnection(const QString &host,
                             int port,
                             const QString &username,
                             const QString &privateKey,
                             const QString &password,
                             QObject *parent)
    : IConnection(parent)
    , m_host(host)
    , m_port(port)
    , m_username(username)
    , m_privateKey(privateKey)
    , m_password(password)
{
#ifdef Q_OS_WIN
    m_pty = new WindowsPty(this);
    connect(m_pty, &WindowsPty::dataReceived, this, &IConnection::dataReceived);
    connect(m_pty, &WindowsPty::dataReceived, this, &SshConnection::maybeSendSavedPassword);
    connect(m_pty, &WindowsPty::exited, this, &SshConnection::disconnectSession);
#endif
}

SshConnection::~SshConnection()
{
#ifdef Q_OS_WIN
    m_pty->stop();
#else
    disconnectSession();
#endif
}

QString SshConnection::displayName() const
{
    if (m_username.isEmpty()) {
        return QStringLiteral("SSH: %1").arg(m_host);
    }
    return QStringLiteral("SSH: %1@%2").arg(m_username, m_host);
}

bool SshConnection::isConnected() const
{
#ifdef Q_OS_WIN
    return m_pty->isRunning();
#else
    return m_masterFd != -1;
#endif
}

QString SshConnection::buildTarget() const
{
    if (m_username.trimmed().isEmpty()) {
        return m_host;
    }
    return QStringLiteral("%1@%2").arg(m_username, m_host);
}

#ifdef Q_OS_WIN

void SshConnection::connectSession()
{
    if (isConnected())
        return;

    if (m_host.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("SSH host is required"));
        return;
    }

    m_authPromptBuffer.clear();
    m_passwordSent = false;

    // -t forces remote pty allocation; the pseudo console makes our stdin a real
    // terminal, so ssh can also prompt for passwords and host-key confirmation.
    QString commandLine = QStringLiteral("ssh -t -p %1").arg(m_port);
    if (!m_privateKey.trimmed().isEmpty())
        commandLine += QStringLiteral(" -i \"%1\"").arg(QDir::toNativeSeparators(m_privateKey.trimmed()));
    commandLine += QStringLiteral(" \"%1\"").arg(buildTarget());

    QString error;
    if (!m_pty->start(commandLine, m_rows, m_columns, &error)) {
        emit errorOccurred(error);
        return;
    }

    emit connected();
}

void SshConnection::disconnectSession()
{
    if (!isConnected())
        return;

    m_pty->stop();
    emit disconnected();
}

void SshConnection::writeData(const QByteArray &data)
{
    m_pty->write(data);
}

void SshConnection::setTerminalSize(int rows, int columns)
{
    if (rows < 1 || columns < 1)
        return;

    m_rows = rows;
    m_columns = columns;
    m_pty->resize(rows, columns);
}

#else // !Q_OS_WIN

void SshConnection::connectSession()
{
    if (isConnected())
        return;

    if (m_host.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("SSH host is required"));
        return;
    }

    m_authPromptBuffer.clear();
    m_passwordSent = false;

    int masterFd = -1;
    pid_t pid = forkpty(&masterFd, nullptr, nullptr, nullptr);

    if (pid == -1) {
        emit errorOccurred(QStringLiteral("Failed to create PTY for SSH session"));
        return;
    }

    if (pid == 0) {
        std::vector<QByteArray> argStore;
        argStore.emplace_back("ssh");
        argStore.emplace_back("-t");
        argStore.emplace_back("-p");
        argStore.emplace_back(QString::number(m_port).toUtf8());

        if (!m_privateKey.trimmed().isEmpty()) {
            argStore.emplace_back("-i");
            argStore.emplace_back(m_privateKey.trimmed().toUtf8());
        }

        argStore.emplace_back(buildTarget().toUtf8());

        std::vector<char *> argv;
        argv.reserve(argStore.size() + 1);
        for (QByteArray &arg : argStore) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp("ssh", argv.data());
        _exit(127);
    }

    m_masterFd = masterFd;
    m_childPid = pid;

    setTerminalSize(m_rows, m_columns);

    int flags = fcntl(masterFd, F_GETFL, 0);
    fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

    auto *notifier = new QSocketNotifier(masterFd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &SshConnection::readProcessOutput);

    emit connected();
}

void SshConnection::disconnectSession()
{
    if (!isConnected())
        return;

    if (m_masterFd != -1) {
        close(m_masterFd);
        m_masterFd = -1;
    }

    if (m_childPid > 0) {
        kill(m_childPid, SIGTERM);

        int status = 0;
        int waitCount = 0;
        while (waitpid(m_childPid, &status, WNOHANG) == 0 && waitCount < 100) {
            usleep(10000);
            ++waitCount;
        }

        if (waitpid(m_childPid, &status, WNOHANG) == 0) {
            kill(m_childPid, SIGKILL);
            waitpid(m_childPid, &status, 0);
        }

        m_childPid = -1;
    }

    emit disconnected();
}

void SshConnection::writeData(const QByteArray &data)
{
    if (m_masterFd == -1)
        return;

    ssize_t written = write(m_masterFd, data.constData(), data.size());
    (void)written;
}

void SshConnection::setTerminalSize(int rows, int columns)
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

    if (ioctl(m_masterFd, TIOCSWINSZ, &ws) == 0 && m_childPid > 0) {
        kill(m_childPid, SIGWINCH);
    }
}

void SshConnection::readProcessOutput()
{
    if (m_masterFd == -1)
        return;

    char buffer[4096];
    ssize_t n = read(m_masterFd, buffer, sizeof(buffer));
    if (n > 0) {
        const QByteArray data(buffer, static_cast<int>(n));
        maybeSendSavedPassword(data);
        emit dataReceived(data);
    } else if (n <= 0 && (errno == EIO || errno == EBADF)) {
        disconnectSession();
    }
}

#endif // Q_OS_WIN

void SshConnection::maybeSendSavedPassword(const QByteArray &data)
{
    if (m_password.isEmpty() || m_passwordSent)
        return;

    m_authPromptBuffer.append(data);
    if (m_authPromptBuffer.size() > 1024)
        m_authPromptBuffer.remove(0, m_authPromptBuffer.size() - 1024);

    if (!m_authPromptBuffer.toLower().contains("password:"))
        return;

    m_passwordSent = true;
    writeData(m_password.toUtf8() + '\r');
    m_authPromptBuffer.clear();
}
