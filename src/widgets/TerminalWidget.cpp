#include "TerminalWidget.h"
#include "../connections/IConnection.h"
#include "../terminal/TerminalScreen.h"
#include "../terminal/TerminalParser.h"
#include "../terminal/TerminalCell.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setStyleSheet("background-color: black; color: white;");
    
    // Create terminal engine
    m_screen = std::make_unique<TerminalScreen>(24, 80);
    m_parser = std::make_unique<TerminalParser>(*m_screen);

    // Set up cursor blink timer
    m_cursorBlinkTimer = new QTimer(this);
    connect(m_cursorBlinkTimer, &QTimer::timeout, this, &TerminalWidget::updateCursorBlink);
    m_cursorBlinkTimer->start(500);  // Blink every 500ms

    // Calculate font metrics
    recalculateFontMetrics();
    updateTerminalSize();
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::setConnection(IConnection *connection)
{
    if (m_connection) {
        disconnect(m_connection, nullptr, this, nullptr);
    }

    m_connection = connection;
    if (!m_connection)
        return;

    connect(m_connection, &IConnection::dataReceived,
            this, &TerminalWidget::appendIncoming);
    connect(m_connection, &IConnection::errorOccurred,
            this, &TerminalWidget::showError);

    if (m_screen) {
        m_connection->setTerminalSize(m_screen->rows(), m_screen->columns());
    }
}

IConnection *TerminalWidget::connection() const
{
    return m_connection;
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    
    // Fill background
    painter.fillRect(rect(), Qt::black);

    if (!m_screen)
        return;

    const auto &cursor = m_screen->cursor();
    
    // Draw each cell
    for (int row = 0; row < m_screen->rows(); ++row) {
        int y = row * m_charHeight + m_ascent;

        for (int col = 0; col < m_screen->columns(); ++col) {
            int x = col * m_charWidth;
            const auto &cell = m_screen->cellAt(row, col);

            // Determine colors
            QColor fgColor = Qt::white;
            QColor bgColor = Qt::black;

            // Simple color mapping for now (0-7 standard ANSI colors)
            static const QColor ansiColors[] = {
                Qt::black,    // 0
                Qt::darkRed,  // 1
                Qt::darkGreen, // 2
                Qt::yellow,   // 3 (dark yellow)
                Qt::darkBlue, // 4
                Qt::darkMagenta, // 5
                Qt::darkCyan, // 6
                Qt::white,    // 7
            };

            if (cell.foreground < 8)
                fgColor = ansiColors[cell.foreground];
            if (cell.background < 8)
                bgColor = ansiColors[cell.background];

            // Handle reverse video
            if (cell.hasAttribute(TerminalCell::Attribute::Reverse)) {
                std::swap(fgColor, bgColor);
            }

            // Draw background
            if (bgColor != Qt::black)
                painter.fillRect(x, row * m_charHeight, m_charWidth, m_charHeight, bgColor);

            // Draw cursor
            if (row == cursor.row && col == cursor.column && m_cursorBlinkState) {
                painter.fillRect(x, row * m_charHeight, m_charWidth, m_charHeight, 
                                bgColor == Qt::black ? Qt::white : Qt::black);
            }

            // Draw character
            if (cell.character != ' ') {
                painter.setPen(fgColor);
                if (cell.hasAttribute(TerminalCell::Attribute::Bold))
                    painter.setFont(QFont(font().family(), font().pointSize(), QFont::Bold));
                else
                    painter.setFont(font());

                painter.drawText(x, y, QString(cell.character));
            }
        }
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTerminalSize();
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_connection || !m_connection->isConnected()) {
        const bool isCtrlC = event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_C;
        const bool isEnter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_R) {
            emit reconnectRequested();
            event->accept();
            return;
        }
        if (isCtrlC || isEnter) {
            emit reconnectRequested();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    QByteArray bytes;

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        bytes = "\r";
        break;
    case Qt::Key_Backspace:
        bytes = "\x7f";
        break;
    case Qt::Key_Tab:
        bytes = "\t";
        break;
    case Qt::Key_Escape:
        bytes = "\x1b";
        break;
    case Qt::Key_Up:
        bytes = "\x1b[A";  // VT100 cursor up
        break;
    case Qt::Key_Down:
        bytes = "\x1b[B";  // VT100 cursor down
        break;
    case Qt::Key_Right:
        bytes = "\x1b[C";  // VT100 cursor right
        break;
    case Qt::Key_Left:
        bytes = "\x1b[D";  // VT100 cursor left
        break;
    case Qt::Key_Home:
        bytes = "\x1b[H";  // VT100 home
        break;
    case Qt::Key_End:
        bytes = "\x1b[F";  // VT100 end
        break;
    case Qt::Key_PageUp:
        bytes = "\x1b[5~";  // VT100 page up
        break;
    case Qt::Key_PageDown:
        bytes = "\x1b[6~";  // VT100 page down
        break;
    default:
        if (!event->text().isEmpty())
            bytes = event->text().toUtf8();
        break;
    }

    if (!bytes.isEmpty()) {
        sendBytes(bytes);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    m_cursorBlinkTimer->start();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    m_cursorBlinkTimer->stop();
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    // Keep keyboard navigation inside the terminal; Tab should be sent to the shell.
    return false;
}

void TerminalWidget::appendIncoming(const QByteArray &data)
{
    if (!m_parser)
        return;

    m_parser->processBytes(data);
    update();  // Trigger repaint
}

void TerminalWidget::showError(const QString &message)
{
    QByteArray errorMsg = QStringLiteral("\n[CrossTerm error] %1\n").arg(message).toUtf8();
    m_parser->processBytes(errorMsg);
    update();
}

void TerminalWidget::updateCursorBlink()
{
    m_cursorBlinkState = !m_cursorBlinkState;
    update();
}

void TerminalWidget::sendBytes(const QByteArray &data)
{
    if (m_connection)
        m_connection->writeData(data);
}

void TerminalWidget::recalculateFontMetrics()
{
    QFontMetrics metrics(font());
    m_charWidth = metrics.horizontalAdvance(QChar('M'));  // Use 'M' as reference
    m_charHeight = metrics.height();
    m_ascent = metrics.ascent();
}

void TerminalWidget::updateTerminalSize()
{
    if (!m_screen)
        return;

    int newCols = width() / m_charWidth;
    int newRows = height() / m_charHeight;

    if (newCols < 1) newCols = 1;
    if (newRows < 1) newRows = 1;

    if (newCols != m_screen->columns() || newRows != m_screen->rows()) {
        m_screen->resize(newRows, newCols);
        if (m_connection) {
            m_connection->setTerminalSize(newRows, newCols);
        }
    }
}
