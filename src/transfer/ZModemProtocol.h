#pragma once

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QProcess>
#include <cstdint>

class IConnection;

/**
 * ZModem binary file transfer protocol implementation.
 * Handles both sending (upload) and receiving (download) file transfers.
 */
class ZModemProtocol : public QObject
{
    Q_OBJECT
public:
    explicit ZModemProtocol(QObject *parent = nullptr);
    ~ZModemProtocol() override;

    // Start sending a file (upload to remote)
    void sendFile(IConnection *connection, const QString &filepath, const QString &remoteWorkingDirectory = QString());
    
    // Start receiving a file (download from remote)
    void receiveFile(IConnection *connection, const QString &destPath, const QString &remotePath = QString(), const QString &remoteWorkingDirectory = QString());
    
    // Process incoming data from the connection
    void processIncomingData(const QByteArray &data);
    
    // Cancel the current transfer
    void cancel();
    
    bool isTransferInProgress() const { return m_transferActive; }

signals:
    void transferStarted(const QString &filename);
    void transferProgress(qint64 bytesTransferred, qint64 totalBytes);
    void transferCompleted(bool success, const QString &message);
    void statusUpdated(const QString &status);

private:
    enum class State {
        Idle,
        SendingInitiation,
        SendingFileHeader,
        SendingFileData,
        SendingEof,
        ReceivingInitiation,
        ReceivingFileHeader,
        ReceivingFileData,
        ReceivingEof,
        Completed,
        Failed
    };

    // ZModem frame types
    enum FrameType : uint8_t {
        ZRQINIT = 0,   // Request to send init
        ZRINIT = 1,    // Receive init
        ZSENDFILE = 2, // Send file
        ZFILE = 3,     // File header
        ZSKIP = 4,     // Skip file
        ZEOF = 5,      // End of file
        ZERROR = 6,    // Error
        ZCANCEL = 7,   // Cancel
        ZFIN = 8,      // Finish
        ZDATA = 0x0A,  // Data subpacket
        ZACK = 0x0B,   // Acknowledge
        ZSEQNO = 0x0C, // Sequence number
        ZCOMMAND = 0x0D, // Command
        ZSTDERR = 0x0E  // Stderr
    };

    struct FileHeader {
        uint32_t fileSize;
        uint32_t modificationTime;
        uint32_t fileMode;
        uint32_t serialNumber;
        char filename[256];
    };

    // Frame building and transmission
    QByteArray buildZDLEFrame(uint8_t frameType, const QByteArray &data, uint32_t position = 0);
    QByteArray buildFileHeader(const QString &filename, qint64 fileSize);
    QByteArray encodeZDLE(const QByteArray &data);
    
    // Frame processing
    bool parseIncomingFrame();
    bool processRINIT();
    bool processFILE();
    bool processDATA();
    bool processEOF();
    bool processACK();

    // CRC calculation
    static uint16_t crc16(const QByteArray &data);
    static uint32_t crc32(const QByteArray &data);

    // Sending utilities
    void sendFrame(uint8_t frameType, const QByteArray &data, uint32_t position = 0);
    void sendData(const QByteArray &bytes);
    void readMoreFileData();
    void startExternalTransfer(IConnection *connection, const QString &path, bool upload, const QString &remotePath = QString(), const QString &remoteWorkingDirectory = QString());
    void stopExternalTransfer(bool success, const QString &message);

    // State machine
    void setState(State newState);
    void processState();

    IConnection *m_connection = nullptr;
    State m_state = State::Idle;
    bool m_transferActive = false;
    
    // File handling
    std::unique_ptr<QFile> m_file;
    QString m_filename;
    qint64 m_fileSize = 0;
    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
    
    // Protocol state
    QByteArray m_incomingBuffer;
    uint32_t m_lastPosition = 0;
    int m_blockSize = 4096;  // Default block size for data frames
    bool m_isUpload = false;  // true for send, false for receive

    std::unique_ptr<QProcess> m_sshTransferProcess;
    std::unique_ptr<QProcess> m_zmodemProcess;
    bool m_zmodemProcessFinished = false;
    bool m_sshTransferProcessFinished = false;
    int m_zmodemExitCode = -1;
    int m_sshTransferExitCode = -1;
    
    // Protocol parameters
    uint8_t m_rxFlags = 0;
    uint32_t m_rxWindow = 0;
};
