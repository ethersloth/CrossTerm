#pragma once

#include <QWidget>
#include <QPointer>
#include <memory>

class IConnection;
class TerminalScreen;
class TerminalParser;

class TerminalWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void setConnection(IConnection *connection);
    IConnection *connection() const;

signals:
    void reconnectRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private slots:
    void appendIncoming(const QByteArray &data);
    void showError(const QString &message);
    void updateCursorBlink();

private:
    void sendBytes(const QByteArray &data);
    void recalculateFontMetrics();
    void updateTerminalSize();

    QPointer<IConnection> m_connection;
    std::unique_ptr<TerminalScreen> m_screen;
    std::unique_ptr<TerminalParser> m_parser;
    
    int m_charWidth = 8;
    int m_charHeight = 16;
    int m_ascent = 12;
    bool m_cursorBlinkState = true;
    QTimer *m_cursorBlinkTimer = nullptr;
};
