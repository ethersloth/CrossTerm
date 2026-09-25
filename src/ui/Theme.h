#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

class QApplication;

// Application-wide look: palette, stylesheet, and line icons painted in code
// (no SVG plugin needed at runtime). Icons resolve their color when painted,
// so switching themes only needs a repaint, not rebuilding every QIcon.
class Theme final : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Dark, Light };

    enum class ColorRole {
        Window,
        Panel,
        Raised,
        Input,
        Border,
        Text,
        MutedText,
        Accent,
        AccentText,
        Selection,
        SelectionText,
        Hover,
        Danger,
        Success,
    };

    enum class IconKind {
        Terminal,
        Server,
        Folder,
        Pin,
        Clock,
        Monitor,
        Play,
        Plus,
        Search,
        Close,
        Undock,
        Info,
        Network,
        Disk,
        Activity,
        Gear,
        Restart,
    };

    static Theme &instance();

    static Mode modeFromString(const QString &value);
    static QString modeToString(Mode mode);

    Mode mode() const { return m_mode; }
    void apply(Mode mode);

    QColor color(ColorRole role) const;

    // Icon drawn in a theme color, re-resolved on every paint.
    static QIcon icon(IconKind kind, ColorRole role = ColorRole::Text);
    // Icon drawn in a fixed color (session colors, command accents).
    static QIcon icon(IconKind kind, const QColor &color);

signals:
    void changed();

private:
    Theme() = default;
    QString styleSheet() const;

    Mode m_mode = Mode::Dark;
};
