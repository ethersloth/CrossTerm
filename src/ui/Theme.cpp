#include "Theme.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace {
struct ThemeColors {
    QColor window;
    QColor panel;
    QColor raised;
    QColor input;
    QColor border;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor selection;
    QColor selectionText;
    QColor hover;
    QColor danger;
    QColor success;
};

const ThemeColors &colorsFor(Theme::Mode mode)
{
    static const ThemeColors dark{
        QColor(0x0f, 0x11, 0x15), // window
        QColor(0x15, 0x18, 0x1d), // panel
        QColor(0x1c, 0x20, 0x27), // raised
        QColor(0x11, 0x13, 0x17), // input
        QColor(0x2b, 0x30, 0x38), // border
        QColor(0xe6, 0xe8, 0xeb), // text
        QColor(0x9a, 0xa3, 0xad), // muted text
        QColor(0x25, 0x63, 0xeb), // accent
        QColor(0x3b, 0x76, 0xf0), // accent hover
        QColor(0xff, 0xff, 0xff), // accent text
        QColor(0x1d, 0x3a, 0x70), // selection
        QColor(0xff, 0xff, 0xff), // selection text
        QColor(0x20, 0x25, 0x2d), // hover
        QColor(0xef, 0x44, 0x44), // danger
        QColor(0x22, 0xc5, 0x5e), // success
    };
    static const ThemeColors light{
        QColor(0xee, 0xf0, 0xf3),
        QColor(0xf8, 0xf9, 0xfb),
        QColor(0xff, 0xff, 0xff),
        QColor(0xff, 0xff, 0xff),
        QColor(0xd3, 0xd8, 0xdf),
        QColor(0x1c, 0x21, 0x28),
        QColor(0x5b, 0x65, 0x72),
        QColor(0x25, 0x63, 0xeb),
        QColor(0x1d, 0x4e, 0xd8),
        QColor(0xff, 0xff, 0xff),
        QColor(0xd6, 0xe4, 0xff),
        QColor(0x0b, 0x2e, 0x6b),
        QColor(0xe7, 0xea, 0xef),
        QColor(0xdc, 0x26, 0x26),
        QColor(0x16, 0xa3, 0x4a),
    };
    return mode == Theme::Mode::Dark ? dark : light;
}

// Line icons drawn on a 24x24 grid with a 2-unit round stroke.
void drawIcon(QPainter &p, Theme::IconKind kind, const QColor &color)
{
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const auto dot = [&p](qreal x, qreal y) { p.drawLine(QPointF(x, y), QPointF(x + 0.01, y)); };

    switch (kind) {
    case Theme::IconKind::Terminal: {
        QPainterPath path;
        path.moveTo(4, 17);
        path.lineTo(10, 11);
        path.lineTo(4, 5);
        p.drawPath(path);
        p.drawLine(QPointF(12, 19), QPointF(20, 19));
        break;
    }
    case Theme::IconKind::Server:
        p.drawRoundedRect(QRectF(2.5, 3, 19, 7.5), 2, 2);
        p.drawRoundedRect(QRectF(2.5, 13.5, 19, 7.5), 2, 2);
        dot(6.5, 6.75);
        dot(6.5, 17.25);
        p.drawLine(QPointF(11, 6.75), QPointF(17.5, 6.75));
        p.drawLine(QPointF(11, 17.25), QPointF(17.5, 17.25));
        break;
    case Theme::IconKind::Folder: {
        QPainterPath path;
        path.moveTo(3, 5.5);
        path.lineTo(9, 5.5);
        path.lineTo(11, 8);
        path.lineTo(21, 8);
        path.lineTo(21, 19);
        path.lineTo(3, 19);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Pin: {
        QPainterPath path;
        path.moveTo(9, 3.5);
        path.lineTo(15, 3.5);
        path.moveTo(10, 3.5);
        path.lineTo(10, 9.5);
        path.lineTo(6, 14.5);
        path.lineTo(18, 14.5);
        path.lineTo(14, 9.5);
        path.lineTo(14, 3.5);
        p.drawPath(path);
        p.drawLine(QPointF(12, 14.5), QPointF(12, 21));
        break;
    }
    case Theme::IconKind::Clock: {
        p.drawEllipse(QPointF(12, 12), 9.5, 9.5);
        QPainterPath path;
        path.moveTo(12, 7);
        path.lineTo(12, 12);
        path.lineTo(15.5, 14);
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Monitor:
        p.drawRoundedRect(QRectF(2.5, 3.5, 19, 13.5), 2, 2);
        p.drawLine(QPointF(8, 21), QPointF(16, 21));
        p.drawLine(QPointF(12, 17), QPointF(12, 21));
        break;
    case Theme::IconKind::Play: {
        QPainterPath path;
        path.moveTo(7, 4.5);
        path.lineTo(19.5, 12);
        path.lineTo(7, 19.5);
        path.closeSubpath();
        p.setBrush(color);
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Theme::IconKind::Search:
        p.drawEllipse(QPointF(11, 11), 7, 7);
        p.drawLine(QPointF(20, 20), QPointF(16, 16));
        break;
    case Theme::IconKind::Close:
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        break;
    case Theme::IconKind::Undock: {
        QPainterPath path;
        path.moveTo(12, 5);
        path.lineTo(19, 12);
        path.lineTo(12, 19);
        path.lineTo(5, 12);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Info:
        p.drawEllipse(QPointF(12, 12), 9.5, 9.5);
        p.drawLine(QPointF(12, 16), QPointF(12, 11.5));
        dot(12, 8);
        break;
    case Theme::IconKind::Network: {
        p.drawRoundedRect(QRectF(9, 2.5, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(2.5, 15.5, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(15.5, 15.5, 6, 6), 1, 1);
        QPainterPath path;
        path.moveTo(5.5, 15.5);
        path.lineTo(5.5, 12);
        path.lineTo(18.5, 12);
        path.lineTo(18.5, 15.5);
        path.moveTo(12, 8.5);
        path.lineTo(12, 12);
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Disk: {
        p.drawEllipse(QRectF(3.5, 2.5, 17, 6));
        QPainterPath path;
        path.moveTo(3.5, 5.5);
        path.lineTo(3.5, 18.5);
        path.moveTo(20.5, 5.5);
        path.lineTo(20.5, 18.5);
        const QRectF middle(3.5, 9, 17, 6);
        path.arcMoveTo(middle, 180);
        path.arcTo(middle, 180, 180);
        const QRectF bottom(3.5, 15.5, 17, 6);
        path.arcMoveTo(bottom, 180);
        path.arcTo(bottom, 180, 180);
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Activity: {
        QPainterPath path;
        path.moveTo(22, 12);
        path.lineTo(18, 12);
        path.lineTo(15, 20.5);
        path.lineTo(9, 3.5);
        path.lineTo(6, 12);
        path.lineTo(2, 12);
        p.drawPath(path);
        break;
    }
    case Theme::IconKind::Gear: {
        const QPointF center(12, 12);
        p.drawEllipse(center, 3, 3);
        p.drawEllipse(center, 7, 7);
        for (int i = 0; i < 8; ++i) {
            const qreal angle = i * std::numbers::pi / 4.0;
            const QPointF dir(std::cos(angle), std::sin(angle));
            p.drawLine(center + dir * 7.0, center + dir * 9.5);
        }
        break;
    }
    case Theme::IconKind::Restart: {
        const QRectF circle(3.5, 3.5, 17, 17);
        QPainterPath path;
        path.arcMoveTo(circle, 40);
        path.arcTo(circle, 40, 290);
        p.drawPath(path);
        const QPointF tip = path.pointAtPercent(0.0);
        QPainterPath head;
        head.moveTo(tip + QPointF(0.5, -5.0));
        head.lineTo(tip);
        head.lineTo(tip + QPointF(-4.8, -0.8));
        p.drawPath(head);
        break;
    }
    }
}

class LineIconEngine final : public QIconEngine
{
public:
    LineIconEngine(Theme::IconKind kind, Theme::ColorRole role, std::optional<QColor> fixedColor)
        : m_kind(kind), m_role(role), m_fixedColor(std::move(fixedColor))
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor color;
        if (m_fixedColor) {
            color = *m_fixedColor;
        } else if (mode == QIcon::Selected && m_role == Theme::ColorRole::Text) {
            color = Theme::instance().color(Theme::ColorRole::SelectionText);
        } else {
            color = Theme::instance().color(m_role);
        }
        if (mode == QIcon::Disabled)
            color.setAlphaF(0.4);

        const qreal side = std::min(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        drawIcon(*painter, m_kind, color);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size; }
    QString key() const override { return QStringLiteral("CrossTermLineIcon"); }
    QIconEngine *clone() const override { return new LineIconEngine(*this); }

private:
    Theme::IconKind m_kind;
    Theme::ColorRole m_role;
    std::optional<QColor> m_fixedColor;
};
}

Theme &Theme::instance()
{
    static Theme theme;
    return theme;
}

Theme::Mode Theme::modeFromString(const QString &value)
{
    return value.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0 ? Mode::Light : Mode::Dark;
}

QString Theme::modeToString(Mode mode)
{
    return mode == Mode::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

QColor Theme::color(ColorRole role) const
{
    const ThemeColors &c = colorsFor(m_mode);
    switch (role) {
    case ColorRole::Window: return c.window;
    case ColorRole::Panel: return c.panel;
    case ColorRole::Raised: return c.raised;
    case ColorRole::Input: return c.input;
    case ColorRole::Border: return c.border;
    case ColorRole::Text: return c.text;
    case ColorRole::MutedText: return c.mutedText;
    case ColorRole::Accent: return c.accent;
    case ColorRole::AccentText: return c.accentText;
    case ColorRole::Selection: return c.selection;
    case ColorRole::SelectionText: return c.selectionText;
    case ColorRole::Hover: return c.hover;
    case ColorRole::Danger: return c.danger;
    case ColorRole::Success: return c.success;
    }
    return c.text;
}

QIcon Theme::icon(IconKind kind, ColorRole role)
{
    return QIcon(new LineIconEngine(kind, role, std::nullopt));
}

QIcon Theme::icon(IconKind kind, const QColor &color)
{
    return QIcon(new LineIconEngine(kind, ColorRole::Text, color));
}

void Theme::apply(Mode mode)
{
    m_mode = mode;
    const ThemeColors &c = colorsFor(mode);

    if (QApplication::style()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.input);
    palette.setColor(QPalette::AlternateBase, c.panel);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.raised);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.danger);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.accentText);
    palette.setColor(QPalette::ToolTipBase, c.panel);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::PlaceholderText, c.mutedText);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Light, c.raised.lighter(115));
    palette.setColor(QPalette::Midlight, c.raised);
    palette.setColor(QPalette::Mid, c.border);
    palette.setColor(QPalette::Dark, c.border.darker(130));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, 160));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, c.mutedText);
    }
    QApplication::setPalette(palette);
    qApp->setStyleSheet(styleSheet());

    emit changed();
}

QString Theme::styleSheet() const
{
    const ThemeColors &c = colorsFor(m_mode);

    QString qss = QStringLiteral(R"(
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @window; width: 6px; height: 6px; }

QMenuBar { background: @window; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: @hover; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: @selection; color: @selectionText; }
QMenu::item:disabled { color: @mutedText; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QToolTip { background: @panel; color: @text; border: 1px solid @border; padding: 4px 6px; }

QStatusBar { background: @window; color: @mutedText; }
QStatusBar::item { border: none; }

QLineEdit, QComboBox {
    background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
    padding: 5px 8px; selection-background-color: @accent; selection-color: @accentText;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: @accent; }
QSpinBox {
    background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
    padding: 4px 22px 4px 8px; selection-background-color: @accent; selection-color: @accentText;
}
QSpinBox::up-button, QSpinBox::down-button { width: 18px; border: none; background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; margin: 3px 3px 0 0; }
QSpinBox::down-button { subcontrol-position: bottom right; margin: 0 3px 3px 0; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(:/icons/assets/chevron-down.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: @panel; border: 1px solid @border; selection-background-color: @selection;
    selection-color: @selectionText; outline: 0;
}
QSpinBox::up-arrow { image: url(:/icons/assets/chevron-up.png); width: 12px; height: 12px; }
QSpinBox::down-arrow { image: url(:/icons/assets/chevron-down.png); width: 12px; height: 12px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; border-radius: 4px; }

QCheckBox::indicator {
    width: 14px; height: 14px; border: 1px solid @mutedText; border-radius: 4px; background: @input;
}
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(:/icons/assets/check.png); }
QCheckBox::indicator:disabled { border-color: @border; background: @raised; }
QLineEdit:disabled { color: @mutedText; }

QPushButton {
    background: @raised; color: @text; border: 1px solid @border; border-radius: 8px; padding: 6px 14px;
}
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled { color: @mutedText; }
QPushButton:default { border-color: @accent; }

QToolButton { background: transparent; border: none; border-radius: 6px; padding: 3px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed { background: @border; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @border; border-radius: 3px; }
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::handle:hover { background: @mutedText; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

#SessionsHeader {
    background: @panel; border: 1px solid @border; border-bottom: none;
    border-top-left-radius: 10px; border-top-right-radius: 10px;
}
#SessionsPanel {
    background: @panel; border: 1px solid @border; border-top: none;
    border-bottom-left-radius: 10px; border-bottom-right-radius: 10px;
}
#SessionsTitle { font-weight: 600; }
#SessionsPanel QTreeView { background: @panel; border: none; outline: 0; }
#SessionsPanel QTreeView::item { padding: 4px 2px; border-radius: 6px; }
#SessionsPanel QTreeView::item:hover { background: @hover; }
#SessionsPanel QTreeView::item:selected { background: @selection; color: @selectionText; }

#SessionTabHeader { background: @window; }
#SessionTabHeader QTabBar::tab {
    background: @panel; color: @mutedText; border: 1px solid @border; border-bottom: none;
    border-top-left-radius: 8px; border-top-right-radius: 8px;
    padding: 7px 8px 7px 12px; margin-right: 4px; min-width: 110px;
}
#SessionTabHeader QTabBar::tab:selected { background: @raised; color: @text; border-top: 2px solid @accent; }
#SessionTabHeader QTabBar::tab:hover:!selected { background: @hover; }
#NewTabButton { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 5px; }
#NewTabButton:hover { background: @hover; }
#SessionStack { background: @panel; border: 1px solid @border; }

#CommandBar { background: @panel; border-bottom: 1px solid @border; }
#CommandAddButton { background: @raised; border: 1px dashed @border; border-radius: 8px; padding: 7px; }
#CommandAddButton:hover { background: @hover; border-color: @accent; }
#CommandOverflow { background: @raised; border: 1px solid @border; border-radius: 8px; padding: 6px 10px; }
#CommandOverflow:hover { background: @hover; }
#CommandOverflow::menu-indicator, #NewTabButton::menu-indicator { image: none; width: 0; }
#CommandButton { background: @raised; padding: 6px 12px; }
#CommandButton:hover { background: @hover; }
#CommandSeparator { background: @border; }
#RunButton {
    background: @accent; border: 1px solid @accent; color: @accentText; font-weight: 600; padding: 6px 18px;
}
#RunButton:hover { background: @accentHover; border-color: @accentHover; }
#RunButton:disabled { background: @raised; border-color: @border; color: @mutedText; }
)");

    // Longest names first so "@selectionText" is not clobbered by "@selection".
    QList<std::pair<QString, QColor>> tokens = {
        {QStringLiteral("@window"), c.window},
        {QStringLiteral("@panel"), c.panel},
        {QStringLiteral("@raised"), c.raised},
        {QStringLiteral("@input"), c.input},
        {QStringLiteral("@border"), c.border},
        {QStringLiteral("@text"), c.text},
        {QStringLiteral("@mutedText"), c.mutedText},
        {QStringLiteral("@accent"), c.accent},
        {QStringLiteral("@accentHover"), c.accentHover},
        {QStringLiteral("@accentText"), c.accentText},
        {QStringLiteral("@selection"), c.selection},
        {QStringLiteral("@selectionText"), c.selectionText},
        {QStringLiteral("@hover"), c.hover},
    };
    std::sort(tokens.begin(), tokens.end(), [](const auto &a, const auto &b) {
        return a.first.size() > b.first.size();
    });
    for (const auto &[token, color] : tokens)
        qss.replace(token, color.name());
    return qss;
}
