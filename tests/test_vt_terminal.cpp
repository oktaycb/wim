// Tests for vt/terminal.h: the emulator end-to-end over escape bytes.
#include "core/vt/terminal.h"
#include "test.h"

#include <string>
#include <vector>

using namespace wim::vt;

namespace {

struct Events : TerminalEvents {
    std::string title;
    std::string cwd;
    std::vector<std::string> notifies; // "title|body|finished"
    int bells = 0;
    std::vector<char> marks;
    std::vector<std::string> lines;

    void onTitle(const std::string& t) override { title = t; }
    void onCwd(const std::string& c) override { cwd = c; }
    void onNotify(const std::string& t, const std::string& b, bool fin) override
    {
        notifies.push_back(t + "|" + b + "|" + (fin ? "1" : "0"));
    }
    void onBell() override { ++bells; }
    void onPromptMark(char kind) override { marks.push_back(kind); }
    void onLine(const std::string& l) override { lines.push_back(l); }
};

struct Fixture {
    // Declaration order matters: events is constructed first so the
    // Terminal can hold a pointer to it.
    Events events;
    Terminal term{20, 5, &events};

    void write(const std::string& s) { term.write(s.data(), s.size()); }
};

} // namespace

WIM_TEST(TermPrintsAndWraps)
{
    Fixture f;
    f.write("hello");
    WIM_CHECK_STREQ(f.term.lineText(0), "hello");
    WIM_CHECK_EQ(f.term.cursorX(), 5);

    // Fill exactly to column 20 (5 + 10 + 5 chars).
    f.write("0123456789");
    WIM_CHECK_EQ(f.term.cursorX(), 15);
    f.write("67890");
    WIM_CHECK_EQ(f.term.cursorX(), 19); // parked at the last column
    WIM_CHECK_EQ(f.term.lineText(0).size(), size_t(20));
    // Wrap is deferred: the cursor holds until the next character.
    f.write("X");
    WIM_CHECK_STREQ(f.term.lineText(1), "X");
}

WIM_TEST(TermControls)
{
    Fixture f;
    f.write("ab\rX"); // CR overwrites
    WIM_CHECK_STREQ(f.term.lineText(0), "Xb");
    f.write("\r\nY"); // CRLF: next line starts at column 0
    WIM_CHECK_EQ(f.term.cursorY(), 1);
    WIM_CHECK_STREQ(f.term.lineText(1), "Y");
    // A bare LF is a pure line feed: the column stays (xterm behavior).
    f.write("\nZ");
    WIM_CHECK_STREQ(f.term.lineText(2), " Z");
    f.write("\b\b"); // two backspaces
    WIM_CHECK_EQ(f.term.cursorX(), 0);
    f.write("\x07"); // bell
    WIM_CHECK_EQ(f.events.bells, 1);
    f.write("\t");
    WIM_CHECK_EQ(f.term.cursorX(), 8); // tab stop
}

WIM_TEST(TermCsiCursor)
{
    Fixture f;
    f.write("\x1B[3;5H"); // CUP row 3 col 5
    WIM_CHECK_EQ(f.term.cursorY(), 2);
    WIM_CHECK_EQ(f.term.cursorX(), 4);
    f.write("abc");
    WIM_CHECK_STREQ(f.term.lineText(2), "    abc");
    WIM_CHECK_EQ(f.term.cursorX(), 7);
    f.write("\x1B[2A"); // up 2
    WIM_CHECK_EQ(f.term.cursorY(), 0);
    f.write("\x1B[10A"); // clamped at 0
    WIM_CHECK_EQ(f.term.cursorY(), 0);
    f.write("\x1B[5D\x1B[C"); // left 5 (clamped), right 1
    WIM_CHECK_EQ(f.term.cursorX(), 3);
    f.write("\x1B[H"); // home
    WIM_CHECK_EQ(f.term.cursorX(), 0);
    WIM_CHECK_EQ(f.term.cursorY(), 0);
}

WIM_TEST(TermErase)
{
    Fixture f;
    f.write("\x1B[5;1Habcdefgh");
    f.write("\x1B[5;4H"); // cursor back to col 4
    f.write("\x1B[4X"); // ECH: erase 4 chars at the cursor, no shift
    WIM_CHECK_STREQ(f.term.lineText(4), "abc    h"); // d,e,f,g erased
    f.write("\x1B[2J"); // clear all
    WIM_CHECK(f.term.lineText(4).empty());

    f.write("\x1B[1;1Hone two");
    f.write("\x1B[4D"); // left 4: cursor x 7 -> 3
    f.write("\x1B[1K"); // EL from start to cursor: clears "one"
    WIM_CHECK_STREQ(f.term.lineText(0), "    two"); // "one " erased, 4 leading spaces
}

WIM_TEST(TermScrollRegion)
{
    Fixture f;
    f.write("r1\r\nr2\r\nr3\r\nr4\r\nr5"); // fill 5 rows
    WIM_CHECK_STREQ(f.term.lineText(0), "r1");

    // Region rows 2-4; LF at row 4 scrolls only inside it.
    f.write("\x1B[2;4r"); // DECSTBM top=2 bottom=4, homes cursor
    WIM_CHECK_EQ(f.term.cursorY(), 0);
    f.write("\x1B[4;1H\n"); // cursor to row 4 (region bottom), LF
    WIM_CHECK_STREQ(f.term.lineText(0), "r1"); // outside region untouched
    WIM_CHECK_STREQ(f.term.lineText(1), "r3");
    WIM_CHECK_STREQ(f.term.lineText(3), "");
    WIM_CHECK_STREQ(f.term.lineText(4), "r5"); // outside region untouched

    // Full-region scroll pushes to scrollback.
    f.write("\x1B[r"); // reset margins
    f.write("\x1B[5;1H\n"); // bottom row, LF
    WIM_CHECK_EQ(f.term.screen().scrollback().size(), size_t(1));
}

WIM_TEST(TermInsertDeleteLines)
{
    Fixture f;
    f.write("a\r\nb\r\nc\r\nd\r\ne");
    f.write("\x1B[2;1H\x1B[2L"); // at row 2, insert 2 lines
    WIM_CHECK_STREQ(f.term.lineText(1), "");
    WIM_CHECK_STREQ(f.term.lineText(2), "");
    WIM_CHECK_STREQ(f.term.lineText(3), "b");
    f.write("\x1B[1;1H\x1B[1M"); // delete line 1
    WIM_CHECK_STREQ(f.term.lineText(0), "");
    WIM_CHECK_STREQ(f.term.lineText(1), "");
    WIM_CHECK_STREQ(f.term.lineText(2), "b");
}

WIM_TEST(TermAltScreen)
{
    Fixture f;
    f.write("main text");
    f.write("\x1B[?1049h"); // enter alt
    WIM_CHECK(f.term.inAltScreen());
    WIM_CHECK(f.term.lineText(0).empty());
    f.write("alt screen");
    WIM_CHECK_STREQ(f.term.lineText(0), "alt screen");
    f.write("\x1B[?1049l"); // leave
    WIM_CHECK(!f.term.inAltScreen());
    WIM_CHECK_STREQ(f.term.lineText(0), "main text");
    // Alt-screen scrolling never grows scrollback.
    WIM_CHECK(f.term.screen().scrollback().empty());
}

WIM_TEST(TermSgr)
{
    Fixture f;
    f.write("\x1B[1;38;5;196;48;2;10;20;30mX");
    // Directly inspectable through the buffer.
    const Cell& cell = (*f.term.screen().row(0))[0];
    WIM_CHECK(cell.attr.bold);
    WIM_CHECK_EQ(int(cell.attr.fg.index), 196);
    WIM_CHECK(cell.attr.fg.kind == Color::Kind::Indexed);
    WIM_CHECK(cell.attr.bg.kind == Color::Kind::Rgb);
    f.write("\x1B[0m");
    // Next char has default attrs.
    f.write("Y");
    const Cell& y = (*f.term.screen().row(0))[1];
    WIM_CHECK(!y.attr.bold);
    WIM_CHECK(y.attr.fg.kind == Color::Kind::Default);
}

WIM_TEST(TermModes)
{
    Fixture f;
    f.write("\x1B[?25l");
    WIM_CHECK(!f.term.cursorVisible());
    f.write("\x1B[?25h");
    WIM_CHECK(f.term.cursorVisible());
    f.write("\x1B[?2004h");
    WIM_CHECK(f.term.bracketedPaste());
    f.write("\x1B[?2004l");
    WIM_CHECK(!f.term.bracketedPaste());
    f.write("\x1B[?1000h\x1B[?1006h");
    WIM_CHECK(f.term.mouseReporting());
    f.write("\x1B[?1000l");
    WIM_CHECK(!f.term.mouseReporting());
}

WIM_TEST(TermWideChars)
{
    Fixture f; // 20 columns
    f.write("\xE6\xBC\xA2\xE5\xAD\x97"); // two CJK ideographs (4 columns)
    WIM_CHECK_STREQ(f.term.lineText(0), "\xE6\xBC\xA2\xE5\xAD\x97");
    WIM_CHECK_EQ(f.term.cursorX(), 4);
    const Line& row = *f.term.screen().row(0);
    WIM_CHECK_EQ(int(row[0].width), 2);
    WIM_CHECK(row[1].wideContinuation);

    // A wide char at the last column wraps instead of splitting.
    f.write("\x1B[1;20H");
    WIM_CHECK_EQ(f.term.cursorX(), 19);
    f.write("\xE6\xBC\xA2");
    WIM_CHECK_STREQ(f.term.lineText(1), "\xE6\xBC\xA2");
}

WIM_TEST(TermOscEvents)
{
    Fixture f;
    f.write("\x1B]0;my window\x07");
    WIM_CHECK_STREQ(f.events.title, "my window");
    f.write("\x1B]2;second title\x1B\\");
    WIM_CHECK_STREQ(f.events.title, "second title");

    f.write("\x1B]7;file:///C:/dev/wim\x07");
    WIM_CHECK_STREQ(f.events.cwd, "/C:/dev/wim");

    f.write("\x1B]9;build finished\x07");
    WIM_CHECK_EQ(f.events.notifies.size(), size_t(1));
    WIM_CHECK(f.events.notifies[0].find("build finished") != std::string::npos);

    f.write("\x1B]777;notify;Build;Done compiling\x07");
    WIM_CHECK_EQ(f.events.notifies.size(), size_t(2));
    WIM_CHECK(f.events.notifies[1].find("Build|Done compiling") != std::string::npos);

    f.write("\x1B]133;A\x07\x1B]133;C\x07");
    WIM_CHECK_EQ(f.events.marks.size(), size_t(2));
    WIM_CHECK_EQ(f.events.marks[0], 'A');
    WIM_CHECK_EQ(f.events.marks[1], 'C');
}

WIM_TEST(TermLineMirror)
{
    Fixture f;
    f.write("first\r\nsecond\r\nthird");
    WIM_CHECK_EQ(f.events.lines.size(), size_t(2)); // third not complete yet
    WIM_CHECK_STREQ(f.events.lines[0], "first");
    WIM_CHECK_STREQ(f.events.lines[1], "second");
}

WIM_TEST(TermResizeKeepsContent)
{
    Fixture f;
    f.write("stay");
    f.term.resize(10, 3);
    WIM_CHECK_STREQ(f.term.lineText(0), "stay");
    WIM_CHECK_EQ(f.term.cols(), 10);
    WIM_CHECK_EQ(f.term.rows(), 3);
}

WIM_TEST(TermApplicationCursorKeys)
{
    Fixture f;
    f.write("\x1B[?1h");
    WIM_CHECK(f.term.applicationCursorKeys());
    f.write("\x1B[?1l");
    WIM_CHECK(!f.term.applicationCursorKeys());
}
