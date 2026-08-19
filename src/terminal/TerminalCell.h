#pragma once

#include <QString>
#include <cstdint>

/**
 * Represents a single cell in the terminal screen.
 * Contains character and attributes (color, bold, etc.)
 */
class TerminalCell
{
public:
    enum class Attribute : uint8_t {
        None = 0,
        Bold = 1 << 0,
        Dim = 1 << 1,
        Italic = 1 << 2,
        Underline = 1 << 3,
        Blink = 1 << 4,
        Reverse = 1 << 5,
        Hidden = 1 << 6,
        StrikeThrough = 1 << 7,
    };

    TerminalCell() = default;
    explicit TerminalCell(QChar character);

    QChar character = ' ';
    uint8_t attributes = 0;  // Bitmask of Attribute flags
    uint8_t foreground = 7;  // ANSI color (0-7, or 256-color index)
    uint8_t background = 0;  // ANSI color (0-7, or 256-color index)
    bool isForegroundExtended = false;  // True if using 256-color or RGB
    bool isBackgroundExtended = false;  // True if using 256-color or RGB

    void reset();
    bool hasAttribute(Attribute attr) const { return (attributes & static_cast<uint8_t>(attr)) != 0; }
    void setAttributes(uint8_t attr) { attributes = attr; }
    void addAttribute(Attribute attr) { attributes |= static_cast<uint8_t>(attr); }
    void removeAttribute(Attribute attr) { attributes &= ~static_cast<uint8_t>(attr); }
};
