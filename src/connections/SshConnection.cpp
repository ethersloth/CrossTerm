#include "SshConnection.h"

#include <QSocketNotifier>

#ifndef Q_OS_WIN
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
                             QObject *parent)
    : IConnection(parent)
    , m_host(host)
    , m_port(port)
    , m_username(username)
    , m_privateKey(privateKey)
{
#ifdef Q_OS_WIN
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyRead,
            this, &SshConnection::readProcessOutput);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SshConnection::processFinished);
    connect(&m_process, QOverload<QProcess::ProcessError>::of(&QProcess::error),
            this, &SshConnection::processError);
#endif
}

SshConnection::~SshConnection()
{
    disconnectSession();
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
    return m_process.state() != QProcess::NotRunning;
#else
    return m_masterFd != -1;
#endif
}

void SshConnection::connectSession()
{
    if (isConnected())
        return;

    if (m_host.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("SSH host is required"));
        return;
    }

#ifdef Q_OS_WIN
    QStringList args;
    args << QStringLiteral("-p") << QString::number(m_port);
    if (!m_privateKey.trimmed().isEmpty()) {
        args << QStringLiteral("-i") << m_privateKey;
    }

    QString target = m_host;
    if (!m_username.trimmed().isEmpty()) {
        target = QStringLiteral("%1@%2").arg(m_username, m_host);
    }
    args << target;

    m_process.start(QStringLiteral("ssh"), args);
    if (!m_process.waitForStarted(5000)) {
        emit errorOccurred(QStringLiteral("Unable to start ssh client"));
        return;
    }

    emit connected();
#else
    int masterFd = -1;
    pid_t pid = forkpty(&masterFd, nullptr, nullptr, nullptr);

    if (pid == -1) {
        emit errorOccurred(QStringLiteral("Failed to create PTY for SSH session"));
        return;
    }

    if (pid == 0) {
        QByteArray cmd = "ssh";
        std::vector<QByteArray> argStore;
        std::vector<char *> argv;

        argStore.emplace_back("ssh");
        argStore.emplace_back("-t");
        argStore.emplace_back("-p");
        argStore.emplace_back(QString::number(m_port).toUtf8());

        if (!m_privateKey.trimmed().isEmpty()) {
            argStore.emplace_back("-i");
            argStore.emplace_back(m_privateKey.toUtf8());
        }

        QString target = buildTarget();
        argStore.emplace_back(target.toUtf8());

        argv.reserve(argStore.size() + 1);
        for (QByteArray &arg : argStore) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp(cmd.constData(), argv.data());
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
#endif
}

void SshConnection::disconnectSession()
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
#endif

    emit disconnected();
}

void SshConnection::writeData(const QByteArray &data)
{
    if (!isConnected())
        return;

#ifdef Q_OS_WIN
    m_process.write(data);
#else
    if (m_masterFd != -1) {
        ssize_t written = write(m_masterFd, data.constData(), data.size());
        (void)written;
    }
#endif
}

void SshConnection::setTerminalSize(int rows, int columns)
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

    if (ioctl(m_masterFd, TIOCSWINSZ, &ws) == 0 && m_childPid > 0) {
        kill(m_childPid, SIGWINCH);
    }
#endif
}

void SshConnection::readProcessOutput()
{
#ifdef Q_OS_WIN
    if (m_process.bytesAvailable() > 0) {
        emit dataReceived(m_process.readAll());
    }
#else
    if (m_masterFd != -1) {
        char buffer[4096];
        ssize_t n = read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            emit dataReceived(QByteArray(buffer, static_cast<int>(n)));
        } else if (n <= 0 && (errno == EIO || errno == EBADF)) {
            disconnectSession();
        }
    }
#endif
}

void SshConnection::processFinished(int, QProcess::ExitStatus)
{
#ifdef Q_OS_WIN
    emit disconnected();
#endif
}

void SshConnection::processError(QProcess::ProcessError)
{
#ifdef Q_OS_WIN
    emit errorOccurred(m_process.errorString());
#endif
}

#ifndef Q_OS_WIN
QString SshConnection::buildTarget() const
{
    if (m_username.trimmed().isEmpty()) {
        return m_host;
    }
    return QStringLiteral("%1@%2").arg(m_username, m_host);
}
#endif
