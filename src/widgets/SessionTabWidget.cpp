#include "SessionTabWidget.h"
#include "../ui/Theme.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

SessionTabWidget::SessionTabWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("SessionTabHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_tabBar = new QTabBar(header);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setMovable(true);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setIconSize(QSize(18, 18));
    headerLayout->addWidget(m_tabBar, 0, Qt::AlignBottom);

    m_newTabButton = new QToolButton(header);
    m_newTabButton->setObjectName(QStringLiteral("NewTabButton"));
    m_newTabButton->setIcon(Theme::icon(Theme::IconKind::Plus));
    m_newTabButton->setIconSize(QSize(18, 18));
    m_newTabButton->setToolTip(QStringLiteral("New tab"));
    m_newTabButton->setPopupMode(QToolButton::InstantPopup);
    headerLayout->addWidget(m_newTabButton, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("SessionStack"));

    layout->addWidget(header);
    layout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        QWidget *page = m_stack->widget(from);
        m_stack->removeWidget(page);
        m_stack->insertWidget(to, page);
        m_stack->setCurrentIndex(m_tabBar->currentIndex());
    });
}

int SessionTabWidget::addTab(QWidget *page, const QIcon &icon, const QString &text)
{
    m_stack->addWidget(page);
    const int index = m_tabBar->addTab(icon, text);

    auto *closeButton = new QToolButton(m_tabBar);
    closeButton->setIcon(Theme::icon(Theme::IconKind::Close, Theme::ColorRole::Accent));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip(QStringLiteral("Close tab"));
    closeButton->setCursor(Qt::ArrowCursor);
    connect(closeButton, &QToolButton::clicked, this, [this, closeButton] {
        for (int i = 0; i < m_tabBar->count(); ++i) {
            if (m_tabBar->tabButton(i, QTabBar::RightSide) == closeButton) {
                emit tabCloseRequested(i);
                return;
            }
        }
    });
    m_tabBar->setTabButton(index, QTabBar::RightSide, closeButton);
    return index;
}

void SessionTabWidget::removeTab(int index)
{
    if (index < 0 || index >= count())
        return;
    // Drop the page first so the tab bar's currentChanged maps onto the
    // already-shifted stack indices.
    m_stack->removeWidget(m_stack->widget(index));
    m_tabBar->removeTab(index);
}

int SessionTabWidget::count() const
{
    return m_tabBar->count();
}

int SessionTabWidget::currentIndex() const
{
    return m_tabBar->currentIndex();
}

void SessionTabWidget::setCurrentIndex(int index)
{
    m_tabBar->setCurrentIndex(index);
}

QWidget *SessionTabWidget::widget(int index) const
{
    return m_stack->widget(index);
}

QWidget *SessionTabWidget::currentWidget() const
{
    return m_stack->currentWidget();
}

int SessionTabWidget::indexOf(const QWidget *page) const
{
    return m_stack->indexOf(page);
}

void SessionTabWidget::setTabText(int index, const QString &text)
{
    m_tabBar->setTabText(index, text);
}

void SessionTabWidget::setTabIcon(int index, const QIcon &icon)
{
    m_tabBar->setTabIcon(index, icon);
}
