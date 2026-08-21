#include "ZModemProtocol.h"
#include "../connections/IConnection.h"
#include "../connections/SshConnection.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <cstring>

// Static logging function
static void logZModemEvent(const QString &message)
{
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/crossterm_zmodem.log";
    QFile logFile(logPath);
    if (!logFile.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " [ZModem] " << message << "\n";
    out.flush();
    logFile.close();
}

static QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(quoted);
}

ZModemProtocol::ZModemProtocol(QObject *parent)
    : QObject(parent)
{
}

ZModemProtocol::~ZModemProtocol()
{
    if (m_file)
        m_file->close();
}

void ZModemProtocol::sendFile(IConnection *connection, const QString &filepath, const QString &remoteWorkingDirectory)
{
    startExternalTransfer(connection, filepath, true, QString(), remoteWorkingDirectory);
}

void ZModemProtocol::receiveFile(IConnection *connection, const QString &destPath, const QString &remotePath, const QString &remoteWorkingDirectory)
{
    startExternalTransfer(connection, destPath, false, remotePath, remoteWorkingDirectory);
}

void ZModemProtocol::startExternalTransfer(IConnection *connection, const QString &path, bool upload, const QString &remotePath, const QString &remoteWorkingDirectory)
{
    auto *ssh = qobject_cast<SshConnection *>(connection);
    if (!ssh) {
        emit transferCompleted(false, "ZModem requires an SSH connection");
        return;
    }

    if (path.isEmpty()) {
        emit transferCompleted(false, "Transfer path is empty");
        return;
    }

    const QString helperName = upload ? QStringLiteral("sz.exe") : QStringLiteral("rz.exe");
    QString helper = qEnvironmentVariable(upload ? "CROSSTERM_SZ" : "CROSSTERM_RZ");
    if (helper.isEmpty())
        helper = QCoreApplication::applicationDirPath() + QDir::separator() + helperName;
    if (!QFileInfo::exists(helper))
        helper = QStandardPaths::findExecutable(upload ? QStringLiteral("sz") : QStringLiteral("rz"));
    if (!QFileInfo::exists(helper)) {
        emit transferCompleted(false, QStringLiteral("Missing %1. Set CROSSTERM_%2 to the lrzsz executable.")
                              .arg(helperName, upload ? QStringLiteral("SZ") : QStringLiteral("RZ")));
        return;
    }

    m_connection = connection;
    disconnect(m_connection, &IConnection::dataReceived, this, &ZModemProtocol::processIncomingData);
    m_filename = path;
    m_transferActive = true;
    m_isUpload = upload;
    m_bytesSent = 0;
    m_bytesReceived = 0;
    m_fileSize = upload ? QFileInfo(path).size() : 0;
    m_zmodemProcessFinished = false;
    m_sshTransferProcessFinished = false;
    m_zmodemProcessStarted = false;
    m_zmodemExitCode = -1;
    m_sshTransferExitCode = -1;

    m_connection->writeData(QByteArray::fromHex("1818181818181818181818080808080808080808"));

    QStringList sshArgs{QStringLiteral("-T"), QStringLiteral("-p"), QString::number(ssh->port())};
    if (!ssh->privateKey().trimmed().isEmpty())
        sshArgs << QStringLiteral("-i") << ssh->privateKey().trimmed();
    const bool useSavedPassword = ssh->privateKey().trimmed().isEmpty() && !ssh->password().isEmpty();
    if (useSavedPassword) {
        sshArgs << QStringLiteral("-o") << QStringLiteral("PreferredAuthentications=password,keyboard-interactive")
                << QStringLiteral("-o") << QStringLiteral("PubkeyAuthentication=no");
    }
    sshArgs << QStringLiteral("%1@%2").arg(ssh->username(), ssh->host());
    QString remoteCommandPrefix;
    if (!remoteWorkingDirectory.isEmpty()) {
        const QString directoryArgument = remoteWorkingDirectory.startsWith(QLatin1Char('$'))
            ? QStringLiteral("\"%1\"").arg(remoteWorkingDirectory)
            : shellQuote(remoteWorkingDirectory);
        remoteCommandPrefix = QStringLiteral("cd %1 && ").arg(directoryArgument);
    }

    if (upload)
        sshArgs << remoteCommandPrefix + QStringLiteral("rz -b -y");
    else {
        QString sourcePath = QFileInfo(path).fileName();
        if (QDir::isRelativePath(sourcePath))
            sourcePath = QStringLiteral("$PWD/") + sourcePath;
        const QString sourceArgument = sourcePath.startsWith(QLatin1Char('$'))
            ? QStringLiteral("\"%1\"").arg(sourcePath)
            : shellQuote(sourcePath);
        sshArgs << remoteCommandPrefix + QStringLiteral("sz -b -y -- %1").arg(sourceArgument);
    }

    logZModemEvent(QStringLiteral("Remote transfer command: ssh %1").arg(sshArgs.join(QLatin1Char(' '))));

    m_sshTransferProcess = std::make_unique<QProcess>(this);
    m_zmodemProcess = std::make_unique<QProcess>(this);
    m_sshTransferProcess->setProgram(QStringLiteral("ssh"));
    m_sshTransferProcess->setArguments(sshArgs);
    if (useSavedPassword) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("SSH_ASKPASS"), QCoreApplication::applicationFilePath());
        environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        environment.insert(QStringLiteral("CROSSTERM_SSH_ASKPASS_PASSWORD"), ssh->password());
        m_sshTransferProcess->setProcessEnvironment(environment);
    }
    m_zmodemProcess->setProgram(helper);
    m_zmodemProcess->setArguments(upload
                                  ? QStringList{QStringLiteral("-b"), QStringLiteral("-y"), path}
                                  : QStringList{QStringLiteral("-b"), QStringLiteral("-y")});
    if (!upload)
        m_zmodemProcess->setWorkingDirectory(QFileInfo(path).absolutePath());

    auto pump = [](QProcess *source, QProcess *destination) {
        if (!source || !destination)
            return;
        const QByteArray bytes = source->readAllStandardOutput();
        if (!bytes.isEmpty())
            destination->write(bytes);
    };
    connect(m_zmodemProcess.get(), &QProcess::readyReadStandardOutput, this,
            [this, pump] { pump(m_zmodemProcess.get(), m_sshTransferProcess.get()); });
    connect(m_sshTransferProcess.get(), &QProcess::readyReadStandardOutput, this,
            [this, pump] { pump(m_sshTransferProcess.get(), m_zmodemProcess.get()); });
    connect(m_zmodemProcess.get(), &QProcess::readyReadStandardError, this, [this] {
        const QString message = QString::fromLocal8Bit(m_zmodemProcess->readAllStandardError());
        logZModemEvent(QStringLiteral("lrzsz stderr: %1").arg(message));
        const QRegularExpression progressPattern(QStringLiteral("Bytes (?:Sent|Received):\\s*(\\d+)"));
        const QRegularExpressionMatch match = progressPattern.match(message);
        if (match.hasMatch())
            emit transferProgress(match.captured(1).toLongLong(), m_fileSize);
    });
    connect(m_sshTransferProcess.get(), &QProcess::readyReadStandardError, this, [this] {
        const QByteArray errorBytes = m_sshTransferProcess->readAllStandardError();
        const QString message = QString::fromLocal8Bit(errorBytes);
        logZModemEvent(QStringLiteral("ssh transfer stderr: %1").arg(message));
    });
    connect(m_zmodemProcess.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                m_zmodemProcessFinished = true;
                m_zmodemExitCode = code;
                if (code != 0) {
                    stopExternalTransfer(false, QStringLiteral("lrzsz exited with code %1").arg(code));
                    return;
                }
                if (m_sshTransferProcessFinished && m_sshTransferExitCode == 0) {
                    emit transferProgress(m_fileSize, m_fileSize);
                    stopExternalTransfer(true, QStringLiteral("Transfer complete"));
                }
            });
    connect(m_sshTransferProcess.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                m_sshTransferProcessFinished = true;
                m_sshTransferExitCode = code;
                if (code != 0) {
                    stopExternalTransfer(false, QStringLiteral("SSH transfer exited with code %1").arg(code));
                    return;
                }
                if (m_zmodemProcessFinished && m_zmodemExitCode == 0) {
                    emit transferProgress(m_fileSize, m_fileSize);
                    stopExternalTransfer(true, QStringLiteral("Transfer complete"));
                }
            });

    emit statusUpdated(upload ? QStringLiteral("Starting ZModem upload") : QStringLiteral("Starting ZModem download"));
    logZModemEvent(QStringLiteral("Starting external lrzsz transfer: %1").arg(path));
    setState(State::Idle);
    m_sshTransferProcess->start();
    m_zmodemProcessStarted = true;
    m_zmodemProcess->start();
}

void ZModemProtocol::stopExternalTransfer(bool success, const QString &message)
{
    if (!m_transferActive)
        return;
    m_transferActive = false;
    if (m_zmodemProcess)
        m_zmodemProcess->kill();
    if (m_sshTransferProcess)
        m_sshTransferProcess->kill();
    if (m_connection)
        disconnect(m_connection, &IConnection::dataReceived, this, &ZModemProtocol::processIncomingData);

    auto releaseProcess = [this](std::unique_ptr<QProcess> &process) {
        QProcess *rawProcess = process.release();
        if (!rawProcess)
            return;

        rawProcess->disconnect(this);
        disconnect(rawProcess, nullptr, this, nullptr);
        if (rawProcess->state() != QProcess::NotRunning)
            rawProcess->kill();
        rawProcess->deleteLater();
    };

    releaseProcess(m_zmodemProcess);
    releaseProcess(m_sshTransferProcess);
    emit transferCompleted(success, message);
}

void ZModemProtocol::processIncomingData(const QByteArray &data)
{
    if (!m_transferActive || !m_connection)
        return;

    if (m_isUpload) {
        // For upload, we mainly ignore incoming data (just acknowledge it)
        logZModemEvent(QString("Received %1 bytes during upload").arg(data.size()));
        return;
    }

    m_incomingBuffer.append(data);
    m_bytesReceived += data.size();
    emit transferProgress(m_bytesReceived, m_fileSize);
    
    // Write received data to file
    if (m_file && m_file->isOpen()) {
        m_file->write(data);
    }

    logZModemEvent(QString("Received %1 bytes (total: %2)").arg(data.size()).arg(m_bytesReceived));
}

void ZModemProtocol::processState()
{

    if (!m_connection || m_sshTransferProcess || m_zmodemProcess)
        return;
    
    switch (m_state) {
    case State::SendingInitiation: {
        logZModemEvent("State: SendingInitiation - Waiting for remote to be ready");
        // Give remote time to prepare
        QTimer::singleShot(500, this, [this]() {
            logZModemEvent("Starting data transmission");
            setState(State::SendingFileData);
            processState();
        });
        break;
    }
    
    case State::SendingFileData: {
        logZModemEvent("State: SendingFileData");
        emit statusUpdated("Sending file data");
        readMoreFileData();
        break;
    }
    
    case State::SendingEof: {
        logZModemEvent("State: SendingEof");
        emit statusUpdated("File transfer complete");
        setState(State::Completed);
        m_transferActive = false;
        if (m_file) m_file->close();
        if (m_connection) disconnect(m_connection, &IConnection::dataReceived, this, &ZModemProtocol::processIncomingData);
        emit transferCompleted(true, "Transfer complete");
        break;
    }
    
    default:
        break;
    }
}

void ZModemProtocol::cancel()
{
    if (m_sshTransferProcess || m_zmodemProcess) {
        stopExternalTransfer(false, QStringLiteral("Transfer cancelled"));
        return;
    }

    if (m_state != State::Failed) {
        logZModemEvent("Transfer cancelled");
        setState(State::Failed);
    }
    m_transferActive = false;
    
    if (m_file) {
        m_file->close();
    }

    emit transferCompleted(false, "Transfer cancelled");
}
void ZModemProtocol::setState(State newState)
{
    if (m_state != newState) {
        logZModemEvent(QString("State change: %1 -> %2").arg(static_cast<int>(m_state)).arg(static_cast<int>(newState)));
        m_state = newState;
    }
}

void ZModemProtocol::sendData(const QByteArray &bytes)
{
    if (!m_connection)
        return;
    
    logZModemEvent(QString("Sending %1 bytes").arg(bytes.size()));
    m_connection->writeData(bytes);
}

void ZModemProtocol::readMoreFileData()
{
    if (m_sshTransferProcess || m_zmodemProcess)
        return;

    if (!m_file || !m_file->isOpen()) {
        logZModemEvent("File not open for reading");
        setState(State::SendingEof);
        processState();
        return;
    }
    
    QByteArray chunk = m_file->read(m_blockSize);
    if (chunk.isEmpty()) {
        logZModemEvent("End of file reached");
        setState(State::SendingEof);
        processState();
        return;
    }
    
    m_bytesSent += chunk.size();
    emit transferProgress(m_bytesSent, m_fileSize);
    
    logZModemEvent(QString("Sending data chunk: %1 bytes (total: %2/%3)").arg(chunk.size()).arg(m_bytesSent).arg(m_fileSize));
    
    // Send raw file data
    sendData(chunk);
    
    // Continue sending more data
    if (m_bytesSent < m_fileSize) {
        QTimer::singleShot(50, this, &ZModemProtocol::readMoreFileData);
    } else {
        setState(State::SendingEof);
        processState();
    }
}

void ZModemProtocol::sendFrame(uint8_t frameType, const QByteArray &data, uint32_t position)
{
    // Simplified - just send raw data for now
    sendData(data);
}

QByteArray ZModemProtocol::buildFileHeader(const QString &filename, qint64 fileSize)
{
    QByteArray header;
    
    // File size (4 bytes, little-endian)
    uint32_t size32 = static_cast<uint32_t>(std::min(fileSize, (qint64)0xFFFFFFFF));
    header.append(static_cast<char>(size32 & 0xFF));
    header.append(static_cast<char>((size32 >> 8) & 0xFF));
    header.append(static_cast<char>((size32 >> 16) & 0xFF));
    header.append(static_cast<char>((size32 >> 24) & 0xFF));
    
    // Filename
    QByteArray fname = filename.toUtf8();
    header.append(fname);
    header.append('\0');
    
    return header;
}

QByteArray ZModemProtocol::buildZDLEFrame(uint8_t frameType, const QByteArray &data, uint32_t position)
{
    QByteArray frame;
    frame.append(static_cast<char>(frameType));
    frame.append(data);
    return frame;
}

QByteArray ZModemProtocol::encodeZDLE(const QByteArray &data)
{
    // Simplified - just return data as-is
    return data;
}

uint16_t ZModemProtocol::crc16(const QByteArray &data)
{
    uint16_t crc = 0;
    
    for (uint8_t byte : data) {
        crc ^= (static_cast<uint16_t>(byte) << 8);
        for (int i = 0; i < 8; i++) {
            crc <<= 1;
            if (crc & 0x10000) {
                crc ^= 0x1021;
                crc &= 0xFFFF;
            }
        }
    }
    
    return crc;
}

uint32_t ZModemProtocol::crc32(const QByteArray &data)
{
    static const uint32_t poly = 0xEDB88320;
    uint32_t crc = 0;
    
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

bool ZModemProtocol::parseIncomingFrame()
{
    // Simplified - no frame parsing for now
    return false;
}

bool ZModemProtocol::processRINIT()
{
    return false;
}

bool ZModemProtocol::processFILE()
{
    return false;
}

bool ZModemProtocol::processDATA()
{
    return false;
}

bool ZModemProtocol::processEOF()
{
    return false;
}

bool ZModemProtocol::processACK()
{
    return false;
}
