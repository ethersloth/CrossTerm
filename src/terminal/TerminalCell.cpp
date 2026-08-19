#include "TerminalCell.h"

TerminalCell::TerminalCell(QChar character)
    : character(character)
{
}

void TerminalCell::reset()
{
    character = ' ';
    attributes = 0;
    foreground = 7;
    background = 0;
    isForegroundExtended = false;
    isBackgroundExtended = false;
}
