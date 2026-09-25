#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QToolButton;
class TerminalWidget;

// Row above a terminal: one button per saved command for the session, a "+"
// to save a new one, then a free-form command box with a Run button. Command
// buttons and Run are enabled only while connected.
class CommandBar final : public QWidget
{
    Q_OBJECT
public:
    using CommandList = QList<QPair<QString, QString>>;

    explicit CommandBar(TerminalWidget *terminal, QWidget *parent = nullptr);

    void setConnected(bool connected);
    // Rebuilds the buttons from the terminal's current saved commands.
    void reloadCommands();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void commandSent(const QString &label);
    // Emitted after the user adds, edits, or removes a saved command here;
    // the terminal already holds the new list.
    void commandsEdited(const CommandBar::CommandList &commands);

private:
    void runTypedCommand();
    void updateOverflow();
    void addCommand();
    void editCommand(int index);
    void removeCommand(int index);
    void applyCommands(const CommandList &commands);

    TerminalWidget *m_terminal = nullptr;
    QWidget *m_buttonRow = nullptr;
    QHBoxLayout *m_buttonLayout = nullptr;
    QList<QPushButton *> m_commandButtons;
    QToolButton *m_overflowButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    bool m_connected = false;
};
