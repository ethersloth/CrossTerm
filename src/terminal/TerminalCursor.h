#pragma once

/**
 * Represents the terminal cursor state.
 * Tracks position, visibility, and blinking.
 */
class TerminalCursor
{
public:
    TerminalCursor() = default;

    int row = 0;
    int column = 0;
    bool visible = true;
    bool blinking = true;

    void moveTo(int r, int c) { row = r; column = c; }
    void moveRight(int delta = 1) { column += delta; }
    void moveLeft(int delta = 1) { column -= delta; }
    void moveUp(int delta = 1) { row -= delta; }
    void moveDown(int delta = 1) { row += delta; }
    void moveToColumn(int c) { column = c; }
    void moveToRow(int r) { row = r; }
    void reset() 
    { 
        row = 0; 
        column = 0; 
        visible = true; 
        blinking = true; 
    }
};
