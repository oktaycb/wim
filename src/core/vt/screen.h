// wim - a Windows-native terminal workspace for coding agents.
// Core/vt: the screen buffer - a grid of cells with attributes plus a
// scrollback of lines that scrolled off the top. No cursor logic lives
// here; Terminal drives it.
#pragma once

#include "core/vt/attrs.h"

#include <deque>
#include <vector>

namespace wim::vt {

struct Cell {
    char32_t ch = U' ';
    Attr attr;
    uint8_t width = 1;      // 2 for East Asian wide characters
    bool wideContinuation = false; // this cell is the right half of one
};

using Line = std::vector<Cell>;

class ScreenBuffer {
public:
    ScreenBuffer(int cols, int rows);

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    // Resizes the grid. Content stays anchored to the top-left; rows lost
    // at the bottom are dropped, new rows are blank. When the buffer
    // shrinks rows that no longer fit, those lines move to the scrollback
    // only when 'pushToScrollback' is set (the alt screen never scrolls).
    void resize(int cols, int rows, bool pushToScrollback);

    Line* row(int y)
    {
        if (y < 0 || y >= rows_)
            return nullptr;
        dirty_ = true; // mutable access implies the row may change
        return &grid_[size_t(y)];
    }
    const Line* row(int y) const { return y >= 0 && y < rows_ ? &grid_[size_t(y)] : nullptr; }

    void clearAll(const Attr& background);
    void clearRow(int y, int fromCol, int toColExclusive, const Attr& background);

    // Scrolls the region [top, bottom] (inclusive rows) by 'lines'.
    // Lines leaving the region are lost; new lines are blank. Scrolling
    // the full region at the bottom edge pushes leaving lines into the
    // scrollback (main screen semantics - the caller decides via
    // scrollCaptures(); alt screen passes false).
    void scrollUp(int top, int bottom, int lines, bool scrollCaptures);
    void scrollDown(int top, int bottom, int lines);

    const std::deque<Line>& scrollback() const { return scrollback_; }
    size_t maxScrollback() const { return maxScrollback_; }
    void setMaxScrollback(size_t lines)
    {
        maxScrollback_ = lines;
        while (scrollback_.size() > maxScrollback_)
            scrollback_.pop_front();
    }

    // True when the buffer contents differ since the last markClean().
    bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

    static Line blankLine(int cols, const Attr& background);

private:
    void pushScrollback(Line line);

    int cols_ = 0;
    int rows_ = 0;
    std::vector<Line> grid_;
    std::deque<Line> scrollback_;
    size_t maxScrollback_ = 10000;
    bool dirty_ = false;
};

} // namespace wim::vt
