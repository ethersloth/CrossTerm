#include "MainWindow.h"
#include "connections/LocalShellConnection.h"
#include "connections/SshConnection.h"
#include "widgets/TerminalWidget.h"
#include "profiles/ProfileManager.h"
#include "profiles/ConnectionProfile.h"
#include "dialogs/NewSessionDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QDropEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr int RoleKind = Qt::UserRole;
constexpr int RoleProfileName = Qt::UserRole + 1;
constexpr int RoleFolderPath = Qt::UserRole + 2;
constexpr int kMaxRecentSessions = 8;

QString connectionTypeLabel(ConnectionProfile::ConnectionType type)
{
    switch (type) {
    case ConnectionProfile::ConnectionType::LocalShell:
        return QStringLiteral("Local Shell");
    case ConnectionProfile::ConnectionType::SSH:
        return QStringLiteral("SSH");
    case ConnectionProfile::ConnectionType::Serial:
        return QStringLiteral("Serial");
    case ConnectionProfile::ConnectionType::Telnet:
        return QStringLiteral("Telnet");
    }
    return QString();
}

const QList<QPair<QString, QString>> &presetSessionColors()
{
    static const QList<QPair<QString, QString>> colors = {
        {QStringLiteral("Red"), QStringLiteral("#e74c3c")},
        {QStringLiteral("Orange"), QStringLiteral("#e67e22")},
        {QStringLiteral("Yellow"), QStringLiteral("#f1c40f")},
        {QStringLiteral("Green"), QStringLiteral("#2ecc71")},
        {QStringLiteral("Blue"), QStringLiteral("#3498db")},
        {QStringLiteral("Purple"), QStringLiteral("#9b59b6")},
        {QStringLiteral("Gray"), QStringLiteral("#95a5a6")},
    };
    return colors;
}

QIcon colorSwatchIcon(const QColor &color)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(color.darker(150));
    painter.drawEllipse(1, 1, 12, 12);
    return QIcon(pixmap);
}

void applyProfileAppearance(QTreeWidgetItem *item, const ConnectionProfile &profile)
{
    const QString colorHex = profile.sessionColor();
    if (colorHex.isEmpty())
        return;

    const QColor color(colorHex);
    if (color.isValid())
        item->setIcon(0, colorSwatchIcon(color));
}

// Returns whether `item` (or any descendant) is visible after filtering by `filterLower`.
bool applyTreeFilter(QTreeWidgetItem *item, const QString &filterLower)
{
    bool anyChildVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (applyTreeFilter(item->child(i), filterLower))
            anyChildVisible = true;
    }

    const bool selfMatches = filterLower.isEmpty() || item->text(0).contains(filterLower, Qt::CaseInsensitive);
    const bool visible = selfMatches || anyChildVisible;
    item->setHidden(!visible);
    if (!filterLower.isEmpty() && anyChildVisible)
        item->setExpanded(true);
    return visible;
}

// Plain QTreeWidget internal-move drag & drop reparents/reorders items in the
// widget itself but doesn't tell us it happened. Notify the owner afterwards
// so it can rebuild the folder/profile model from the new tree shape.
class SessionTreeWidget : public QTreeWidget
{
public:
    explicit SessionTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent) {}

    std::function<void()> onDrop;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QTreeWidget::dropEvent(event);
        if (onDrop)
            onDrop();
    }
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_profileManager(new ProfileManager())
{
    setWindowTitle(QStringLiteral("CrossTerm 0.6.0"));
    resize(1200, 760);

    // Load saved profiles
    m_profileManager->loadProfiles();

    buildUi();
    buildMenus();
    populateSessions();

    statusBar()->showMessage(QStringLiteral("Ready"));
}

MainWindow::~MainWindow()
{
    if (!m_tabs)
        return;

    for (int index = 0; index < m_tabs->count(); ++index) {
        auto *terminal = qobject_cast<TerminalWidget *>(m_tabs->widget(index));
        if (!terminal || !terminal->connection())
            continue;

        IConnection *connection = terminal->connection();
        connection->blockSignals(true);
        connection->disconnectSession();
    }
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

    auto *sessionsContainer = new QWidget(dock);
    auto *sessionsLayout = new QVBoxLayout(sessionsContainer);
    sessionsLayout->setContentsMargins(4, 4, 4, 4);
    sessionsLayout->setSpacing(4);

    m_sessionFilterEdit = new QLineEdit(sessionsContainer);
    m_sessionFilterEdit->setPlaceholderText(QStringLiteral("Filter sessions..."));
    m_sessionFilterEdit->setClearButtonEnabled(true);
    sessionsLayout->addWidget(m_sessionFilterEdit);

    auto *sessionTree = new SessionTreeWidget(sessionsContainer);
    sessionTree->setHeaderHidden(true);
    sessionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    sessionTree->setDragEnabled(true);
    sessionTree->setAcceptDrops(true);
    sessionTree->setDropIndicatorShown(true);
    sessionTree->setDragDropMode(QAbstractItemView::InternalMove);
    sessionTree->setSelectionMode(QAbstractItemView::SingleSelection);
    sessionTree->onDrop = [this] { reconcileSessionTree(); };
    m_sessionTree = sessionTree;
    sessionsLayout->addWidget(m_sessionTree, 1);

    dock->setWidget(sessionsContainer);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(m_sessionTree, &QTreeWidget::itemDoubleClicked,
            this, &MainWindow::openSelectedSession);
    connect(m_sessionTree, &QWidget::customContextMenuRequested,
            this, &MainWindow::onSessionContextMenu);
    connect(m_sessionFilterEdit, &QLineEdit::textChanged,
            this, &MainWindow::onSessionFilterChanged);
}

void MainWindow::onSessionFilterChanged(const QString &text)
{
    const QString filterLower = text.trimmed();
    QTreeWidgetItem *root = m_sessionTree->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        applyTreeFilter(root->child(i), filterLower);
    }
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
                           QStringLiteral("CrossTerm 0.6.0\n\n"
                                          "A cross-platform terminal and connection manager built with C++20 and Qt 6.\n\n"
                                          "Features:\n"
                                          "• VT100 Terminal Emulation\n"
                                          "• Saved Connection Profiles with Folders and Tags\n"
                                          "• Local Shell and SSH Support\n"
                                          "• Serial and Telnet (coming soon)"));
    });
}

void MainWindow::populateSessions()
{
    m_sessionTree->clear();

    auto *localShell = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Local Shell")});
    localShell->setData(0, RoleKind, QStringLiteral("local-shell"));
    localShell->setFlags(localShell->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));

    const auto allProfiles = m_profileManager->allProfiles();

    QList<ConnectionProfile> pinnedProfiles;
    for (const auto &profile : allProfiles) {
        if (profile.isPinned())
            pinnedProfiles.append(profile);
    }
    if (!pinnedProfiles.isEmpty()) {
        auto *pinnedGroup = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Pinned")});
        pinnedGroup->setData(0, RoleKind, QStringLiteral("pinned-group"));
        pinnedGroup->setFlags(pinnedGroup->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
        for (const auto &profile : pinnedProfiles) {
            auto *item = new QTreeWidgetItem(pinnedGroup, {profile.name()});
            item->setData(0, RoleKind, QStringLiteral("pinned-profile"));
            item->setData(0, RoleProfileName, profile.name());
            item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
            item->setToolTip(0, connectionTypeLabel(profile.type()));
            applyProfileAppearance(item, profile);
        }
        pinnedGroup->setExpanded(true);
    }

    QList<ConnectionProfile> recentProfiles;
    for (const auto &name : recentSessionNames()) {
        if (m_profileManager->hasProfile(name))
            recentProfiles.append(m_profileManager->profile(name));
    }
    if (!recentProfiles.isEmpty()) {
        auto *recentGroup = new QTreeWidgetItem(m_sessionTree, {QStringLiteral("Recent")});
        recentGroup->setData(0, RoleKind, QStringLiteral("recent-group"));
        recentGroup->setFlags(recentGroup->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
        for (const auto &profile : recentProfiles) {
            auto *item = new QTreeWidgetItem(recentGroup, {profile.name()});
            item->setData(0, RoleKind, QStringLiteral("recent-profile"));
            item->setData(0, RoleProfileName, profile.name());
            item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
            item->setToolTip(0, connectionTypeLabel(profile.type()));
            applyProfileAppearance(item, profile);
        }
        recentGroup->setExpanded(true);
    }

    QMap<QString, QTreeWidgetItem *> folderItems;
    std::function<QTreeWidgetItem *(const QString &)> ensureFolder =
        [&](const QString &path) -> QTreeWidgetItem * {
        if (path.isEmpty())
            return nullptr;

        auto it = folderItems.constFind(path);
        if (it != folderItems.constEnd())
            return it.value();

        const int slash = path.lastIndexOf(QLatin1Char('/'));
        const QString name = slash < 0 ? path : path.mid(slash + 1);
        QTreeWidgetItem *parentItem = slash < 0 ? nullptr : ensureFolder(path.left(slash));

        auto *item = parentItem ? new QTreeWidgetItem(parentItem, {name})
                                 : new QTreeWidgetItem(m_sessionTree, {name});
        item->setData(0, RoleKind, QStringLiteral("folder"));
        item->setData(0, RoleFolderPath, path);
        item->setFlags(item->flags() | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled);
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        folderItems.insert(path, item);
        return item;
    };

    QStringList folderPaths = m_profileManager->folders();
    folderPaths.sort(Qt::CaseInsensitive);
    for (const auto &path : std::as_const(folderPaths)) {
        ensureFolder(path);
    }

    for (const auto &profile : allProfiles) {
        QTreeWidgetItem *parentItem = ensureFolder(profile.folder());
        auto *item = parentItem ? new QTreeWidgetItem(parentItem, {profile.name()})
                                 : new QTreeWidgetItem(m_sessionTree, {profile.name()});
        item->setData(0, RoleKind, QStringLiteral("saved-profile"));
        item->setData(0, RoleProfileName, profile.name());
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setToolTip(0, connectionTypeLabel(profile.type()));
        applyProfileAppearance(item, profile);
    }

    m_sessionTree->expandAll();

    if (m_sessionFilterEdit && !m_sessionFilterEdit->text().trimmed().isEmpty())
        onSessionFilterChanged(m_sessionFilterEdit->text());
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

    if (kind == QStringLiteral("saved-profile") || kind == QStringLiteral("pinned-profile")
        || kind == QStringLiteral("recent-profile")) {
        const QString name = item->data(0, RoleProfileName).toString();
        if (m_profileManager->hasProfile(name)) {
            openProfileSession(m_profileManager->profile(name));
        }
    }
}

void MainWindow::onSessionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sessionTree->itemAt(pos);
    const QString kind = item ? item->data(0, RoleKind).toString() : QString();

    if (!item || kind == QStringLiteral("folder")) {
        const QString parentPath = item ? item->data(0, RoleFolderPath).toString() : QString();

        QMenu menu(this);
        QAction *newSessionAction = menu.addAction(QStringLiteral("New Session..."));
        QAction *newFolderAction = menu.addAction(item ? QStringLiteral("New Subfolder...")
                                                        : QStringLiteral("New Folder..."));
        QAction *renameAction = nullptr;
        QAction *deleteAction = nullptr;
        if (item) {
            menu.addSeparator();
            renameAction = menu.addAction(QStringLiteral("Rename Folder..."));
            deleteAction = menu.addAction(QStringLiteral("Delete Folder..."));
        }
        menu.addSeparator();
        QAction *exportAction = menu.addAction(item ? QStringLiteral("Export Folder...")
                                                     : QStringLiteral("Export All Sessions..."));
        QAction *importAction = menu.addAction(item ? QStringLiteral("Import Into This Folder...")
                                                     : QStringLiteral("Import Sessions..."));

        QAction *chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == newSessionAction) {
            onNewSession();
        } else if (chosen == newFolderAction) {
            createFolder(parentPath);
        } else if (chosen == renameAction) {
            renameFolderInteractive(parentPath);
        } else if (chosen == deleteAction) {
            deleteFolderInteractive(parentPath);
        } else if (chosen == exportAction) {
            exportFolder(parentPath);
        } else if (chosen == importAction) {
            importFolder(parentPath);
        }
        return;
    }

    if (kind == QStringLiteral("local-shell") || kind == QStringLiteral("pinned-group")
        || kind == QStringLiteral("recent-group"))
        return;

    if (kind != QStringLiteral("saved-profile") && kind != QStringLiteral("pinned-profile")
        && kind != QStringLiteral("recent-profile"))
        return;

    const QString profileName = item->data(0, RoleProfileName).toString();
    if (profileName.isEmpty() || !m_profileManager->hasProfile(profileName))
        return;

    const ConnectionProfile currentProfile = m_profileManager->profile(profileName);

    QMenu menu(this);
    QAction *openAction = menu.addAction(QStringLiteral("Open Session"));
    QAction *propertiesAction = menu.addAction(QStringLiteral("Properties..."));
    QAction *pinAction = menu.addAction(currentProfile.isPinned() ? QStringLiteral("Unpin Session")
                                                                  : QStringLiteral("Pin Session"));

    QMenu *moveMenu = menu.addMenu(QStringLiteral("Move to Folder"));
    QMap<QAction *, QString> moveTargets;
    QAction *rootAction = moveMenu->addAction(QStringLiteral("(No Folder)"));
    rootAction->setCheckable(true);
    rootAction->setChecked(currentProfile.folder().isEmpty());
    moveTargets.insert(rootAction, QString());

    QStringList allFolders = m_profileManager->folders();
    allFolders.sort(Qt::CaseInsensitive);
    if (!allFolders.isEmpty())
        moveMenu->addSeparator();
    for (const auto &folderPath : std::as_const(allFolders)) {
        QAction *folderAction = moveMenu->addAction(folderPath);
        folderAction->setCheckable(true);
        folderAction->setChecked(currentProfile.folder() == folderPath);
        moveTargets.insert(folderAction, folderPath);
    }

    QMenu *colorMenu = menu.addMenu(QStringLiteral("Set Color"));
    QMap<QAction *, QString> colorTargets;
    for (const auto &preset : presetSessionColors()) {
        QAction *colorAction = colorMenu->addAction(colorSwatchIcon(QColor(preset.second)), preset.first);
        colorAction->setCheckable(true);
        colorAction->setChecked(currentProfile.sessionColor().compare(preset.second, Qt::CaseInsensitive) == 0);
        colorTargets.insert(colorAction, preset.second);
    }
    colorMenu->addSeparator();
    QAction *customColorAction = colorMenu->addAction(QStringLiteral("Custom..."));
    QAction *clearColorAction = colorMenu->addAction(QStringLiteral("Clear Color"));
    clearColorAction->setEnabled(!currentProfile.sessionColor().isEmpty());

    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete Session..."));

    QAction *chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == openAction) {
        openProfileSession(m_profileManager->profile(profileName));
        return;
    }

    if (chosen == pinAction) {
        ConnectionProfile updated = currentProfile;
        updated.setPinned(!updated.isPinned());
        m_profileManager->updateProfile(updated);
        if (m_profileManager->saveProfiles())
            populateSessions();
        return;
    }

    if (moveTargets.contains(chosen)) {
        ConnectionProfile updated = currentProfile;
        updated.setFolder(moveTargets.value(chosen));
        m_profileManager->updateProfile(updated);
        if (m_profileManager->saveProfiles())
            populateSessions();
        return;
    }

    if (colorTargets.contains(chosen)) {
        ConnectionProfile updated = currentProfile;
        updated.setSessionColor(colorTargets.value(chosen));
        m_profileManager->updateProfile(updated);
        if (m_profileManager->saveProfiles())
            populateSessions();
        return;
    }

    if (chosen == customColorAction) {
        const QColor initial = currentProfile.sessionColor().isEmpty()
            ? QColor(Qt::blue) : QColor(currentProfile.sessionColor());
        const QColor picked = QColorDialog::getColor(initial, this, QStringLiteral("Session Color"));
        if (picked.isValid()) {
            ConnectionProfile updated = currentProfile;
            updated.setSessionColor(picked.name());
            m_profileManager->updateProfile(updated);
            if (m_profileManager->saveProfiles())
                populateSessions();
        }
        return;
    }

    if (chosen == clearColorAction) {
        ConnectionProfile updated = currentProfile;
        updated.setSessionColor(QString());
        m_profileManager->updateProfile(updated);
        if (m_profileManager->saveProfiles())
            populateSessions();
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

void MainWindow::createFolder(const QString &parentPath)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Folder"),
                                                QStringLiteral("Folder name:"),
                                                QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, QStringLiteral("New Folder"),
                             QStringLiteral("Folder names can't contain '/'."));
        return;
    }

    const QString path = parentPath.isEmpty() ? name : parentPath + QStringLiteral("/") + name;
    m_profileManager->addFolder(path);
    if (m_profileManager->saveProfiles())
        populateSessions();
}

void MainWindow::renameFolderInteractive(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString currentName = slash < 0 ? path : path.mid(slash + 1);
    const QString parentPath = slash < 0 ? QString() : path.left(slash);

    bool ok = false;
    const QString newName = QInputDialog::getText(this, QStringLiteral("Rename Folder"),
                                                   QStringLiteral("Folder name:"),
                                                   QLineEdit::Normal, currentName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == currentName)
        return;

    if (newName.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, QStringLiteral("Rename Folder"),
                             QStringLiteral("Folder names can't contain '/'."));
        return;
    }

    const QString newPath = parentPath.isEmpty() ? newName : parentPath + QStringLiteral("/") + newName;
    m_profileManager->renameFolder(path, newPath);
    if (m_profileManager->saveProfiles())
        populateSessions();
}

void MainWindow::deleteFolderInteractive(const QString &path)
{
    const QMessageBox::StandardButton confirmation = QMessageBox::question(
        this,
        QStringLiteral("Delete Folder"),
        QStringLiteral("Delete the folder '%1'?\n\nAny sessions and subfolders inside will move to the top level.").arg(path),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirmation != QMessageBox::Yes)
        return;

    m_profileManager->removeFolder(path);
    if (!m_profileManager->saveProfiles()) {
        QMessageBox::critical(this, QStringLiteral("Delete Folder"),
                              QStringLiteral("Failed to save the updated session list."));
        return;
    }

    populateSessions();
    statusBar()->showMessage(QStringLiteral("Deleted folder: %1").arg(path), 2500);
}

void MainWindow::reconcileSessionTree()
{
    QStringList newFolders;
    QHash<QString, QString> profileFolders;

    std::function<void(QTreeWidgetItem *, const QString &)> walk =
        [&](QTreeWidgetItem *parent, const QString &parentPath) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            const QString kind = child->data(0, RoleKind).toString();
            if (kind == QStringLiteral("folder")) {
                const QString path = parentPath.isEmpty()
                    ? child->text(0)
                    : parentPath + QStringLiteral("/") + child->text(0);
                newFolders << path;
                walk(child, path);
            } else if (kind == QStringLiteral("saved-profile")) {
                profileFolders.insert(child->data(0, RoleProfileName).toString(), parentPath);
            }
        }
    };
    walk(m_sessionTree->invisibleRootItem(), QString());

    m_profileManager->setFolders(newFolders);
    for (auto it = profileFolders.constBegin(); it != profileFolders.constEnd(); ++it) {
        ConnectionProfile updated = m_profileManager->profile(it.key());
        if (updated.name().isEmpty())
            continue;
        updated.setFolder(it.value());
        m_profileManager->updateProfile(updated);
    }

    m_profileManager->saveProfiles();
    populateSessions();
}

QStringList MainWindow::recentSessionNames() const
{
    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    return settings.value(QStringLiteral("session/recentNames")).toStringList();
}

void MainWindow::pushRecentSession(const QString &name)
{
    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    QStringList recents = settings.value(QStringLiteral("session/recentNames")).toStringList();
    recents.removeAll(name);
    recents.prepend(name);
    while (recents.size() > kMaxRecentSessions)
        recents.removeLast();
    settings.setValue(QStringLiteral("session/recentNames"), recents);
}

void MainWindow::exportFolder(const QString &rootPath)
{
    const QString suggestedName = rootPath.isEmpty()
        ? QStringLiteral("sessions.json")
        : rootPath.mid(rootPath.lastIndexOf(QLatin1Char('/')) + 1) + QStringLiteral(".json");

    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Sessions"), suggestedName,
        QStringLiteral("JSON Files (*.json)"));
    if (filePath.isEmpty())
        return;

    QJsonArray foldersArray;
    for (const auto &folderPath : m_profileManager->folders()) {
        if (rootPath.isEmpty()) {
            foldersArray.append(folderPath);
        } else if (folderPath.startsWith(rootPath + QStringLiteral("/"))) {
            foldersArray.append(folderPath.mid(rootPath.length() + 1));
        }
    }

    QJsonArray profilesArray;
    for (const auto &profile : m_profileManager->allProfiles()) {
        const QString profileFolder = profile.folder();
        const bool included = rootPath.isEmpty() || profileFolder == rootPath
            || profileFolder.startsWith(rootPath + QStringLiteral("/"));
        if (!included)
            continue;

        ConnectionProfile exported = profile;
        if (rootPath.isEmpty()) {
            // keep folder as-is
        } else if (profileFolder == rootPath) {
            exported.setFolder(QString());
        } else {
            exported.setFolder(profileFolder.mid(rootPath.length() + 1));
        }
        profilesArray.append(exported.toJson());
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("folders")] = foldersArray;
    root[QStringLiteral("profiles")] = profilesArray;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, QStringLiteral("Export Sessions"),
                              QStringLiteral("Failed to write file: %1").arg(filePath));
        return;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();

    statusBar()->showMessage(QStringLiteral("Exported %1 session(s) to %2")
                                  .arg(profilesArray.size()).arg(filePath), 3500);
}

void MainWindow::importFolder(const QString &targetPath)
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Sessions"), QString(),
        QStringLiteral("JSON Files (*.json)"));
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Import Sessions"),
                              QStringLiteral("Failed to read file: %1").arg(filePath));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Import Sessions"),
                             QStringLiteral("'%1' isn't a valid CrossTerm session export.").arg(filePath));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray foldersArray = root[QStringLiteral("folders")].toArray();
    const QJsonArray profilesArray = root[QStringLiteral("profiles")].toArray();

    for (const auto &value : foldersArray) {
        const QString relPath = value.toString();
        if (relPath.isEmpty())
            continue;
        const QString absPath = targetPath.isEmpty() ? relPath : targetPath + QStringLiteral("/") + relPath;
        m_profileManager->addFolder(absPath);
    }

    int imported = 0;
    for (const auto &value : profilesArray) {
        if (!value.isObject())
            continue;

        ConnectionProfile profile = ConnectionProfile::fromJson(value.toObject());
        if (profile.name().isEmpty())
            continue;

        QString uniqueName = profile.name();
        int suffix = 2;
        while (m_profileManager->hasProfile(uniqueName))
            uniqueName = QStringLiteral("%1 (%2)").arg(profile.name()).arg(suffix++);
        profile.setName(uniqueName);

        const QString relFolder = profile.folder();
        if (!targetPath.isEmpty()) {
            profile.setFolder(relFolder.isEmpty() ? targetPath : targetPath + QStringLiteral("/") + relFolder);
        }

        m_profileManager->addProfile(profile);
        ++imported;
    }

    if (!m_profileManager->saveProfiles()) {
        QMessageBox::critical(this, QStringLiteral("Import Sessions"),
                              QStringLiteral("Failed to save the imported sessions."));
        return;
    }

    populateSessions();
    statusBar()->showMessage(QStringLiteral("Imported %1 session(s) from %2")
                                  .arg(imported).arg(filePath), 3500);
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

    auto *fontFamily = new QFontComboBox(&dialog);
    const QString defaultFontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    fontFamily->setCurrentFont(QFont(settings.value(QStringLiteral("global/fontFamily"), defaultFontFamily).toString()));
    form->addRow(QStringLiteral("Default terminal font:"), fontFamily);

    auto *fontSize = new QSpinBox(&dialog);
    fontSize->setRange(6, 36);
    fontSize->setValue(settings.value(QStringLiteral("global/fontSize"), 12).toInt());
    form->addRow(QStringLiteral("Default font size:"), fontSize);

    auto *downloadPathRow = new QWidget(&dialog);
    auto *downloadPathLayout = new QHBoxLayout(downloadPathRow);
    downloadPathLayout->setContentsMargins(0, 0, 0, 0);
    downloadPathLayout->setSpacing(6);
    auto *downloadDirectory = new QLineEdit(&dialog);
    downloadDirectory->setText(settings.value(QStringLiteral("global/downloadDirectory"),
                                              QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString());
    auto *downloadBrowse = new QPushButton(QStringLiteral("Browse..."), &dialog);
    downloadPathLayout->addWidget(downloadDirectory, 1);
    downloadPathLayout->addWidget(downloadBrowse);
    form->addRow(QStringLiteral("Default ZModem download folder:"), downloadPathRow);

    connect(downloadBrowse, &QPushButton::clicked, &dialog, [&dialog, downloadDirectory]() {
        const QString selected = QFileDialog::getExistingDirectory(
            &dialog,
            QStringLiteral("Select Default Download Folder"),
            downloadDirectory->text().trimmed());
        if (!selected.isEmpty())
            downloadDirectory->setText(selected);
    });

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
    settings.setValue(QStringLiteral("global/fontFamily"), fontFamily->currentFont().family());
    settings.setValue(QStringLiteral("global/fontSize"), fontSize->value());
    settings.setValue(QStringLiteral("global/downloadDirectory"), downloadDirectory->text().trimmed());
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
    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    const QString defaultFontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    const QString sessionFontFamily = profile.hasProperty(QStringLiteral("font_family"))
        ? profile.fontFamily()
        : settings.value(QStringLiteral("global/fontFamily"), defaultFontFamily).toString();
    const int sessionFontSize = profile.hasProperty(QStringLiteral("font_size"))
        ? profile.fontSize()
        : settings.value(QStringLiteral("global/fontSize"), 12).toInt();
    const QFont sessionFont(sessionFontFamily, sessionFontSize);
    terminal->applySessionFont(sessionFont);
    terminal->setDownloadDirectory(profile.hasProperty(QStringLiteral("download_directory"))
                                       ? profile.downloadDirectory()
                                       : settings.value(QStringLiteral("global/downloadDirectory"),
                                                        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString());
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
                                QStringLiteral("Serial support is coming in a future release."));
        terminal->deleteLater();
        return;

    case ConnectionProfile::ConnectionType::Telnet:
        // TODO: Implement Telnet connection
        QMessageBox::information(this, QStringLiteral("Coming Soon"),
                                QStringLiteral("Telnet support is coming in a future release."));
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

    if (m_profileManager->hasProfile(profile.name())) {
        pushRecentSession(profile.name());
        populateSessions();
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
