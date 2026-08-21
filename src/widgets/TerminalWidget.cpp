#include "TerminalWidget.h"
#include "../connections/IConnection.h"
#include "../terminal/TerminalScreen.h"
#include "../terminal/TerminalParser.h"
#include "../terminal/TerminalCell.h"
#include "../transfer/ZModemProtocol.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMessageBox>
#include <QStandardPaths>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTextStream>
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
    
    // Create ZModem protocol handler
    m_zmodem = std::make_unique<ZModemProtocol>(this);
    connect(m_zmodem.get(), &ZModemProtocol::transferCompleted,
            this, &TerminalWidget::onZModemTransferCompleted);
    connect(m_zmodem.get(), &ZModemProtocol::transferProgress,
            this, &TerminalWidget::onZModemTransferProgress);

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

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_lastSubmittedCommand = m_commandInputBuffer;
        const QRegularExpression downloadCommandPattern(
            QStringLiteral("^\\s*(?:sz|sb)\\s+([^\\s]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch downloadCommandMatch =
            downloadCommandPattern.match(m_lastSubmittedCommand);
        m_pendingZmodemDownloadPath = downloadCommandMatch.hasMatch()
            ? downloadCommandMatch.captured(1).trimmed()
            : QString();
        m_commandInputBuffer.clear();
    } else if (event->key() == Qt::Key_Backspace) {
        if (!m_commandInputBuffer.isEmpty())
            m_commandInputBuffer.chop(1);
    } else if (!event->text().isEmpty() && event->text().at(0).unicode() >= 0x20) {
        m_commandInputBuffer.append(event->text());
    }

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

    m_promptDetectionBuffer.append(data);
    if (m_promptDetectionBuffer.size() > 4096)
        m_promptDetectionBuffer.remove(0, m_promptDetectionBuffer.size() - 4096);

    QString incomingText = QString::fromUtf8(m_promptDetectionBuffer);
    const QRegularExpression ansiPattern(QStringLiteral("\\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    incomingText.remove(ansiPattern);
    const QRegularExpression promptDirectoryPattern(
        QStringLiteral("\\[~(/[^\\]\\r\\n]*)\\]"));
    const QRegularExpressionMatch promptDirectoryMatch = promptDirectoryPattern.match(incomingText);
    if (promptDirectoryMatch.hasMatch())
        m_remoteWorkingDirectory = QStringLiteral("$HOME") + promptDirectoryMatch.captured(1);

    if (!m_zmodem || !m_zmodem->isTransferInProgress())
        maybePromptForZModemTransfer(data);
    m_parser->processBytes(data);
    update();  // Trigger repaint
}

static void logZModemEvent(const QString &message)
{
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/crossterm_zmodem.log";
    QFile logFile(logPath);
    logFile.open(QIODevice::Append | QIODevice::Text);
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << message << "\n";
    out.flush();
    logFile.close();
}

void TerminalWidget::maybePromptForZModemTransfer(const QByteArray &data)
{
    auto sanitizeCommandLine = [](const QString &raw) {
        QString cleaned = raw;
        // Remove CSI escape sequences (for example: ESC [ 5 C from arrow keys).
        cleaned.remove(QRegularExpression(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]")));
        cleaned.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")));
        return cleaned.trimmed();
    };
    auto findDownloadPathOnScreen = [this]() {
        if (!m_screen)
            return QString();

        const QRegularExpression commandPattern(
            QStringLiteral("(?:^|\\s)(?:sz|sb)\\s+([^\\s]+)"),
            QRegularExpression::CaseInsensitiveOption);
        QString path;
        for (int row = 0; row < m_screen->rows(); ++row) {
            QString line;
            for (int column = 0; column < m_screen->columns(); ++column)
                line.append(m_screen->cellAt(row, column).character);

            const QRegularExpressionMatchIterator matches = commandPattern.globalMatch(line);
            auto iterator = matches;
            while (iterator.hasNext())
                path = iterator.next().captured(1).trimmed();
        }
        return path;
    };

    if (m_zmodemPromptActive || data.isEmpty())
        return;

    logZModemEvent(QString("Received %1 bytes: %2").arg(data.size()).arg(QString(data.left(100).toHex())));

    const QByteArray rawMarkers[] = {
        QByteArray::fromHex("1818"),
        QByteArray::fromHex("1A45"),
        QByteArray::fromHex("1A2A")
    };

    bool hasRawZmodemSequence = false;
    for (const auto &marker : rawMarkers) {
        if (data.contains(marker)) {
            logZModemEvent(QString("Found raw ZModem marker: %1").arg(QString(marker.toHex())));
            hasRawZmodemSequence = true;
            break;
        }
    }

    QString command;
    bool foundCommand = false;

    const QString text = QString::fromUtf8(data);
    
    if (hasRawZmodemSequence) {
        // If we see raw ZModem markers, infer from context; default to rz (upload to remote)
        command = QStringLiteral("rz");
        foundCommand = true;
    } else {
        const QString submittedCommand = sanitizeCommandLine(m_lastSubmittedCommand).toLower();
        if (submittedCommand.isEmpty())
            return;

        const QRegularExpression commandPattern(
            QStringLiteral("\\b(rz|sz|rb|sb)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = commandPattern.match(submittedCommand);
        if (match.hasMatch()) {
            command = match.captured(1).toLower();
            logZModemEvent(QString("Found ZModem command: %1").arg(command));
            foundCommand = true;
        } else {
            logZModemEvent(QString("No command found. Text: %1").arg(text.left(100)));
        }
    }

    if (!foundCommand)
        return;

    const QString submittedLine = sanitizeCommandLine(m_lastSubmittedCommand);
    const bool uploadToRemote = command == QStringLiteral("rz") || command == QStringLiteral("rb");

    if (!uploadToRemote) {
        const QRegularExpression remotePathPattern(
            QStringLiteral("^\\s*(?:sz|sb)\\s+([^\\s]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch remotePathMatch = remotePathPattern.match(submittedLine);
        m_remoteZmodemPath = !m_pendingZmodemDownloadPath.isEmpty()
            ? m_pendingZmodemDownloadPath
            : (remotePathMatch.hasMatch() ? remotePathMatch.captured(1).trimmed() : QString());
        if (m_remoteZmodemPath.isEmpty())
            m_remoteZmodemPath = findDownloadPathOnScreen();
        m_remoteZmodemPath.remove(QLatin1Char('['));
        m_remoteZmodemPath.remove(QLatin1Char(']'));
        m_remoteZmodemPath.remove(QLatin1Char('\''));
        m_remoteZmodemPath.remove(QLatin1Char('"'));
        if (m_remoteZmodemPath.contains(QLatin1Char(':')) || m_remoteZmodemPath.contains(QLatin1Char('\\')))
            m_remoteZmodemPath = QFileInfo(m_remoteZmodemPath).fileName();
        logZModemEvent(QString("Captured remote ZModem path: %1").arg(m_remoteZmodemPath));
    } else {
        m_remoteZmodemPath.clear();
    }
    m_lastSubmittedCommand.clear();
    m_pendingZmodemDownloadPath.clear();

    if (!m_lastZmodemCommand.isEmpty() && m_lastZmodemCommand == command && m_lastZmodemPrompt.isValid()) {
        const int elapsed = m_lastZmodemPrompt.msecsTo(QDateTime::currentDateTime());
        if (elapsed < 2000) {
            logZModemEvent(QString("Suppressing duplicate ZModem command (elapsed %1ms)").arg(elapsed));
            return;
        }
    }

    m_lastZmodemCommand = command;
    m_lastZmodemPrompt = QDateTime::currentDateTime();

    const QString title = uploadToRemote ? QStringLiteral("ZModem upload requested")
                                        : QStringLiteral("ZModem download requested");
    const QString message = uploadToRemote ? QStringLiteral("The remote host is asking to receive a file from this PC. Do you want to pick a local file for upload?")
                                          : QStringLiteral("The remote host is asking to send a file to this PC. Do you want to choose a local destination?");
    const bool useConfiguredDownloadFolder = !uploadToRemote && !m_downloadDirectory.trimmed().isEmpty();

    if (!useConfiguredDownloadFolder) {
        logZModemEvent(QString("SHOWING POPUP for %1").arg(command));

        // Create message box with window flags to keep it on top
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(title);
        msgBox.setText(message);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
        msgBox.setWindowModality(Qt::ApplicationModal);

        if (msgBox.exec() != QMessageBox::Yes) {
            logZModemEvent(QString("User declined popup"));
            return;
        }

        logZModemEvent(QString("User accepted popup"));
    } else {
        logZModemEvent(QString("Starting configured ZModem download without a destination prompt"));
    }

    m_zmodemPromptActive = true;
    handleZModemTransferPrompt(command, uploadToRemote);
    m_zmodemPromptActive = false;
}

void TerminalWidget::handleZModemTransferPrompt(const QString &command, bool uploadToRemote)
{
    Q_UNUSED(command);

    const QString title = uploadToRemote ? QStringLiteral("Choose file to upload")
                                        : QStringLiteral("Choose download destination");
    QString filename;

    if (uploadToRemote) {
        filename = QFileDialog::getOpenFileName(this, title, QDir::homePath(), QStringLiteral("All Files (*)"));
    } else if (!m_downloadDirectory.trimmed().isEmpty()) {
        QString remoteFilename = QFileInfo(m_remoteZmodemPath).fileName();
        if (remoteFilename.isEmpty())
            remoteFilename = QStringLiteral("download");
        filename = QDir(m_downloadDirectory.trimmed()).filePath(remoteFilename);
        logZModemEvent(QString("Using configured download destination: %1").arg(filename));
    } else {
        const QString startPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        filename = QFileDialog::getSaveFileName(this, title, startPath, QStringLiteral("All Files (*)"));
    }

    if (filename.isEmpty())
        return;

    if (!m_zmodem || !m_connection) {
        QMessageBox::warning(this, "Error", "ZModem handler or connection not available");
        return;
    }

    logZModemEvent(QString("Starting %1 for file: %2").arg(uploadToRemote ? "upload" : "download").arg(filename));

    // Start the ZModem transfer
    if (uploadToRemote) {
        m_zmodem->sendFile(m_connection, filename, m_remoteWorkingDirectory);
    } else {
        m_zmodem->receiveFile(m_connection, filename, m_remoteZmodemPath, m_remoteWorkingDirectory);
    }
}

void TerminalWidget::onZModemTransferCompleted(bool success, const QString &message)
{
    if (success) {
        logZModemEvent(QString("Transfer completed: %1").arg(message));
        QMessageBox::information(this, "Transfer Complete", message);
    } else {
        logZModemEvent(QString("Transfer failed: %1").arg(message));
        QMessageBox::warning(this, "Transfer Failed", message);
    }
}

void TerminalWidget::onZModemTransferProgress(qint64 bytesTransferred, qint64 totalBytes)
{
    if (totalBytes > 0) {
        int percent = static_cast<int>((bytesTransferred * 100) / totalBytes);
        logZModemEvent(QString("Progress: %1 / %2 bytes (%3%)").arg(bytesTransferred).arg(totalBytes).arg(percent));
    }
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

void TerminalWidget::applySessionFont(const QFont &font)
{
    setFont(font);
    recalculateFontMetrics();
    update();
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
