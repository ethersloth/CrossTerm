#pragma once

#include "TerminalCell.h"
#include "TerminalCursor.h"

#include <QVector>

/**
 * Manages the terminal screen buffer.
 * Maintains a 2D grid of cells and handles scrollback.
 */
class TerminalScreen
{
public:
    static constexpr int DEFAULT_ROWS = 24;
    static constexpr int DEFAULT_COLUMNS = 80;

    explicit TerminalScreen(int rows = DEFAULT_ROWS, int columns = DEFAULT_COLUMNS);

    // Size management
    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    void resize(int rows, int columns);

    // Cell access
    const TerminalCell &cellAt(int row, int column) const;
    TerminalCell &cellAt(int row, int column);
    void setCellAt(int row, int column, const TerminalCell &cell);

    // Cursor management
    TerminalCursor &cursor() { return m_cursor; }
    const TerminalCursor &cursor() const { return m_cursor; }

    // Screen operations
    void clear();
    void clearLine();
    void clearToEndOfLine();
    void clearFromStartOfLine();
    void eraseCells(int row, int startCol, int endCol);
    void insertLine();
    void deleteLine();
    void insertCells(int count);
    void deleteCells(int count);

    // Scrolling
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    
    // Scrollback buffer
    int scrollbackSize() const { return m_scrollback.size(); }
    const QVector<TerminalCell> &scrollbackLine(int index) const;
    void addToScrollback(const QVector<TerminalCell> &line);
    void clearScrollback();

    // Text attributes (used by parser)
    TerminalCell &defaultCell() { return m_defaultCell; }
    const TerminalCell &defaultCell() const { return m_defaultCell; }

    // Alternate screen buffer
    void enableAltScreen();
    void disableAltScreen();
    bool isAltScreenEnabled() const { return m_altScreenEnabled; }

private:
    void ensureBounds();
    
    int m_rows;
    int m_columns;
    QVector<QVector<TerminalCell>> m_buffer;
    QVector<QVector<TerminalCell>> m_altBuffer;
    QVector<QVector<TerminalCell>> m_scrollback;
    TerminalCursor m_cursor;
    TerminalCell m_defaultCell;
    bool m_altScreenEnabled = false;
    
    static constexpr int MAX_SCROLLBACK = 10000;  // Maximum lines to keep in scrollback
};
