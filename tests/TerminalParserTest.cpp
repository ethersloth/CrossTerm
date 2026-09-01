#include <cassert>

#include "../src/terminal/TerminalParser.h"

int main()
{
    {
        TerminalScreen screen(5, 10);
        TerminalParser parser(screen);

        parser.processBytes(QByteArray("AB\nC"));

        assert(screen.cursor().row == 1);
        assert(screen.cursor().column == 2);
        assert(screen.cellAt(0, 0).character == QChar('A'));
        assert(screen.cellAt(0, 1).character == QChar('B'));
        assert(screen.cellAt(1, 2).character == QChar('C'));
    }

    {
        TerminalScreen screen(5, 10);
        TerminalParser parser(screen);

        parser.processBytes(QByteArray("\x1b[20h\r\n"));
        assert(screen.cursor().row == 1);
        assert(screen.cursor().column == 0);
    }

    {
        TerminalScreen screen(2, 4);
        screen.setScrollbackLimit(2);

        screen.setCellAt(0, 0, TerminalCell(QChar('A')));
        screen.scrollUp();
        screen.setCellAt(0, 0, TerminalCell(QChar('B')));
        screen.scrollUp();
        screen.setCellAt(0, 0, TerminalCell(QChar('C')));
        screen.scrollUp();

        assert(screen.scrollbackSize() == 2);
        assert(screen.scrollbackLine(0).at(0).character == QChar('B'));
        assert(screen.scrollbackLine(1).at(0).character == QChar('C'));

        screen.setScrollbackLimit(1);
        assert(screen.scrollbackSize() == 1);
        assert(screen.scrollbackLine(0).at(0).character == QChar('C'));
    }

    return 0;
}
