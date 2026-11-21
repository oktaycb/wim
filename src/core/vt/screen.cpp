#include "screen.h"

namespace wim::vt {

ScreenBuffer::ScreenBuffer(int cols, int rows) : cols_(cols), rows_(rows)
{
    if (cols_ < 1)
        cols_ = 1;
    if (rows_ < 1)
        rows_ = 1;
    grid_.assign(size_t(rows_), blankLine(cols_, Attr{}));
}

Line ScreenBuffer::blankLine(int cols, const Attr& background)
{
    Line line{size_t(cols)};
    for (auto& cell : line)
        cell.attr = background;
    return line;
}

void ScreenBuffer::clearAll(const Attr& background)
{
    for (auto& line : grid_)
        line = blankLine(cols_, background);
    dirty_ = true;
}

void ScreenBuffer::clearRow(int y, int fromCol, int toColExclusive, const Attr& background)
{
    Line* line = row(y);
    if (!line)
        return;
    if (fromCol < 0)
        fromCol = 0;
    if (toColExclusive > cols_)
        toColExclusive = cols_;
    for (int x = fromCol; x < toColExclusive; ++x) {
        Cell& cell = (*line)[size_t(x)];
        // A wide character's left half is erased together with its right.
        if (cell.wideContinuation && x > 0 && (*line)[size_t(x - 1)].width == 2) {
            (*line)[size_t(x - 1)] = Cell{};
            (*line)[size_t(x - 1)].attr = background;
        }
        cell = Cell{};
        cell.attr = background;
    }
    dirty_ = true;
}

void ScreenBuffer::pushScrollback(Line line)
{
    scrollback_.push_back(std::move(line));
    while (scrollback_.size() > maxScrollback_)
        scrollback_.pop_front();
}

void ScreenBuffer::resize(int cols, int rows, bool pushToScrollback)
{
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    if (cols == cols_ && rows == rows_)
        return;

    // Rows that fall off the bottom go to the scrollback (main screen) or
    // are dropped (alt screen) when shrinking height; the visible grid
    // keeps the TOP rows' content, which is what terminals do.
    if (rows < rows_) {
        const int lost = rows_ - rows;
        if (pushToScrollback) {
            // Keep the newest content: the bottom 'lost' rows leave the
            // screen only when the alternative would discard them. Standard
            // behavior keeps the TOP anchored, so the bottom rows are lost
            // to scrollback.
            for (int y = rows_ - lost; y < rows_; ++y)
                pushScrollback(std::move(grid_[size_t(y)]));
        }
    }

    std::vector<Line> next{size_t(rows)};
    const int copyRows = rows < rows_ ? rows : rows_;
    for (int y = 0; y < copyRows; ++y) {
        Line line = std::move(grid_[size_t(y)]);
        line.resize(size_t(cols), Cell{});
        // A wide char split at the new right edge loses its right half.
        for (int x = cols - 1; x >= 0; --x) {
            if (line[size_t(x)].width == 2 && x == cols - 1) {
                line[size_t(x)] = Cell{};
                break;
            }
            if (!line[size_t(x)].wideContinuation)
                break;
        }
        next[size_t(y)] = std::move(line);
    }
    for (int y = copyRows; y < rows; ++y)
        next[size_t(y)] = blankLine(cols, Attr{});
    grid_ = std::move(next);
    cols_ = cols;
    rows_ = rows;
    dirty_ = true;
}

void ScreenBuffer::scrollUp(int top, int bottom, int lines, bool scrollCaptures)
{
    if (top < 0)
        top = 0;
    if (bottom >= rows_)
        bottom = rows_ - 1;
    if (top > bottom || lines <= 0)
        return;
    if (lines > bottom - top + 1)
        lines = bottom - top + 1;

    for (int i = 0; i < lines; ++i) {
        if (scrollCaptures && top == 0)
            pushScrollback(std::move(grid_[size_t(top)]));
        grid_.erase(grid_.begin() + top);
        Line blank = blankLine(cols_, Attr{});
        grid_.insert(grid_.begin() + bottom, std::move(blank));
    }
    dirty_ = true;
}

void ScreenBuffer::scrollDown(int top, int bottom, int lines)
{
    if (top < 0)
        top = 0;
    if (bottom >= rows_)
        bottom = rows_ - 1;
    if (top > bottom || lines <= 0)
        return;
    if (lines > bottom - top + 1)
        lines = bottom - top + 1;

    for (int i = 0; i < lines; ++i) {
        grid_.erase(grid_.begin() + bottom);
        Line blank = blankLine(cols_, Attr{});
        grid_.insert(grid_.begin() + top, std::move(blank));
    }
    dirty_ = true;
}

} // namespace wim::vt
