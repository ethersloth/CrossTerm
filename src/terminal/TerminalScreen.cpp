#include "TerminalScreen.h"
#include <algorithm>

TerminalScreen::TerminalScreen(int rows, int columns)
    : m_rows(rows), m_columns(columns)
{
    m_buffer.resize(m_rows);
    for (auto &row : m_buffer) {
        row.resize(m_columns);
    }
    
    m_altBuffer.resize(m_rows);
    for (auto &row : m_altBuffer) {
        row.resize(m_columns);
    }
}

void TerminalScreen::resize(int rows, int columns)
{
    if (rows == m_rows && columns == m_columns)
        return;

    m_rows = rows;
    m_columns = columns;

    // Resize main buffer
    m_buffer.resize(m_rows);
    for (auto &row : m_buffer) {
        row.resize(m_columns);
    }

    // Resize alt buffer
    m_altBuffer.resize(m_rows);
    for (auto &row : m_altBuffer) {
        row.resize(m_columns);
    }

    ensureBounds();
}

const TerminalCell &TerminalScreen::cellAt(int row, int column) const
{
    static const TerminalCell empty;
    
    if (row < 0 || row >= m_rows || column < 0 || column >= m_columns)
        return empty;

    const auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    return buffer[row][column];
}

TerminalCell &TerminalScreen::cellAt(int row, int column)
{
    if (row < 0 || row >= m_rows || column < 0 || column >= m_columns) {
        static TerminalCell dummy;
        return dummy;
    }

    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    return buffer[row][column];
}

void TerminalScreen::setCellAt(int row, int column, const TerminalCell &cell)
{
    if (row >= 0 && row < m_rows && column >= 0 && column < m_columns) {
        auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
        buffer[row][column] = cell;
    }
}

void TerminalScreen::clear()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    for (auto &row : buffer) {
        for (auto &cell : row) {
            cell.reset();
        }
    }
    m_cursor.moveTo(0, 0);
}

void TerminalScreen::clearLine()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        for (auto &cell : buffer[m_cursor.row]) {
            cell.reset();
        }
    }
}

void TerminalScreen::clearToEndOfLine()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        for (int col = m_cursor.column; col < m_columns; ++col) {
            buffer[m_cursor.row][col].reset();
        }
    }
}

void TerminalScreen::clearFromStartOfLine()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        for (int col = 0; col <= m_cursor.column; ++col) {
            buffer[m_cursor.row][col].reset();
        }
    }
}

void TerminalScreen::eraseCells(int row, int startCol, int endCol)
{
    if (row < 0 || row >= m_rows)
        return;

    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    for (int col = startCol; col <= endCol && col < m_columns; ++col) {
        buffer[row][col].reset();
    }
}

void TerminalScreen::insertLine()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        QVector<TerminalCell> newLine(m_columns);
        buffer.insert(m_cursor.row, newLine);
        if (buffer.size() > m_rows) {
            buffer.removeLast();
        }
    }
}

void TerminalScreen::deleteLine()
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        buffer.removeAt(m_cursor.row);
        QVector<TerminalCell> newLine(m_columns);
        buffer.append(newLine);
    }
}

void TerminalScreen::insertCells(int count)
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        auto &row = buffer[m_cursor.row];
        for (int i = 0; i < count && m_cursor.column < m_columns; ++i) {
            row.insert(m_cursor.column, TerminalCell());
            row.removeLast();
        }
    }
}

void TerminalScreen::deleteCells(int count)
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    if (m_cursor.row >= 0 && m_cursor.row < m_rows) {
        auto &row = buffer[m_cursor.row];
        for (int i = 0; i < count && m_cursor.column < m_columns; ++i) {
            row.removeAt(m_cursor.column);
            row.append(TerminalCell());
        }
    }
}

void TerminalScreen::scrollUp(int lines)
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    for (int i = 0; i < lines; ++i) {
        if (!buffer.isEmpty()) {
            addToScrollback(buffer.first());
            buffer.removeFirst();
            QVector<TerminalCell> newLine(m_columns);
            buffer.append(newLine);
        }
    }
}

void TerminalScreen::scrollDown(int lines)
{
    auto &buffer = m_altScreenEnabled ? m_altBuffer : m_buffer;
    for (int i = 0; i < lines; ++i) {
        if (!buffer.isEmpty()) {
            QVector<TerminalCell> newLine(m_columns);
            buffer.prepend(newLine);
            buffer.removeLast();
        }
    }
}

const QVector<TerminalCell> &TerminalScreen::scrollbackLine(int index) const
{
    static const QVector<TerminalCell> empty;
    if (index < 0 || index >= m_scrollback.size())
        return empty;
    return m_scrollback[index];
}

void TerminalScreen::addToScrollback(const QVector<TerminalCell> &line)
{
    m_scrollback.append(line);
    if (m_scrollback.size() > MAX_SCROLLBACK) {
        m_scrollback.removeFirst();
    }
}

void TerminalScreen::clearScrollback()
{
    m_scrollback.clear();
}

void TerminalScreen::enableAltScreen()
{
    m_altScreenEnabled = true;
}

void TerminalScreen::disableAltScreen()
{
    m_altScreenEnabled = false;
}

void TerminalScreen::ensureBounds()
{
    m_cursor.row = std::clamp(m_cursor.row, 0, m_rows - 1);
    m_cursor.column = std::clamp(m_cursor.column, 0, m_columns - 1);
}
