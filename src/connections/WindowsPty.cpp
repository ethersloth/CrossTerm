#include "WindowsPty.h"

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace {

QString formatError(const char *what, DWORD code)
{
    return QStringLiteral("%1 failed (error %2)").arg(QLatin1String(what)).arg(code);
}

} // namespace

WindowsPty::WindowsPty(QObject *parent)
    : QObject(parent)
{
}

WindowsPty::~WindowsPty()
{
    stop();
}

bool WindowsPty::isRunning() const
{
    return m_pseudoConsole != nullptr;
}

bool WindowsPty::start(const QString &commandLine, int rows, int columns, QString *error)
{
    if (isRunning())
        return true;

    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;

    if (!CreatePipe(&inputRead, &inputWrite, &sa, 0))
        return fail(formatError("CreatePipe", GetLastError()));

    if (!CreatePipe(&outputRead, &outputWrite, &sa, 0)) {
        const DWORD err = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        return fail(formatError("CreatePipe", err));
    }

    // Only the pty side is inheritable.
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    HPCON pseudoConsole = nullptr;
    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    const HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &pseudoConsole);

    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    if (FAILED(hr)) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        return fail(QStringLiteral("CreatePseudoConsole failed (0x%1)")
                        .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<BYTE> attrBuffer(attrSize);
    auto *attrList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList;

    bool ok = InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize);
    if (ok) {
        ok = UpdateProcThreadAttribute(attrList,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       pseudoConsole,
                                       sizeof(pseudoConsole),
                                       nullptr,
                                       nullptr);
    }
    if (!ok) {
        const DWORD err = GetLastError();
        ClosePseudoConsole(pseudoConsole);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        return fail(formatError("ConPTY attribute setup", err));
    }

    // One named wstring: begin()/end() from separate temporaries would dangle.
    const std::wstring wideCommandLine = commandLine.toStdWString();
    std::vector<wchar_t> commandLineBuffer(wideCommandLine.begin(), wideCommandLine.end());
    commandLineBuffer.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr,
                                        commandLineBuffer.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr,
                                        nullptr,
                                        &si.StartupInfo,
                                        &pi);
    const DWORD startError = started ? ERROR_SUCCESS : GetLastError();

    DeleteProcThreadAttributeList(attrList);

    if (!started) {
        ClosePseudoConsole(pseudoConsole);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        return fail(formatError("CreateProcess", startError));
    }

    m_pseudoConsole = pseudoConsole;
    m_inputWrite = inputWrite;
    m_outputRead = outputRead;
    m_childProcess = pi.hProcess;
    m_childThread = pi.hThread;
    m_stopping = false;

    m_reader = std::thread([this] { readerLoop(); });
    m_waiter = std::thread([this] { waiterLoop(); });

    return true;
}

void WindowsPty::readerLoop()
{
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(static_cast<HANDLE>(m_outputRead), buffer, sizeof(buffer), &read, nullptr);
        if (!ok || read == 0)
            break;

        QByteArray chunk(buffer, static_cast<int>(read));
        QMetaObject::invokeMethod(
            this, [this, chunk] { emit dataReceived(chunk); }, Qt::QueuedConnection);
    }
}

void WindowsPty::waiterLoop()
{
    // ConPTY keeps the output pipe open after the child exits, so the reader never
    // sees EOF. Watching the process handle is the only reliable exit signal.
    WaitForSingleObject(static_cast<HANDLE>(m_childProcess), INFINITE);

    if (m_stopping)
        return;

    QMetaObject::invokeMethod(this, [this] { emit exited(); }, Qt::QueuedConnection);
}

void WindowsPty::stop()
{
    if (!m_pseudoConsole && !m_reader.joinable() && !m_waiter.joinable())
        return;

    m_stopping = true;

    if (m_childProcess)
        TerminateProcess(static_cast<HANDLE>(m_childProcess), 0);

    if (m_waiter.joinable())
        m_waiter.join();

    // Closing the pty drops the write end, which unblocks ReadFile in the reader.
    if (m_pseudoConsole) {
        ClosePseudoConsole(static_cast<HPCON>(m_pseudoConsole));
        m_pseudoConsole = nullptr;
    }

    if (m_reader.joinable())
        m_reader.join();

    for (void **handle : {&m_inputWrite, &m_outputRead, &m_childProcess, &m_childThread}) {
        if (*handle) {
            CloseHandle(static_cast<HANDLE>(*handle));
            *handle = nullptr;
        }
    }
}

void WindowsPty::write(const QByteArray &data)
{
    if (!isRunning() || data.isEmpty())
        return;

    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(m_inputWrite),
              data.constData(),
              static_cast<DWORD>(data.size()),
              &written,
              nullptr);
}

void WindowsPty::resize(int rows, int columns)
{
    if (!isRunning() || rows < 1 || columns < 1)
        return;

    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    ResizePseudoConsole(static_cast<HPCON>(m_pseudoConsole), size);
}

#endif // Q_OS_WIN
