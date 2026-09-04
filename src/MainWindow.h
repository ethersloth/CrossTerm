#pragma once

#include <QMainWindow>

class QTreeWidget;
class QTabWidget;
class QTreeWidgetItem;
class QLineEdit;
class ProfileManager;
class ConnectionProfile;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openSelectedSession(QTreeWidgetItem *item, int column);
    void onSessionContextMenu(const QPoint &pos);
    void onSessionFilterChanged(const QString &text);
    void onGlobalOptions();
    void onNewSession();
    void onNewLocalShell();
    void closeCurrentTab();
    void closeTab(int index);

private:
    void buildUi();
    void buildMenus();
    void populateSessions();
    void openProfileSession(const ConnectionProfile &profile);
    void reconcileSessionTree();
    void createFolder(const QString &parentPath);
    void renameFolderInteractive(const QString &path);
    void deleteFolderInteractive(const QString &path);
    void exportFolder(const QString &rootPath);
    void importFolder(const QString &targetPath);
    QStringList recentSessionNames() const;
    void pushRecentSession(const QString &name);

    QTreeWidget *m_sessionTree = nullptr;
    QLineEdit *m_sessionFilterEdit = nullptr;
    QTabWidget *m_tabs = nullptr;
    ProfileManager *m_profileManager = nullptr;
};
