#include "MainWindow.h"
#include "connections/LocalShellConnection.h"
#include "connections/SshConnection.h"
#include "widgets/TerminalWidget.h"
#include "profiles/ProfileManager.h"
#include "profiles/ConnectionProfile.h"
#include "dialogs/NewSessionDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {
constexpr int RoleKind = Qt::UserRole;
constexpr int RoleProfileName = Qt::UserRole + 1;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_profileManager(new ProfileManager())
{
    setWindowTitle(QStringLiteral("CrossTerm 0.4.0"));
    resize(1200, 760);

    // Load saved profiles
    m_profileManager->loadProfiles();

    buildUi();
    buildMenus();
    populateSessions();

    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::buildUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested,
            this, &MainWindow::closeTab);

    auto *dock = new QDockWidget(QStringLiteral("Sessions"), this);
    dock->setObjectName(QStringLiteral("SessionsDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_sessionTree = new QTreeWidget(dock);
    m_sessionTree->setHeaderHidden(true);
        m_sessionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    dock->setWidget(m_sessionTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(m_sessionTree, &QTreeWidget::itemDoubleClicked,
            this, &MainWindow::openSelectedSession);
        connect(m_sessionTree, &QWidget::customContextMenuRequested,
            this, &MainWindow::onSessionContextMenu);
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto *newSession = fileMenu->addAction(QStringLiteral("New &Session..."));
    newSession->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(newSession, &QAction::triggered, this, &MainWindow::onNewSession);

    auto *newLocal = fileMenu->addAction(QStringLiteral("New &Local Shell"));
    newLocal->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    connect(newLocal, &QAction::triggered, this, &MainWindow::onNewLocalShell);

    auto *close = fileMenu->addAction(QStringLiteral("&Close Tab"));
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    fileMenu->addSeparator();
    auto *quit = fileMenu->addAction(QStringLiteral("E&xit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto *sessionMenu = menuBar()->addMenu(QStringLiteral("&Session"));
    sessionMenu->addAction(newSession);
    sessionMenu->addAction(newLocal);

    auto *globalOptionsMenu = menuBar()->addMenu(QStringLiteral("&Global Options"));
    auto *globalSettings = globalOptionsMenu->addAction(QStringLiteral("Settings..."));
    connect(globalSettings, &QAction::triggered, this, &MainWindow::onGlobalOptions);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto *about = helpMenu->addAction(QStringLiteral("&About CrossTerm"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this,
                           QStringLiteral("About CrossTerm"),
                           QStringLiteral("CrossTerm 0.3.0\n\n"
                                          "A cross-platform terminal and connection manager built with C++20 and Qt 6.\n\n"
                                          "Features:\n"
                                          "• VT100 Terminal Emulation\n"
                                          "• Saved Connection Profiles\n"
                                          "• Local Shell Support\n"
                                          "• SSH (coming in 0.4)\n"
                                          "• Serial (coming in 0.5)\n"
                                          "• Telnet (coming in 0.6)"));
    });
}

void MainWindow::populateSessions()
{
    m_sessionTree->clear();

    auto *localGroup = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Local")});
    auto *localShell = new QTreeWidgetItem(localGroup, {QStringLiteral("Local Shell")});
    localShell->setData(0, RoleKind, QStringLiteral("local-shell"));

    auto *servers = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Servers")});
    auto *serial = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Serial")});
    auto *telnet = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Telnet")});

    const auto profiles = m_profileManager->allProfiles();
    for (const auto &profile : profiles) {
        QTreeWidgetItem *parentItem = nullptr;
        switch (profile.type()) {
        case ConnectionProfile::ConnectionType::LocalShell:
            parentItem = localGroup;
            break;
        case ConnectionProfile::ConnectionType::SSH:
            parentItem = servers;
            break;
        case ConnectionProfile::ConnectionType::Serial:
            parentItem = serial;
            break;
        case ConnectionProfile::ConnectionType::Telnet:
            parentItem = telnet;
            break;
        }

        if (!parentItem)
            continue;

        auto *item = new QTreeWidgetItem(parentItem, {profile.name()});
        item->setData(0, RoleKind, QStringLiteral("saved-profile"));
        item->setData(0, RoleProfileName, profile.name());
    }

    m_sessionTree->expandAll();
}

void MainWindow::openSelectedSession(QTreeWidgetItem *item, int)
{
    if (!item)
        return;

    const QString kind = item->data(0, RoleKind).toString();
    if (kind == QStringLiteral("local-shell")) {
        onNewLocalShell();
        return;
    }

    if (kind == QStringLiteral("saved-profile")) {
        const QString name = item->data(0, RoleProfileName).toString();
        if (m_profileManager->hasProfile(name)) {
            openProfileSession(m_profileManager->profile(name));
        }
    }
}

void MainWindow::onSessionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sessionTree->itemAt(pos);
    if (!item)
        return;

    const QString kind = item->data(0, RoleKind).toString();
    if (kind != QStringLiteral("saved-profile"))
        return;

    const QString profileName = item->data(0, RoleProfileName).toString();
    if (profileName.isEmpty() || !m_profileManager->hasProfile(profileName))
        return;

    QMenu menu(this);
    QAction *openAction = menu.addAction(QStringLiteral("Open Session"));
    QAction *propertiesAction = menu.addAction(QStringLiteral("Properties..."));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete Session..."));

    QAction *chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == openAction) {
        openProfileSession(m_profileManager->profile(profileName));
        return;
    }

    if (chosen == propertiesAction) {
        NewSessionDialog dialog(*m_profileManager, this);
        dialog.setWindowTitle(QStringLiteral("Session Properties"));
        if (!dialog.selectProfile(profileName)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Session Properties"),
                                 QStringLiteral("Unable to load profile '%1'.").arg(profileName));
            return;
        }

        if (dialog.exec() != QDialog::Accepted)
            return;

        const ConnectionProfile updated = dialog.createProfile();
        if (updated.name().isEmpty())
            return;

        if (updated.name() != profileName) {
            m_profileManager->removeProfile(profileName);
        }
        m_profileManager->updateProfile(updated);
        if (!m_profileManager->saveProfiles()) {
            QMessageBox::critical(this,
                                  QStringLiteral("Session Properties"),
                                  QStringLiteral("Failed to save updated profile."));
            return;
        }

        populateSessions();
        return;
    }

    if (chosen == deleteAction) {
        const QMessageBox::StandardButton confirmation = QMessageBox::question(
            this,
            QStringLiteral("Delete Session"),
            QStringLiteral("Delete the saved session '%1'?\n\nThis cannot be undone.").arg(profileName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (confirmation != QMessageBox::Yes)
            return;

        m_profileManager->removeProfile(profileName);
        if (!m_profileManager->saveProfiles()) {
            QMessageBox::critical(this,
                                  QStringLiteral("Delete Session"),
                                  QStringLiteral("Failed to save the updated session list."));
            return;
        }

        populateSessions();
        statusBar()->showMessage(QStringLiteral("Deleted session: %1").arg(profileName), 2500);
    }
}

void MainWindow::onGlobalOptions()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Global Options"));
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();

    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    const QString defaultLogDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");

    auto *enableLoggingByDefault = new QCheckBox(QStringLiteral("Enable logging by default for new sessions"), &dialog);
    enableLoggingByDefault->setChecked(settings.value(QStringLiteral("global/loggingEnabledByDefault"), false).toBool());
    form->addRow(enableLoggingByDefault);

    auto *scrollbackLines = new QSpinBox(&dialog);
    scrollbackLines->setRange(100, 100000);
    scrollbackLines->setSingleStep(1000);
    scrollbackLines->setValue(settings.value(QStringLiteral("global/scrollbackLines"), 10000).toInt());
    form->addRow(QStringLiteral("Default scrollback lines:"), scrollbackLines);

    auto *logPathRow = new QWidget(&dialog);
    auto *logPathLayout = new QHBoxLayout(logPathRow);
    logPathLayout->setContentsMargins(0, 0, 0, 0);
    logPathLayout->setSpacing(6);

    auto *logDirectory = new QLineEdit(&dialog);
    logDirectory->setText(settings.value(QStringLiteral("global/logDirectory"), defaultLogDir).toString());
    auto *browse = new QPushButton(QStringLiteral("Browse..."), &dialog);
    logPathLayout->addWidget(logDirectory, 1);
    logPathLayout->addWidget(browse);
    form->addRow(QStringLiteral("Default Log Directory:"), logPathRow);

    connect(browse, &QPushButton::clicked, &dialog, [&dialog, logDirectory]() {
        QString start = logDirectory->text().trimmed();
        if (start.isEmpty())
            start = QDir::homePath();
        const QString selected = QFileDialog::getExistingDirectory(&dialog,
                                                                    QStringLiteral("Select Default Log Directory"),
                                                                    start);
        if (!selected.isEmpty())
            logDirectory->setText(selected);
    });

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue(QStringLiteral("global/loggingEnabledByDefault"), enableLoggingByDefault->isChecked());
    settings.setValue(QStringLiteral("global/logDirectory"), logDirectory->text().trimmed());
    settings.setValue(QStringLiteral("global/scrollbackLines"), scrollbackLines->value());
    statusBar()->showMessage(QStringLiteral("Global options saved"), 2500);
}

void MainWindow::onNewSession()
{
    auto profile = NewSessionDialog::showNewSessionDialog(*m_profileManager, this);
    if (profile.name().isEmpty())
        return;  // User cancelled

    openProfileSession(profile);
    populateSessions();
}

void MainWindow::openProfileSession(const ConnectionProfile &profile)
{
    if (profile.name().isEmpty())
        return;

    auto *terminal = new TerminalWidget(m_tabs);
    const QFont sessionFont(profile.fontFamily(), profile.fontSize());
    terminal->applySessionFont(sessionFont);
    terminal->setDownloadDirectory(profile.downloadDirectory());
    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    const int defaultScrollbackLines = settings.value(QStringLiteral("global/scrollbackLines"), 10000).toInt();
    terminal->setScrollbackLimit(profile.property(QStringLiteral("scrollback_lines"),
                                                  QString::number(defaultScrollbackLines)).toInt());

    // Create connection based on profile type
    IConnection *connection = nullptr;

    switch (profile.type()) {
    case ConnectionProfile::ConnectionType::LocalShell:
        connection = new LocalShellConnection(terminal);
        break;

    case ConnectionProfile::ConnectionType::SSH:
        connection = new SshConnection(profile.sshHost(),
                                       profile.sshPort(),
                                       profile.sshUsername(),
                                       profile.sshPrivateKey(),
                                       profile.sshPassword(),
                                       terminal);
        break;

    case ConnectionProfile::ConnectionType::Serial:
        // TODO: Implement Serial connection
        QMessageBox::information(this, QStringLiteral("Coming Soon"),
                                QStringLiteral("Serial support is coming in CrossTerm 0.5"));
        terminal->deleteLater();
        return;

    case ConnectionProfile::ConnectionType::Telnet:
        // TODO: Implement Telnet connection
        QMessageBox::information(this, QStringLiteral("Coming Soon"),
                                QStringLiteral("Telnet support is coming in CrossTerm 0.6"));
        terminal->deleteLater();
        return;
    }

    if (!connection) {
        terminal->deleteLater();
        return;
    }

    terminal->setConnection(connection);

    const int index = m_tabs->addTab(terminal, profile.name());
    m_tabs->setCurrentIndex(index);

    connect(connection, &IConnection::connected, terminal, [this, terminal, profile] {
        const int idx = m_tabs->indexOf(terminal);
        if (idx >= 0) {
            m_tabs->setTabText(idx, profile.name() + QStringLiteral(" [LIVE]"));
        }
        statusBar()->showMessage(QStringLiteral("Connected: %1").arg(profile.name()), 2500);
    });
    connect(connection, &IConnection::disconnected, terminal, [this, terminal, connection, profile] {
        const int idx = m_tabs->indexOf(terminal);
        if (idx >= 0) {
            m_tabs->setTabText(idx, profile.name() + QStringLiteral(" [DOWN]"));
        }
        statusBar()->showMessage(QStringLiteral("Disconnected: %1").arg(profile.name()), 2500);
    });
    connect(terminal, &TerminalWidget::reconnectRequested, terminal, [this, terminal, connection, profile] {
        if (!connection || connection->isConnected())
            return;

        const int idx = m_tabs->indexOf(terminal);
        if (idx >= 0) {
            m_tabs->setTabText(idx, profile.name() + QStringLiteral(" [RECONNECTING]") );
        }
        statusBar()->showMessage(QStringLiteral("Manual reconnect: %1").arg(profile.name()), 2500);
        connection->connectSession();
    });

    // Session logging: per-profile option, falls back to global defaults.
    bool logEnabled = settings.value(QStringLiteral("global/loggingEnabledByDefault"), false).toBool();
    const QString rawEnabled = profile.property(QStringLiteral("session_log_enabled"));
    if (!rawEnabled.isEmpty()) {
        logEnabled = (rawEnabled == QStringLiteral("1") || rawEnabled.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    }

    if (logEnabled) {
        QString logPath = profile.property(QStringLiteral("session_log_path")).trimmed();
        if (logPath.isEmpty()) {
            const QString baseDir = settings.value(QStringLiteral("global/logDirectory"),
                                                   QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs")).toString();
            QString safeName = profile.name();
            safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
            logPath = QStringLiteral("%1/%2_%3.log")
                          .arg(baseDir,
                               safeName,
                               QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        }

        const QFileInfo logInfo(logPath);
        QDir().mkpath(logInfo.absolutePath());

        auto *logFile = new QFile(logPath, terminal);
        if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            connect(connection, &IConnection::dataReceived, terminal, [logFile](const QByteArray &data) {
                if (!logFile->isOpen())
                    return;
                logFile->write(data);
                logFile->flush();
            });
            statusBar()->showMessage(QStringLiteral("Logging enabled: %1").arg(logPath), 3500);
        } else {
            statusBar()->showMessage(QStringLiteral("Failed to open log file: %1").arg(logPath), 3500);
            logFile->deleteLater();
        }
    }

    connection->connectSession();
    terminal->setFocus();
}

void MainWindow::onNewLocalShell()
{
    ConnectionProfile localProfile(QStringLiteral("Local Shell"), ConnectionProfile::ConnectionType::LocalShell);
    openProfileSession(localProfile);
}

void MainWindow::closeCurrentTab()
{
    closeTab(m_tabs->currentIndex());
}

void MainWindow::closeTab(int index)
{
    if (index < 0)
        return;

    QWidget *page = m_tabs->widget(index);
    if (auto *terminal = qobject_cast<TerminalWidget *>(page)) {
        if (terminal->connection()) {
            terminal->connection()->setProperty("closing", true);
            terminal->connection()->disconnectSession();
        }
    }

    m_tabs->removeTab(index);
    page->deleteLater();
}
