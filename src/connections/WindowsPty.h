#pragma once

#include <QObject>

#ifdef Q_OS_WIN

#include <atomic>
#include <thread>

// Runs a child process under a Windows pseudo console (ConPTY) so it sees a real
// terminal: console APIs work, and full VT is emitted back.
class WindowsPty final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsPty(QObject *parent = nullptr);
    ~WindowsPty() override;

    bool isRunning() const;

    // commandLine must already be quoted the way CreateProcessW expects.
    bool start(const QString &commandLine, int rows, int columns, QString *error);
    void stop();
    void write(const QByteArray &data);
    void resize(int rows, int columns);

signals:
    void dataReceived(const QByteArray &data);
    void exited();

private:
    void readerLoop();
    void waiterLoop();

    // HPCON/HANDLE are void*; kept untyped so windows.h stays out of this header.
    void *m_pseudoConsole = nullptr;
    void *m_inputWrite = nullptr;
    void *m_outputRead = nullptr;
    void *m_childProcess = nullptr;
    void *m_childThread = nullptr;
    std::thread m_reader;
    std::thread m_waiter;
    std::atomic<bool> m_stopping{false};
};

#endif // Q_OS_WIN
