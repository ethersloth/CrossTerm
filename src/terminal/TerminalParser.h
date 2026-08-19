#pragma once

#include "TerminalScreen.h"

#include <QByteArray>

/**
 * Parses VT100/xterm escape sequences and updates terminal screen accordingly.
 * Handles ANSI sequences, cursor movement, colors, and text attributes.
 */
class TerminalParser
{
public:
    explicit TerminalParser(TerminalScreen &screen);

    /**
     * Feed bytes to the parser. Updates the screen as sequences are recognized.
     */
    void processBytes(const QByteArray &bytes);

    /**
     * Get any unprocessed partial sequence (for debugging/logging).
     */
    QByteArray partialSequence() const { return m_buffer; }

private:
    enum class State {
        Normal,           // Regular text input
        Escape,           // After ESC character
        CSI,              // After ESC [ (Control Sequence Introducer)
        OSC,              // After ESC ] (Operating System Command)
        SOS,              // After ESC X (Start of String)
    };

    void processNormalChar(uint8_t byte);
    void processUtf8Byte(uint8_t byte);
    void writePrintableChar(QChar ch);
    void processEscapeChar(uint8_t byte);
    void processCsiChar(uint8_t byte);
    void processOscChar(uint8_t byte);
    
    // CSI command handlers
    void handleCsiCommand(const QByteArray &params, uint8_t command);
    void handleCursorUp(int count);
    void handleCursorDown(int count);
    void handleCursorForward(int count);
    void handleCursorBackward(int count);
    void handleCursorNextLine(int count);
    void handleCursorPreviousLine(int count);
    void handleCursorPosition(int row, int column);
    void handleEraseDisplay(int mode);
    void handleEraseLine(int mode);
    void handleSelectGraphicRendition(const QByteArray &params);
    void handleSaveCursorPosition();
    void handleRestoreCursorPosition();
    void handleSetMode(const QByteArray &params);
    void handleResetMode(const QByteArray &params);
    void handleInsertLines(int count);
    void handleDeleteLines(int count);
    void handleInsertChars(int count);
    void handleDeleteChars(int count);
    void handleAltScreenMode(bool enable);

    // Helper functions
    int parseNumber(const QByteArray &str, int defaultValue = 1);
    int parseNumber(const QString &str, int defaultValue = 1);
    void parseCSIParameters();
    void handleBell();

    TerminalScreen &m_screen;
    State m_state = State::Normal;
    QByteArray m_buffer;
    QByteArray m_csiParams;
    QByteArray m_utf8Pending;
    int m_utf8ExpectedContinuation = 0;
    bool m_wrapPending = false;
    TerminalCursor m_savedCursor;
    bool m_autoWrap = true;
    bool m_lineFeed = false;
    bool m_applicationCursorMode = false;
};
