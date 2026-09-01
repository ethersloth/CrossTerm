#pragma once

#include <QDateTime>
#include <QWidget>
#include <QPointer>
#include <memory>

class IConnection;
class TerminalCell;
class TerminalScreen;
class TerminalParser;
class ZModemProtocol;

class TerminalWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void setConnection(IConnection *connection);
    IConnection *connection() const;
    void applySessionFont(const QFont &font);
    void setDownloadDirectory(const QString &directory) { m_downloadDirectory = directory; }
    void setScrollbackLimit(int lines);

signals:
    void reconnectRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private slots:
    void appendIncoming(const QByteArray &data);
    void showError(const QString &message);
    void updateCursorBlink();

private slots:
    void onZModemTransferCompleted(bool success, const QString &message);
    void onZModemTransferProgress(qint64 bytesTransferred, qint64 totalBytes);

private:
    void sendBytes(const QByteArray &data);
    void recalculateFontMetrics();
    void updateTerminalSize();
    void maybePromptForZModemTransfer(const QByteArray &data);
    void scheduleZModemDetection();
    void handleZModemTransferPrompt(const QString &command, bool uploadToRemote);
    const TerminalCell &displayCellAt(int row, int column) const;
    QPoint terminalPositionAt(const QPoint &pixelPosition) const;
    bool hasSelection() const;
    bool isCellSelected(int logicalRow, int column) const;
    QString selectedText() const;
    void copySelection();
    void pasteClipboard();

    bool m_zmodemPromptActive = false;
    bool m_zmodemDetectionPending = false;
    QString m_commandInputBuffer;
    QString m_lastSubmittedCommand;
    QString m_pendingZmodemDownloadPath;
    QString m_remoteZmodemPath;
    QString m_remoteWorkingDirectory;
    QString m_downloadDirectory;
    QByteArray m_promptDetectionBuffer;
    QString m_lastZmodemCommand;
    QDateTime m_lastZmodemPrompt;
    std::unique_ptr<ZModemProtocol> m_zmodem;

    QPointer<IConnection> m_connection;
    std::unique_ptr<TerminalScreen> m_screen;
    std::unique_ptr<TerminalParser> m_parser;
    
    int m_charWidth = 8;
    int m_charHeight = 16;
    int m_ascent = 12;
    int m_scrollOffset = 0;
    QPoint m_selectionStart{-1, -1};
    QPoint m_selectionEnd{-1, -1};
    bool m_selecting = false;
    bool m_cursorBlinkState = true;
    QTimer *m_cursorBlinkTimer = nullptr;
    QTimer *m_zmodemDetectionTimer = nullptr;
};
