#include "TerminalParser.h"
#include <QStringList>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>
#include <algorithm>

namespace {

bool traceEnabled()
{
    return qEnvironmentVariableIsSet("CROSSTERM_TRACE");
}

QFile &traceFile()
{
    static QFile file(qEnvironmentVariable("CROSSTERM_TRACE_FILE", "/tmp/crossterm-trace.log"));
    static bool opened = false;
    if (!opened) {
        const bool ok = file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        Q_UNUSED(ok);
        opened = true;
    }
    return file;
}

QString printableBytes(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        if (b == '\n') {
            out += "\\n";
        } else if (b == '\r') {
            out += "\\r";
        } else if (b == '\t') {
            out += "\\t";
        } else if (b == 0x1b) {
            out += "\\e";
        } else if (b >= 0x20 && b < 0x7f) {
            out += QChar(b);
        } else {
            out += QString("\\x%1").arg(static_cast<int>(b), 2, 16, QLatin1Char('0'));
        }
    }
    return out;
}

void traceLog(const QString &line)
{
    if (!traceEnabled())
        return;

    QFile &file = traceFile();
    if (!file.isOpen())
        return;

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " " << line << "\n";
    stream.flush();
}

}

TerminalParser::TerminalParser(TerminalScreen &screen)
    : m_screen(screen)
{
}

void TerminalParser::processBytes(const QByteArray &bytes)
{
    traceLog(QString("IN bytes=%1 data=\"%2\"")
             .arg(bytes.size())
             .arg(printableBytes(bytes)));

    for (uint8_t byte : bytes) {
        switch (m_state) {
        case State::Normal:
            if (byte == 0x1b) {  // ESC
                m_state = State::Escape;
                m_buffer.clear();
            } else {
                processNormalChar(byte);
            }
            break;

        case State::Escape:
            processEscapeChar(byte);
            break;

        case State::CSI:
            processCsiChar(byte);
            break;

        case State::OSC:
            processOscChar(byte);
            break;

        case State::SOS:
            // Just skip until we find ST (0x1b 0x5c)
            m_buffer.append(byte);
            if (m_buffer.size() >= 2 && 
                m_buffer[m_buffer.size() - 2] == 0x1b && 
                m_buffer[m_buffer.size() - 1] == 0x5c) {
                m_state = State::Normal;
                m_buffer.clear();
            }
            break;
        }
    }
}

void TerminalParser::processNormalChar(uint8_t byte)
{
    auto &cursor = m_screen.cursor();

    const auto clearWrapPending = [this]() {
        m_wrapPending = false;
    };

    switch (byte) {
    case 0x07:  // BEL
        handleBell();
        break;

    case 0x08:  // BS (Backspace)
        clearWrapPending();
        if (cursor.column > 0)
            cursor.moveLeft();
        break;

    case 0x09:  // HT (Horizontal Tab)
        clearWrapPending();
        cursor.moveToColumn(((cursor.column / 8) + 1) * 8);
        if (cursor.column >= m_screen.columns())
            cursor.moveToColumn(m_screen.columns() - 1);
        break;

    case 0x0a:  // LF (Line Feed)
    case 0x0b:  // VT (Vertical Tab)
    case 0x0c:  // FF (Form Feed)
        clearWrapPending();
        cursor.moveDown();
        if (cursor.row >= m_screen.rows()) {
            m_screen.scrollUp();
            cursor.moveToRow(m_screen.rows() - 1);
        }
        // Most shells/programs write '\n' expecting a visual new line start.
        cursor.moveToColumn(0);
        break;

    case 0x0d:  // CR (Carriage Return)
        clearWrapPending();
        cursor.moveToColumn(0);
        break;

    case 0x0e:  // SO (Shift Out)
    case 0x0f:  // SI (Shift In)
        clearWrapPending();
        // Graphics mode switching - not implementing for now
        break;

    default:
        processUtf8Byte(byte);
        break;
    }
}

void TerminalParser::processUtf8Byte(uint8_t byte)
{
    if (m_utf8ExpectedContinuation == 0) {
        if (byte < 0x80) {
            if (byte >= 0x20)
                writePrintableChar(QChar(byte));
            return;
        }

        if (byte >= 0xC2 && byte <= 0xDF) {
            m_utf8Pending.clear();
            m_utf8Pending.append(static_cast<char>(byte));
            m_utf8ExpectedContinuation = 1;
            return;
        }

        if (byte >= 0xE0 && byte <= 0xEF) {
            m_utf8Pending.clear();
            m_utf8Pending.append(static_cast<char>(byte));
            m_utf8ExpectedContinuation = 2;
            return;
        }

        if (byte >= 0xF0 && byte <= 0xF4) {
            m_utf8Pending.clear();
            m_utf8Pending.append(static_cast<char>(byte));
            m_utf8ExpectedContinuation = 3;
            return;
        }

        writePrintableChar(QChar(0xFFFD));
        return;
    }

    if ((byte & 0xC0) != 0x80) {
        // Invalid continuation: flush replacement and retry this byte as a new sequence.
        writePrintableChar(QChar(0xFFFD));
        m_utf8Pending.clear();
        m_utf8ExpectedContinuation = 0;
        processUtf8Byte(byte);
        return;
    }

    m_utf8Pending.append(static_cast<char>(byte));
    --m_utf8ExpectedContinuation;

    if (m_utf8ExpectedContinuation == 0) {
        const QString decoded = QString::fromUtf8(m_utf8Pending);
        if (!decoded.isEmpty())
            writePrintableChar(decoded.at(0));
        else
            writePrintableChar(QChar(0xFFFD));

        m_utf8Pending.clear();
    }
}

void TerminalParser::writePrintableChar(QChar ch)
{
    auto &cursor = m_screen.cursor();
    if (m_autoWrap && m_wrapPending) {
        cursor.moveToColumn(0);
        cursor.moveDown();
        if (cursor.row >= m_screen.rows()) {
            m_screen.scrollUp();
            cursor.moveToRow(m_screen.rows() - 1);
        }
        m_wrapPending = false;
    }

    TerminalCell outCell = m_screen.defaultCell();
    outCell.character = ch;
    m_screen.setCellAt(cursor.row, cursor.column, outCell);

    if (cursor.column == m_screen.columns() - 1) {
        if (!m_autoWrap) {
            return;
        }
        // xterm-style deferred wrap: wrap only when next printable char arrives.
        m_wrapPending = true;
        return;
    }

    cursor.moveRight();
}

void TerminalParser::processEscapeChar(uint8_t byte)
{
    m_wrapPending = false;

    switch (byte) {
    case '[':  // ESC [ - Control Sequence Introducer
        m_state = State::CSI;
        m_csiParams.clear();
        break;

    case ']':  // ESC ] - Operating System Command
        m_state = State::OSC;
        m_buffer.clear();
        break;

    case 'X':  // ESC X - Start of String
        m_state = State::SOS;
        m_buffer.clear();
        break;

    case 'M':  // ESC M - Reverse Index
        m_screen.cursor().moveUp();
        if (m_screen.cursor().row < 0) {
            m_screen.cursor().moveToRow(0);
            m_screen.scrollDown();
        }
        m_state = State::Normal;
        break;

    case 'D':  // ESC D - Index (move down)
        m_screen.cursor().moveDown();
        if (m_screen.cursor().row >= m_screen.rows()) {
            m_screen.cursor().moveToRow(m_screen.rows() - 1);
            m_screen.scrollUp();
        }
        m_state = State::Normal;
        break;

    case 'E':  // ESC E - Next Line
        m_screen.cursor().moveDown();
        m_screen.cursor().moveToColumn(0);
        if (m_screen.cursor().row >= m_screen.rows()) {
            m_screen.cursor().moveToRow(m_screen.rows() - 1);
            m_screen.scrollUp();
        }
        m_state = State::Normal;
        break;

    case '7':  // ESC 7 - Save Cursor Position
        m_savedCursor = m_screen.cursor();
        m_state = State::Normal;
        break;

    case '8':  // ESC 8 - Restore Cursor Position
        m_screen.cursor() = m_savedCursor;
        m_state = State::Normal;
        break;

    case 'c':  // ESC c - Reset Terminal
        m_screen.clear();
        m_screen.cursor().reset();
        m_state = State::Normal;
        break;

    default:
        traceLog(QString("ESC unknown='\\x%1'")
                 .arg(static_cast<int>(byte), 2, 16, QLatin1Char('0')));
        m_state = State::Normal;
        break;
    }
}

void TerminalParser::processCsiChar(uint8_t byte)
{
    // Accumulate parameter bytes and command byte
    if (byte >= '0' && byte <= '9') {
        m_csiParams.append(byte);
    } else if (byte == ';') {
        m_csiParams.append(';');
    } else if (byte == '?' || byte == '>') {
        m_csiParams.append(byte);
    } else if ((byte >= '@' && byte <= '~') || (byte >= 0x40 && byte <= 0x7e)) {
        // Command character
        handleCsiCommand(m_csiParams, byte);
        m_state = State::Normal;
        m_csiParams.clear();
    }
}

void TerminalParser::processOscChar(uint8_t byte)
{
    // Operating System Command - just accumulate until BEL or ST
    m_buffer.append(byte);

    if (byte == 0x07) {  // BEL
        m_state = State::Normal;
        m_buffer.clear();
    } else if (m_buffer.size() >= 2 &&
               m_buffer[m_buffer.size() - 2] == 0x1b &&
               m_buffer[m_buffer.size() - 1] == 0x5c) {  // ST
        m_state = State::Normal;
        m_buffer.clear();
    }
}

void TerminalParser::handleCsiCommand(const QByteArray &params, uint8_t command)
{
    m_wrapPending = false;
    const int oldRow = m_screen.cursor().row;
    const int oldCol = m_screen.cursor().column;

    // Parse parameters
    QStringList paramList;
    QString current;
    for (char c : params) {
        if (c == ';') {
            paramList << current;
            current.clear();
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty() || params.endsWith(';')) {
        paramList << current;
    }

    switch (command) {
    case 'A':  // Cursor Up
        handleCursorUp(parseNumber(params));
        break;

    case 'B':  // Cursor Down
        handleCursorDown(parseNumber(params));
        break;

    case 'C':  // Cursor Forward
        handleCursorForward(parseNumber(params));
        break;

    case 'a':  // HPR - Horizontal Position Relative
        handleCursorForward(parseNumber(params));
        break;

    case 'D':  // Cursor Backward
        handleCursorBackward(parseNumber(params));
        break;

    case 'E':  // Cursor Next Line (CNL)
        handleCursorNextLine(parseNumber(params));
        break;

    case 'F':  // Cursor Previous Line (CPL)
        handleCursorPreviousLine(parseNumber(params));
        break;

    case 'H':  // Cursor Position
    case 'f': {  // Also 'f' for HVPA
        int row = 1, col = 1;
        if (!paramList.isEmpty() && !paramList[0].isEmpty())
            row = parseNumber(paramList[0]);
        if (paramList.size() > 1 && !paramList[1].isEmpty())
            col = parseNumber(paramList[1]);
        handleCursorPosition(row - 1, col - 1);  // Convert to 0-based
        break;
    }

    case 'G': {  // Cursor Horizontal Absolute (CHA)
        int col = 1;
        if (!paramList.isEmpty() && !paramList[0].isEmpty())
            col = parseNumber(paramList[0]);
        m_screen.cursor().moveToColumn(std::clamp(col - 1, 0, m_screen.columns() - 1));
        break;
    }

    case 'd': {  // Vertical Position Absolute (VPA)
        int row = 1;
        if (!paramList.isEmpty() && !paramList[0].isEmpty())
            row = parseNumber(paramList[0]);
        m_screen.cursor().moveToRow(std::clamp(row - 1, 0, m_screen.rows() - 1));
        break;
    }

    case 'J':  // Erase in Display
        handleEraseDisplay(parseNumber(params, 0));
        break;

    case 'K':  // Erase in Line
        handleEraseLine(parseNumber(params, 0));
        break;

    case 'm':  // Select Graphic Rendition
        handleSelectGraphicRendition(params);
        break;

    case 's':  // Save Cursor Position
        m_savedCursor = m_screen.cursor();
        break;

    case 'u':  // Restore Cursor Position
        m_screen.cursor() = m_savedCursor;
        break;

    case 'h':  // Set Mode
        handleSetMode(params);
        break;

    case 'l':  // Reset Mode
        handleResetMode(params);
        break;

    case 'L':  // Insert Lines
        handleInsertLines(parseNumber(params));
        break;

    case 'M':  // Delete Lines
        handleDeleteLines(parseNumber(params));
        break;

    case '@':  // Insert Characters
        handleInsertChars(parseNumber(params));
        break;

    case 'P':  // Delete Characters
        handleDeleteChars(parseNumber(params));
        break;

    case 'X': {  // Erase Characters (ECH)
        int count = parseNumber(params);
        if (count <= 0)
            count = 1;
        int row = m_screen.cursor().row;
        int start = m_screen.cursor().column;
        int end = std::min(start + count - 1, m_screen.columns() - 1);
        m_screen.eraseCells(row, start, end);
        break;
    }

    default:
        // Unknown command - ignore
        break;
    }

    traceLog(QString("CSI cmd='%1' params='%2' cursor %3,%4 -> %5,%6")
             .arg(QChar(command))
             .arg(QString::fromLatin1(params))
             .arg(oldRow)
             .arg(oldCol)
             .arg(m_screen.cursor().row)
             .arg(m_screen.cursor().column));
}

void TerminalParser::handleCursorUp(int count)
{
    m_screen.cursor().moveUp(count);
    if (m_screen.cursor().row < 0)
        m_screen.cursor().moveToRow(0);
}

void TerminalParser::handleCursorDown(int count)
{
    m_screen.cursor().moveDown(count);
    if (m_screen.cursor().row >= m_screen.rows())
        m_screen.cursor().moveToRow(m_screen.rows() - 1);
}

void TerminalParser::handleCursorForward(int count)
{
    m_screen.cursor().moveRight(count);
    if (m_screen.cursor().column >= m_screen.columns())
        m_screen.cursor().moveToColumn(m_screen.columns() - 1);
}

void TerminalParser::handleCursorBackward(int count)
{
    m_screen.cursor().moveLeft(count);
    if (m_screen.cursor().column < 0)
        m_screen.cursor().moveToColumn(0);
}

void TerminalParser::handleCursorNextLine(int count)
{
    if (count <= 0)
        count = 1;

    m_screen.cursor().moveDown(count);
    if (m_screen.cursor().row >= m_screen.rows())
        m_screen.cursor().moveToRow(m_screen.rows() - 1);
    m_screen.cursor().moveToColumn(0);
}

void TerminalParser::handleCursorPreviousLine(int count)
{
    if (count <= 0)
        count = 1;

    m_screen.cursor().moveUp(count);
    if (m_screen.cursor().row < 0)
        m_screen.cursor().moveToRow(0);
    m_screen.cursor().moveToColumn(0);
}

void TerminalParser::handleCursorPosition(int row, int column)
{
    row = std::clamp(row, 0, m_screen.rows() - 1);
    column = std::clamp(column, 0, m_screen.columns() - 1);
    m_screen.cursor().moveTo(row, column);
}

void TerminalParser::handleEraseDisplay(int mode)
{
    auto &cursor = m_screen.cursor();
    traceLog(QString("ED mode=%1 at %2,%3")
             .arg(mode)
             .arg(cursor.row)
             .arg(cursor.column));

    switch (mode) {
    case 0:  // Erase from cursor to end of display
        m_screen.clearToEndOfLine();
        for (int r = cursor.row + 1; r < m_screen.rows(); ++r) {
            for (int c = 0; c < m_screen.columns(); ++c) {
                m_screen.cellAt(r, c).reset();
            }
        }
        break;

    case 1:  // Erase from start of display to cursor
        for (int r = 0; r < cursor.row; ++r) {
            for (int c = 0; c < m_screen.columns(); ++c) {
                m_screen.cellAt(r, c).reset();
            }
        }
        m_screen.clearFromStartOfLine();
        break;

    case 2:  // Erase entire display
        for (int r = 0; r < m_screen.rows(); ++r) {
            for (int c = 0; c < m_screen.columns(); ++c) {
                m_screen.cellAt(r, c).reset();
            }
        }
        break;

    case 3:  // Erase scrollback only (xterm behavior)
        m_screen.clearScrollback();
        break;

    default:
        break;
    }
}

void TerminalParser::handleEraseLine(int mode)
{
    auto &cursor = m_screen.cursor();

    switch (mode) {
    case 0:  // Erase from cursor to end of line
        m_screen.clearToEndOfLine();
        break;

    case 1:  // Erase from start of line to cursor
        m_screen.clearFromStartOfLine();
        break;

    case 2:  // Erase entire line
        m_screen.clearLine();
        break;

    default:
        break;
    }
}

void TerminalParser::handleSelectGraphicRendition(const QByteArray &params)
{
    auto &defaultCell = m_screen.defaultCell();
    
    if (params.isEmpty()) {
        // No parameters - reset to default
        defaultCell.reset();
        return;
    }

    // Parse parameters separated by semicolons
    QStringList parts;
    QString current;
    for (char c : params) {
        if (c == ';') {
            parts << current;
            current.clear();
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty())
        parts << current;

    for (const auto &part : parts) {
        if (part.isEmpty())
            continue;

        int code = part.toInt();

        switch (code) {
        case 0:  // Reset all attributes
            defaultCell.reset();
            break;

        case 1:  // Bold
            defaultCell.addAttribute(TerminalCell::Attribute::Bold);
            break;

        case 2:  // Dim
            defaultCell.addAttribute(TerminalCell::Attribute::Dim);
            break;

        case 3:  // Italic
            defaultCell.addAttribute(TerminalCell::Attribute::Italic);
            break;

        case 4:  // Underline
            defaultCell.addAttribute(TerminalCell::Attribute::Underline);
            break;

        case 5:  // Blink
            defaultCell.addAttribute(TerminalCell::Attribute::Blink);
            break;

        case 7:  // Reverse video
            defaultCell.addAttribute(TerminalCell::Attribute::Reverse);
            break;

        case 8:  // Concealed/Hidden
            defaultCell.addAttribute(TerminalCell::Attribute::Hidden);
            break;

        case 9:  // Strikethrough
            defaultCell.addAttribute(TerminalCell::Attribute::StrikeThrough);
            break;

        case 22:  // Normal intensity (not bold or dim)
            defaultCell.removeAttribute(TerminalCell::Attribute::Bold);
            defaultCell.removeAttribute(TerminalCell::Attribute::Dim);
            break;

        case 23:  // Not italic
            defaultCell.removeAttribute(TerminalCell::Attribute::Italic);
            break;

        case 24:  // Not underlined
            defaultCell.removeAttribute(TerminalCell::Attribute::Underline);
            break;

        case 25:  // Not blinking
            defaultCell.removeAttribute(TerminalCell::Attribute::Blink);
            break;

        case 27:  // Not reverse
            defaultCell.removeAttribute(TerminalCell::Attribute::Reverse);
            break;

        case 28:  // Not concealed
            defaultCell.removeAttribute(TerminalCell::Attribute::Hidden);
            break;

        case 29:  // Not strikethrough
            defaultCell.removeAttribute(TerminalCell::Attribute::StrikeThrough);
            break;

        // Foreground colors
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            defaultCell.foreground = code - 30;
            break;

        case 90: case 91: case 92: case 93:  // Bright foreground
        case 94: case 95: case 96: case 97:
            defaultCell.foreground = (code - 90) + 8;
            break;

        // Background colors
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            defaultCell.background = code - 40;
            break;

        case 100: case 101: case 102: case 103:  // Bright background
        case 104: case 105: case 106: case 107:
            defaultCell.background = (code - 100) + 8;
            break;

        case 39:  // Default foreground
            defaultCell.foreground = 7;
            break;

        case 49:  // Default background
            defaultCell.background = 0;
            break;

        default:
            break;
        }
    }
}

void TerminalParser::handleSaveCursorPosition()
{
    m_savedCursor = m_screen.cursor();
}

void TerminalParser::handleRestoreCursorPosition()
{
    m_screen.cursor() = m_savedCursor;
}

void TerminalParser::handleSetMode(const QByteArray &params)
{
    if (params.startsWith('?')) {
        // Private mode
        int code = parseNumber(params.mid(1));
        if (code == 1) {  // DECCKM - application cursor keys
            m_applicationCursorMode = true;
        }
        if (code == 7) {  // DECAWM - auto-wrap
            m_autoWrap = true;
        }
        if (code == 1047 || code == 1049) {  // Alt screen
            handleAltScreenMode(true);
        }
    } else {
        int code = parseNumber(params, 0);
        if (code == 20) {  // LNM - line feed/new line mode
            m_lineFeed = true;
        }
    }
}

void TerminalParser::handleResetMode(const QByteArray &params)
{
    if (params.startsWith('?')) {
        // Private mode
        int code = parseNumber(params.mid(1));
        if (code == 1) {  // DECCKM - application cursor keys
            m_applicationCursorMode = false;
        }
        if (code == 7) {  // DECAWM - auto-wrap
            m_autoWrap = false;
            m_wrapPending = false;
        }
        if (code == 1047 || code == 1049) {  // Alt screen
            handleAltScreenMode(false);
        }
    } else {
        int code = parseNumber(params, 0);
        if (code == 20) {  // LNM - line feed/new line mode
            m_lineFeed = false;
        }
    }
}

void TerminalParser::handleInsertLines(int count)
{
    for (int i = 0; i < count; ++i) {
        m_screen.insertLine();
    }
}

void TerminalParser::handleDeleteLines(int count)
{
    for (int i = 0; i < count; ++i) {
        m_screen.deleteLine();
    }
}

void TerminalParser::handleInsertChars(int count)
{
    m_screen.insertCells(count);
}

void TerminalParser::handleDeleteChars(int count)
{
    m_screen.deleteCells(count);
}

void TerminalParser::handleAltScreenMode(bool enable)
{
    if (enable)
        m_screen.enableAltScreen();
    else
        m_screen.disableAltScreen();
}

int TerminalParser::parseNumber(const QByteArray &str, int defaultValue)
{
    if (str.isEmpty())
        return defaultValue;
    bool ok;
    int value = str.toInt(&ok);
    return ok ? value : defaultValue;
}

int TerminalParser::parseNumber(const QString &str, int defaultValue)
{
    if (str.isEmpty())
        return defaultValue;
    bool ok;
    int value = str.toInt(&ok);
    return ok ? value : defaultValue;
}

void TerminalParser::handleBell()
{
    // TODO: Implement bell sound
}
