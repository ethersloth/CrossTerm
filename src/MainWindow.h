#pragma once

#include <QMainWindow>

class QTreeWidget;
class QTabWidget;
class QTreeWidgetItem;
class ProfileManager;
class ConnectionProfile;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openSelectedSession(QTreeWidgetItem *item, int column);
    void onSessionContextMenu(const QPoint &pos);
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

    QTreeWidget *m_sessionTree = nullptr;
    QTabWidget *m_tabs = nullptr;
    ProfileManager *m_profileManager = nullptr;
};
