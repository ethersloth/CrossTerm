#include "CommandBar.h"
#include "TerminalWidget.h"
#include "../ui/Theme.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>

#include <algorithm>

namespace {
struct CommandIcon {
    Theme::IconKind kind;
    QColor color;
};

// Picks an icon from the command's name first, then its text, so common admin
// commands read at a glance. Anything unrecognized gets a plain prompt icon.
CommandIcon iconForCommand(const QString &name, const QString &command)
{
    static const struct {
        QRegularExpression pattern;
        CommandIcon icon;
    } rules[] = {
        {QRegularExpression(QStringLiteral(R"(\b(reboot|shutdown|poweroff|restart host|update)\b)")),
         {Theme::IconKind::Restart, QColor(0xef, 0x44, 0x44)}},
        {QRegularExpression(QStringLiteral(R"(\b(restart|service|systemctl|daemon)\b)")),
         {Theme::IconKind::Gear, QColor(0xf5, 0x9e, 0x0b)}},
        {QRegularExpression(QStringLiteral(R"(\b(disk|df|du|lsblk|storage|mount|zpool)\b)")),
         {Theme::IconKind::Disk, QColor(0x3b, 0x82, 0xf6)}},
        {QRegularExpression(QStringLiteral(R"(\b(process(es)?|top|htop|btop|ps|load)\b)")),
         {Theme::IconKind::Activity, QColor(0xd9, 0x46, 0xef)}},
        {QRegularExpression(QStringLiteral(R"(\b(network|net|ip|ifconfig|ping|netstat|ss|nmcli|route)\b)")),
         {Theme::IconKind::Network, QColor(0x22, 0xc5, 0x5e)}},
        {QRegularExpression(QStringLiteral(R"(\b(info|uname|hostnamectl|version|uptime|status|sysinfo)\b)")),
         {Theme::IconKind::Info, QColor(0x3b, 0x82, 0xf6)}},
    };

    for (const QString &text : {name.toLower(), command.toLower()}) {
        for (const auto &rule : rules) {
            if (rule.pattern.match(text).hasMatch())
                return rule.icon;
        }
    }
    return {Theme::IconKind::Terminal, QColor()};
}

// Name/command editor used for both adding and editing. Returns false on cancel.
bool promptForCommand(QWidget *parent, const QString &title, QString &name, QString &command)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(460);

    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(name, &dialog);
    nameEdit->setPlaceholderText(QStringLiteral("e.g. Disk usage"));
    auto *commandEdit = new QLineEdit(command, &dialog);
    commandEdit->setPlaceholderText(QStringLiteral("e.g. df -h"));
    form->addRow(QStringLiteral("Name:"), nameEdit);
    form->addRow(QStringLiteral("Command:"), commandEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const auto validate = [=] {
        buttons->button(QDialogButtonBox::Save)->setEnabled(!nameEdit->text().trimmed().isEmpty()
                                                            && !commandEdit->text().trimmed().isEmpty());
    };
    QObject::connect(nameEdit, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(commandEdit, &QLineEdit::textChanged, &dialog, validate);
    validate();
    (name.isEmpty() ? nameEdit : commandEdit)->setFocus();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    name = nameEdit->text().trimmed();
    command = commandEdit->text().trimmed();
    return true;
}
}

CommandBar::CommandBar(TerminalWidget *terminal, QWidget *parent)
    : QWidget(parent), m_terminal(terminal)
{
    setObjectName(QStringLiteral("CommandBar"));
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    // Ignored horizontal policy: the row takes whatever width is left and
    // updateOverflow() decides how many buttons fit in it.
    m_buttonRow = new QWidget(this);
    m_buttonRow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_buttonRow->installEventFilter(this);
    m_buttonLayout = new QHBoxLayout(m_buttonRow);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(8);

    m_overflowButton = new QToolButton(m_buttonRow);
    m_overflowButton->setObjectName(QStringLiteral("CommandOverflow"));
    m_overflowButton->setText(QStringLiteral("More"));
    m_overflowButton->setToolTip(QStringLiteral("More saved commands"));
    m_overflowButton->setPopupMode(QToolButton::InstantPopup);
    m_overflowButton->setMenu(new QMenu(m_overflowButton));
    m_buttonLayout->addWidget(m_overflowButton);

    m_addButton = new QToolButton(m_buttonRow);
    m_addButton->setObjectName(QStringLiteral("CommandAddButton"));
    m_addButton->setIcon(Theme::icon(Theme::IconKind::Plus));
    m_addButton->setIconSize(QSize(16, 16));
    m_addButton->setToolTip(QStringLiteral("Save a command to this session"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    connect(m_addButton, &QToolButton::clicked, this, &CommandBar::addCommand);
    m_buttonLayout->addWidget(m_addButton);
    m_buttonLayout->addStretch(1);
    layout->addWidget(m_buttonRow, 1);

    auto *separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("CommandSeparator"));
    separator->setFixedSize(1, 28);
    layout->addWidget(separator);

    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setPlaceholderText(QStringLiteral("Enter command..."));
    m_commandEdit->setClearButtonEnabled(true);
    m_commandEdit->setMinimumWidth(240);
    m_commandEdit->setMaximumWidth(380);
    layout->addWidget(m_commandEdit);

    m_runButton = new QPushButton(QStringLiteral("Run"), this);
    m_runButton->setObjectName(QStringLiteral("RunButton"));
    m_runButton->setIcon(Theme::icon(Theme::IconKind::Play, Theme::ColorRole::AccentText));
    m_runButton->setIconSize(QSize(16, 16));
    m_runButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_runButton);

    connect(m_commandEdit, &QLineEdit::returnPressed, this, &CommandBar::runTypedCommand);
    connect(m_runButton, &QPushButton::clicked, this, &CommandBar::runTypedCommand);

    reloadCommands();
    setConnected(false);
}

void CommandBar::reloadCommands()
{
    qDeleteAll(m_commandButtons);
    m_commandButtons.clear();

    const auto commands = m_terminal->savedCommands();
    for (int i = 0; i < commands.size(); ++i) {
        const auto &[name, command] = commands.at(i);
        auto *button = new QPushButton(name, m_buttonRow);
        button->setObjectName(QStringLiteral("CommandButton"));
        const CommandIcon icon = iconForCommand(name, command);
        button->setIcon(icon.color.isValid() ? Theme::icon(icon.kind, icon.color)
                                             : Theme::icon(icon.kind));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(command);
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(m_connected);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QPushButton::clicked, this, [this, name, command] {
            if (m_terminal->runCommand(command))
                emit commandSent(name);
        });
        connect(button, &QWidget::customContextMenuRequested, this, [this, button, i](const QPoint &pos) {
            QMenu menu(this);
            QAction *edit = menu.addAction(QStringLiteral("Edit..."));
            QAction *remove = menu.addAction(QStringLiteral("Remove"));
            QAction *chosen = menu.exec(button->mapToGlobal(pos));
            if (chosen == edit)
                editCommand(i);
            else if (chosen == remove)
                removeCommand(i);
        });
        m_buttonLayout->insertWidget(i, button);
        m_commandButtons.append(button);
    }
    updateOverflow();
}

void CommandBar::setConnected(bool connected)
{
    m_connected = connected;
    for (auto *button : std::as_const(m_commandButtons))
        button->setEnabled(connected);
    m_runButton->setEnabled(connected);
}

bool CommandBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_buttonRow && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        updateOverflow();
    return QWidget::eventFilter(watched, event);
}

void CommandBar::updateOverflow()
{
    const int spacing = m_buttonLayout->spacing();
    // The "+" button always stays visible.
    const int available = m_buttonRow->width() - m_addButton->sizeHint().width() - spacing;

    int total = 0;
    for (auto *button : std::as_const(m_commandButtons))
        total += button->sizeHint().width() + spacing;

    const bool overflowing = total - spacing > available;
    const int budget = overflowing ? available - m_overflowButton->sizeHint().width() - spacing : available;

    QMenu *menu = m_overflowButton->menu();
    menu->clear();
    int used = 0;
    bool fits = true;
    for (auto *button : std::as_const(m_commandButtons)) {
        used += button->sizeHint().width();
        fits = fits && used <= budget;
        used += spacing;
        button->setVisible(fits);
        if (!fits) {
            QAction *action = menu->addAction(button->icon(), button->text());
            action->setToolTip(button->toolTip());
            action->setEnabled(m_connected);
            connect(action, &QAction::triggered, button, &QPushButton::click);
        }
    }
    m_overflowButton->setVisible(overflowing);
}

void CommandBar::addCommand()
{
    QString name;
    const QString typed = m_commandEdit->text().trimmed();
    QString command = typed;
    if (!promptForCommand(this, QStringLiteral("Save Command"), name, command))
        return;
    if (!typed.isEmpty() && command == typed)
        m_commandEdit->clear();

    CommandList commands = m_terminal->savedCommands();
    // Same name replaces the existing entry, matching Session Properties.
    auto existing = std::find_if(commands.begin(), commands.end(),
                                 [&name](const auto &entry) { return entry.first == name; });
    if (existing != commands.end())
        existing->second = command;
    else
        commands.append({name, command});
    applyCommands(commands);
}

void CommandBar::editCommand(int index)
{
    CommandList commands = m_terminal->savedCommands();
    if (index < 0 || index >= commands.size())
        return;

    QString name = commands.at(index).first;
    QString command = commands.at(index).second;
    if (!promptForCommand(this, QStringLiteral("Edit Command"), name, command))
        return;

    commands[index] = {name, command};
    for (int i = commands.size() - 1; i >= 0; --i) {
        if (i != index && commands.at(i).first == name)
            commands.removeAt(i);
    }
    applyCommands(commands);
}

void CommandBar::removeCommand(int index)
{
    CommandList commands = m_terminal->savedCommands();
    if (index < 0 || index >= commands.size())
        return;

    const auto answer = QMessageBox::question(this,
                                              QStringLiteral("Remove Command"),
                                              QStringLiteral("Remove the saved command \"%1\"?")
                                                  .arg(commands.at(index).first));
    if (answer != QMessageBox::Yes)
        return;

    commands.removeAt(index);
    applyCommands(commands);
}

void CommandBar::applyCommands(const CommandList &commands)
{
    m_terminal->setSavedCommands(commands);
    // Rebuild after the triggering button's handler has returned; that
    // button is deleted by the rebuild.
    QMetaObject::invokeMethod(this, [this] { reloadCommands(); }, Qt::QueuedConnection);
    emit commandsEdited(commands);
}

void CommandBar::runTypedCommand()
{
    const QString command = m_commandEdit->text().trimmed();
    if (command.isEmpty() || !m_terminal->runCommand(command))
        return;
    m_commandEdit->clear();
    emit commandSent(command);
}
