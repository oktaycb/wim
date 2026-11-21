#include "terminal.h"
#include "core/util/str.h"
#include "width.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wim::vt {

Terminal::Terminal(int cols, int rows, TerminalEvents* events)
    : main_(cols, rows), alt_(cols, rows), events_(events)
{
    regionBottom_ = rows - 1;
    parser_.setSink(this);
}

void Terminal::write(const char* data, size_t len)
{
    parser_.feed(data, len);
}

void Terminal::reset()
{
    inAlt_ = false;
    active_ = &main_;
    cursor_ = Cursor{};
    pen_ = Pen{};
    regionTop_ = 0;
    regionBottom_ = main_.rows() - 1;
    originMode_ = false;
    autoWrap_ = true;
    bracketedPaste_ = false;
    applicationCursorKeys_ = false;
    mouseMode_ = MouseMode::Off;
    mouseSgr_ = false;
    scrollOffset_ = 0;
    main_.clearAll(Attr{});
    damage_ = true;
}

void Terminal::resize(int cols, int rows)
{
    if (cols < 1 || rows < 1)
        return;
    if (cols == main_.cols() && rows == main_.rows())
        return;
    main_.resize(cols, rows, true);
    alt_.resize(cols, rows, false);
    regionTop_ = 0;
    regionBottom_ = rows - 1;
    cursor_.x = std::min(cursor_.x, cols - 1);
    cursor_.y = std::min(cursor_.y, rows - 1);
    cursor_.pendingWrap = false;
    if (scrollOffset_ > (int)main_.scrollback().size())
        scrollOffset_ = (int)main_.scrollback().size();
    damage_ = true;
}

void Terminal::setScrollOffset(int lines)
{
    const int maxOffset = (int)main_.scrollback().size();
    scrollOffset_ = std::clamp(lines, 0, maxOffset);
    damage_ = true;
}

// ---- printing -----------------------------------------------------------------

void Terminal::print(char32_t ch)
{
    const int width = charWidth(ch);
    if (width == 0)
        return; // combining marks and ZW* are laid over the previous cell;
                // v1 drops them rather than storing grapheme clusters

    if (cursor_.pendingWrap && autoWrap_) {
        cursor_.x = 0;
        linefeed();
        cursor_.pendingWrap = false;
    }

    if (cursor_.x + width > active_->cols()) {
        if (autoWrap_) {
            cursor_.x = 0;
            linefeed();
        } else {
            cursor_.x = active_->cols() - width; // overwrite the last cell
        }
    }

    Line* row = active_->row(cursor_.y);
    if (!row)
        return;
    Cell& cell = (*row)[size_t(cursor_.x)];
    cell.ch = ch;
    cell.attr = pen_.attr;
    cell.width = uint8_t(width);
    cell.wideContinuation = false;
    if (width == 2 && cursor_.x + 1 < active_->cols()) {
        Cell& right = (*row)[size_t(cursor_.x + 1)];
        right = Cell{};
        right.attr = pen_.attr;
        right.wideContinuation = true;
    }
    lastChar_ = ch;
    cursor_.x += width;
    if (cursor_.x >= active_->cols()) {
        cursor_.x = active_->cols() - 1;
        cursor_.pendingWrap = true;
    }
    damage_ = true;
}

// ---- C0 ------------------------------------------------------------------------

void Terminal::execute(unsigned char control)
{
    switch (control) {
    case 0x07: // BEL
        if (events_)
            events_->onBell();
        damage_ = true; // a bell may flip an attention indicator
        return;
    case 0x08: // BS
        cursor_.pendingWrap = false;
        if (cursor_.x > 0)
            --cursor_.x;
        damage_ = true;
        return;
    case 0x09: { // HT: next tab stop, never past the last column
        const int next = (cursor_.x / tabWidth_ + 1) * tabWidth_;
        cursor_.x = std::min(next, active_->cols() - 1);
        damage_ = true;
        return;
    }
    case 0x0A:   // LF
    case 0x0B:   // VT
    case 0x0C:   // FF
        linefeed();
        return;
    case 0x0D: // CR
        cursor_.x = 0;
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 0x0E: // SO
    case 0x0F: // SI
        return; // charsets are not tracked
    default:
        return;
    }
}

void Terminal::linefeed()
{
    if (events_ && !inAlt_) {
        // The line the cursor is leaving is complete - tell the mirror.
        if (const Line* row = active_->row(cursor_.y))
            events_->onLine(lineText(cursor_.y));
    }
    if (cursor_.y == regionBottom_) {
        active_->scrollUp(regionTop_, regionBottom_, 1, !inAlt_);
    } else if (cursor_.y < active_->rows() - 1) {
        ++cursor_.y;
    }
    cursor_.pendingWrap = false;
    damage_ = true;
}

void Terminal::reverseLinefeed()
{
    if (cursor_.y == regionTop_) {
        active_->scrollDown(regionTop_, regionBottom_, 1);
    } else if (cursor_.y > 0) {
        --cursor_.y;
    }
    cursor_.pendingWrap = false;
    damage_ = true;
}

// ---- ESC ------------------------------------------------------------------------

void Terminal::escDispatch(const char* /*intermediates*/, int count, char finalByte)
{
    if (count > 0)
        return; // charset selection and friends: ignored
    switch (finalByte) {
    case 'D': linefeed(); return;               // IND
    case 'M': reverseLinefeed(); return;         // RI
    case 'E': cursor_.x = 0; linefeed(); return; // NEL
    case '7': saveCursor(); return;              // DECSC
    case '8': restoreCursor(); return;           // DECRC
    case 'c': reset(); return;                   // RIS
    case '=':
    case '>':
        return; // keypad modes: the input side encodes what apps expect
    default:
        return;
    }
}

// ---- CSI ------------------------------------------------------------------------

void Terminal::csiDispatch(const CsiSequence& seq)
{
    handleCsi(seq);
}

void Terminal::handleCsi(const CsiSequence& seq)
{
    const int p1 = seq.param(0, 1); // most sequences default to 1
    switch (seq.finalByte) {
    case 'A': // CUU
        cursor_.y = std::max(regionTop_, cursor_.y - std::max(1, p1));
        if (cursor_.y > regionBottom_)
            cursor_.y = 0;
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'B': // CUD
    case 'e': // VPR
        cursor_.y = std::min(regionBottom_, cursor_.y + std::max(1, p1));
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'C': // CUF
    case 'a': // HPR
        cursor_.x = std::min(active_->cols() - 1, cursor_.x + std::max(1, p1));
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'D': // CUB
        cursor_.x = std::max(0, cursor_.x - std::max(1, p1));
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'E': // CNL
        cursor_.y = std::min(regionBottom_, cursor_.y + std::max(1, p1));
        cursor_.x = 0;
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'F': // CPL
        cursor_.y = std::max(regionTop_, cursor_.y - std::max(1, p1));
        cursor_.x = 0;
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'G': // CHA
    case '`': // HPA
        cursor_.x = std::clamp(p1 - 1, 0, active_->cols() - 1);
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'd': // VPA
        cursor_.y = std::clamp(p1 - 1, 0, active_->rows() - 1);
        cursor_.pendingWrap = false;
        damage_ = true;
        return;
    case 'H': // CUP
    case 'f': // HVP
        moveCursor(seq.param(1, 1) - 1, seq.param(0, 1) - 1);
        return;
    case 'J': { // ED
        const int mode = seq.param(0, 0);
        if (mode == 0) {
            blankRowRange(cursor_.y, cursor_.x, active_->cols());
        } else if (mode == 1) {
            blankRowRange(cursor_.y, 0, std::min(cursor_.x + 1, active_->cols()));
        } else if (mode == 2 || mode == 3) {
            active_->clearAll(pen_.attr);
            if (mode == 3)
                main_.setMaxScrollback(main_.maxScrollback()); // drop history
        }
        damage_ = true;
        return;
    }
    case 'K': { // EL
        const int mode = seq.param(0, 0);
        if (mode == 0)
            blankRowRange(cursor_.y, cursor_.x, active_->cols());
        else if (mode == 1)
            blankRowRange(cursor_.y, 0, std::min(cursor_.x + 1, active_->cols()));
        else
            blankRowRange(cursor_.y, 0, active_->cols());
        damage_ = true;
        return;
    }
    case 'L': { // IL: shift rows down inside the region, blanks at the cursor
        if (cursor_.y < regionTop_ || cursor_.y > regionBottom_)
            return;
        const int lines = std::min(std::max(1, p1), regionBottom_ - cursor_.y + 1);
        active_->scrollDown(cursor_.y, regionBottom_, lines);
        cursor_.x = 0;
        damage_ = true;
        return;
    }
    case 'M': { // DL: shift rows up inside the region
        if (cursor_.y < regionTop_ || cursor_.y > regionBottom_)
            return;
        const int lines = std::min(std::max(1, p1), regionBottom_ - cursor_.y + 1);
        active_->scrollUp(cursor_.y, regionBottom_, lines, false);
        cursor_.x = 0;
        damage_ = true;
        return;
    }
    case '@': { // ICH
        Line* row = active_->row(cursor_.y);
        if (!row)
            return;
        const int n = std::min(std::max(1, p1), active_->cols() - cursor_.x);
        row->insert(row->begin() + cursor_.x, size_t(n), Cell{});
        row->resize(size_t(active_->cols()));
        damage_ = true;
        return;
    }
    case 'P': { // DCH
        Line* row = active_->row(cursor_.y);
        if (!row)
            return;
        const int n = std::min(std::max(1, p1), active_->cols() - cursor_.x);
        row->erase(row->begin() + cursor_.x, row->begin() + cursor_.x + n);
        row->resize(size_t(active_->cols()), Cell{});
        damage_ = true;
        return;
    }
    case 'X': { // ECH
        const int n = std::min(std::max(1, p1), active_->cols() - cursor_.x);
        blankRowRange(cursor_.y, cursor_.x, cursor_.x + n);
        damage_ = true;
        return;
    }
    case 'S': // SU
        active_->scrollUp(regionTop_, regionBottom_, std::max(1, p1), !inAlt_);
        damage_ = true;
        return;
    case 'T': // SD
        active_->scrollDown(regionTop_, regionBottom_, std::max(1, p1));
        damage_ = true;
        return;
    case 'r': { // DECSTBM
        int top = seq.param(0, 1) - 1;
        int bottom = seq.param(1, active_->rows()) - 1;
        top = std::clamp(top, 0, active_->rows() - 1);
        bottom = std::clamp(bottom, 0, active_->rows() - 1);
        if (top < bottom) {
            regionTop_ = top;
            regionBottom_ = bottom;
        }
        moveCursor(0, 0); // cursor homes (respects origin mode)
        return;
    }
    case 'm': // SGR
        applySgr(seq);
        damage_ = true;
        return;
    case 'h': // DECSET (private) or SM
        setMode(seq, true);
        return;
    case 'l': // DECRST or RM
        setMode(seq, false);
        return;
    case 'b': { // REP: repeat the last printed character (progress bars)
        if (lastChar_ == 0)
            return;
        const int n = std::max(1, p1);
        for (int i = 0; i < n; ++i)
            print(lastChar_);
        return;
    }
    default:
        return; // ' q' cursor style, print queries, window ops: ignored
    }
}

void Terminal::moveCursor(int x, int y)
{
    if (originMode_) {
        y += regionTop_;
        y = std::clamp(y, regionTop_, regionBottom_);
    } else {
        y = std::clamp(y, 0, active_->rows() - 1);
    }
    x = std::clamp(x, 0, active_->cols() - 1);
    cursor_.x = x;
    cursor_.y = y;
    cursor_.pendingWrap = false;
    damage_ = true;
}

void Terminal::blankRowRange(int y, int fromX, int toX)
{
    active_->clearRow(y, fromX, toX, pen_.attr);
}

// ---- SGR ------------------------------------------------------------------------

void Terminal::applySgr(const CsiSequence& seq)
{
    Attr& a = pen_.attr;
    if (seq.paramCount == 0) {
        pen_ = Pen{};
        return;
    }
    for (int i = 0; i < seq.paramCount; ++i) {
        const int p = seq.param(i, 0);
        switch (p) {
        case 0: pen_ = Pen{}; a = pen_.attr; break;
        case 1: a.bold = true; break;
        case 2: a.faint = true; break;
        case 3: a.italic = true; break;
        case 4: a.underline = true; break;
        case 5: a.blink = true; break;
        case 7: a.reverse = true; break;
        case 8: a.invisible = true; break;
        case 9: a.strikethrough = true; break;
        case 21:
        case 22: a.bold = false; a.faint = false; break;
        case 23: a.italic = false; break;
        case 24: a.underline = false; break;
        case 25: a.blink = false; break;
        case 27: a.reverse = false; break;
        case 28: a.invisible = false; break;
        case 29: a.strikethrough = false; break;
        case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
            a.fg = Color::indexed(uint8_t(p - 30)); break;
        case 38:
        case 48: {
            Color color;
            const int kind = seq.param(i + 1, 0);
            if (kind == 5 && i + 2 < seq.paramCount + 0) {
                const int idx = seq.param(i + 2, 0);
                if (idx >= 0 && idx <= 255)
                    color = Color::indexed(uint8_t(idx));
                i += 2;
            } else if (kind == 2) {
                color = Color::rgb(uint8_t(seq.param(i + 2, 0)), uint8_t(seq.param(i + 3, 0)),
                                   uint8_t(seq.param(i + 4, 0)));
                i += 4;
            } else {
                // Unknown color space: skip its arguments per xterm.
                i = seq.paramCount;
            }
            if (p == 38)
                a.fg = color;
            else
                a.bg = color;
            break;
        }
        case 39: a.fg = Color::def(); break;
        case 49: a.bg = Color::def(); break;
        case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
            a.fg = Color::indexed(uint8_t(p - 90 + 8)); break;
        case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
            a.bg = Color::indexed(uint8_t(p - 100 + 8)); break;
        default: break;
        }
    }
    pen_.attr = a;
}

// ---- modes ----------------------------------------------------------------------

void Terminal::setMode(const CsiSequence& seq, bool enable)
{
    if (seq.privateMarker == '?') {
        switch (seq.param(0, 0)) {
        case 1:
            applicationCursorKeys_ = enable;
            return;
        case 6:
            originMode_ = enable;
            moveCursor(0, 0);
            return;
        case 7:
            autoWrap_ = enable;
            if (!enable)
                cursor_.pendingWrap = false;
            return;
        case 25:
            cursorVisible_ = enable;
            damage_ = true;
            return;
        case 47:
        case 1047:
            switchScreen(enable);
            return;
        case 1048:
            if (enable)
                saveCursor();
            else
                restoreCursor();
            return;
        case 1049:
            if (enable) {
                saveCursor();
                switchScreen(true);
                alt_.clearAll(Attr{});
            } else {
                switchScreen(false);
                restoreCursor();
            }
            return;
        case 1000:
            mouseMode_ = enable ? MouseMode::Press : MouseMode::Off;
            if (events_)
                events_->onInputModesChanged();
            return;
        case 1002:
            mouseMode_ = enable ? MouseMode::Drag : MouseMode::Off;
            if (events_)
                events_->onInputModesChanged();
            return;
        case 1003:
            mouseMode_ = enable ? MouseMode::Any : MouseMode::Off;
            if (events_)
                events_->onInputModesChanged();
            return;
        case 1006:
            mouseSgr_ = enable;
            if (events_)
                events_->onInputModesChanged();
            return;
        case 2004:
            bracketedPaste_ = enable;
            if (events_)
                events_->onInputModesChanged();
            return;
        default:
            return;
        }
    }
    // Non-private SM/RM (IRM insert mode, LNM newline mode): not tracked.
}

void Terminal::switchScreen(bool toAlt)
{
    if (toAlt == inAlt_)
        return;
    inAlt_ = toAlt;
    active_ = inAlt_ ? &alt_ : &main_;
    cursor_ = Cursor{};
    scrollOffset_ = 0;
    damage_ = true;
}

void Terminal::saveCursor()
{
    savedCursor_ = cursor_;
    savedPenAttr_ = pen_.attr;
}

void Terminal::restoreCursor()
{
    cursor_ = savedCursor_;
    pen_.attr = savedPenAttr_;
    cursor_.x = std::clamp(cursor_.x, 0, active_->cols() - 1);
    cursor_.y = std::clamp(cursor_.y, 0, active_->rows() - 1);
    cursor_.pendingWrap = false;
    damage_ = true;
}

// ---- OSC ------------------------------------------------------------------------

void Terminal::oscPut(const char* data, size_t len)
{
    // The parser already bounds the payload; keep a small extra cap.
    if (osc_.size() + len <= 16384)
        osc_.append(data, len);
}

void Terminal::oscEnd(bool)
{
    handleOsc(osc_);
    osc_.clear();
}

void Terminal::handleOsc(const std::string& payload)
{
    if (payload.empty() || !events_)
        return;
    const size_t semi = payload.find(';');
    const std::string code = payload.substr(0, semi);
    const std::string rest = semi == std::string::npos ? "" : payload.substr(semi + 1);

    if (code == "0" || code == "2") {
        events_->onTitle(rest);
    } else if (code == "7") {
        // file://host/path, percent-decoded; localhost stripped.
        std::string path = rest;
        if (str::istartsWith(path, "file://")) {
            path = path.substr(7);
            const size_t slash = path.find('/');
            if (slash != std::string::npos)
                path = path.substr(slash);
        }
        // Percent-decode.
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                const auto hex = [](char c) {
                    return c >= '0' && c <= '9' ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                };
                const int hi = hex(path[i + 1]), lo = hex(path[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    decoded += char(hi * 16 + lo);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }
        events_->onCwd(decoded);
    } else if (code == "9") {
        // iTerm2: "9;message" attention; "9;4;state;progress" ignored.
        if (str::startsWith(rest, "4;"))
            return;
        events_->onNotify("", rest, false);
    } else if (code == "99") {
        // Kitty: 99;i=..;p=..;d=... - surface the d= payload as the body.
        std::string body;
        for (const auto part : str::split(rest, ';')) {
            if (str::startsWith(part, "d="))
                body = part.substr(2);
        }
        if (!body.empty())
            events_->onNotify("", body, false);
    } else if (code == "777") {
        // rxvt: 777;notify;Title;Body
        const auto parts = str::split(rest, ';');
        if (parts.size() >= 1 && str::iequals(str::trim(parts[0]), "notify")) {
            const std::string title = parts.size() > 1 ? std::string(parts[1]) : "";
            const std::string body = parts.size() > 2 ? std::string(parts[2]) : "";
            events_->onNotify(title, body, false);
        }
    } else if (code == "133") {
        if (!rest.empty())
            events_->onPromptMark(rest[0]);
    }
}

// ---- text access ------------------------------------------------------------------

std::string Terminal::encodeLine(const Line& line)
{
    std::string out;
    for (const Cell& c : line) {
        if (c.wideContinuation)
            continue;
        if (c.ch < 0x80) {
            out += char(c.ch);
        } else if (c.ch < 0x800) {
            out += char(0xC0 | (c.ch >> 6));
            out += char(0x80 | (c.ch & 0x3F));
        } else if (c.ch < 0x10000) {
            out += char(0xE0 | (c.ch >> 12));
            out += char(0x80 | ((c.ch >> 6) & 0x3F));
            out += char(0x80 | (c.ch & 0x3F));
        } else {
            out += char(0xF0 | (c.ch >> 18));
            out += char(0x80 | ((c.ch >> 12) & 0x3F));
            out += char(0x80 | ((c.ch >> 6) & 0x3F));
            out += char(0x80 | (c.ch & 0x3F));
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string Terminal::lineText(int y) const
{
    if (y < 0 || y >= active_->rows())
        return {};
    return encodeLine(*active_->row(y));
}

std::string Terminal::altLineText(int y) const
{
    if (y < 0 || y >= alt_.rows())
        return {};
    return encodeLine(*alt_.row(y));
}

} // namespace wim::vt
