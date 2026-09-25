#pragma once

#include <QWidget>

class QIcon;
class QStackedWidget;
class QTabBar;
class QToolButton;

// Tab strip with a "new tab" button right after the last tab, above a stack
// of session pages. Exposes the subset of the QTabWidget API MainWindow uses.
class SessionTabWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SessionTabWidget(QWidget *parent = nullptr);

    int addTab(QWidget *page, const QIcon &icon, const QString &text);
    void removeTab(int index);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    QWidget *widget(int index) const;
    QWidget *currentWidget() const;
    int indexOf(const QWidget *page) const;
    void setTabText(int index, const QString &text);
    void setTabIcon(int index, const QIcon &icon);

    QToolButton *newTabButton() const { return m_newTabButton; }

signals:
    void tabCloseRequested(int index);

private:
    QTabBar *m_tabBar = nullptr;
    QToolButton *m_newTabButton = nullptr;
    QStackedWidget *m_stack = nullptr;
};
