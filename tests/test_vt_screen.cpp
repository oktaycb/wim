// Tests for vt/screen.h: grid, resize, scroll regions, scrollback.
#include "core/vt/screen.h"
#include "test.h"

using namespace wim::vt;

namespace {
std::string rowText(const ScreenBuffer& s, int y)
{
    std::string out;
    for (const Cell& c : *s.row(y)) {
        if (c.wideContinuation)
            continue;
        if (c.ch < 0x80) {
            out += char(c.ch);
        } else if (c.ch < 0x800) {
            out += char(0xC0 | (c.ch >> 6));
            out += char(0x80 | (c.ch & 0x3F));
        } else {
            out += char(0xE0 | (c.ch >> 12));
            out += char(0x80 | ((c.ch >> 6) & 0x3F));
            out += char(0x80 | (c.ch & 0x3F));
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}
} // namespace

WIM_TEST(ScreenGridStartsBlank)
{
    ScreenBuffer s(10, 4);
    WIM_CHECK_EQ(s.cols(), 10);
    WIM_CHECK_EQ(s.rows(), 4);
    for (int y = 0; y < 4; ++y)
        WIM_CHECK(rowText(s, y).find_first_not_of(" ") == std::string::npos);
    WIM_CHECK(s.row(4) == nullptr);
    WIM_CHECK(s.row(-1) == nullptr);

    s.row(1)->at(0).ch = U'x';
    WIM_CHECK_STREQ(rowText(s, 1), "x");
    WIM_CHECK(s.dirty());
    s.markClean();
    WIM_CHECK(!s.dirty());
}

WIM_TEST(ScreenResizeGrowsAndShrinks)
{
    ScreenBuffer s(4, 2);
    s.row(0)->at(0).ch = U'a';
    s.row(1)->at(0).ch = U'b';

    s.resize(6, 3, false);
    WIM_CHECK_EQ(s.cols(), 6);
    WIM_CHECK_EQ(s.rows(), 3);
    WIM_CHECK_STREQ(rowText(s, 0), "a");
    WIM_CHECK_STREQ(rowText(s, 1), "b");
    WIM_CHECK_STREQ(rowText(s, 2), ""); // all-blank row trims to empty

    s.resize(2, 2, false);
    WIM_CHECK_STREQ(rowText(s, 0), "a");
    WIM_CHECK_STREQ(rowText(s, 1), "b");
}

WIM_TEST(ScreenResizeShrinkPushesScrollback)
{
    ScreenBuffer s(4, 4);
    for (int y = 0; y < 4; ++y)
        s.row(y)->at(0).ch = char32_t(U'0' + y);

    s.resize(4, 2, true);
    // Top rows stay; the lost bottom rows went to scrollback.
    WIM_CHECK_STREQ(rowText(s, 0), "0");
    WIM_CHECK_STREQ(rowText(s, 1), "1");
    WIM_CHECK_EQ(s.scrollback().size(), size_t(2));
    WIM_CHECK_EQ(s.scrollback()[0][0].ch, U'2');
    WIM_CHECK_EQ(s.scrollback()[1][0].ch, U'3');

    // Alt screen (pushToScrollback=false) just drops them.
    ScreenBuffer alt(4, 4);
    for (int y = 0; y < 4; ++y)
        alt.row(y)->at(0).ch = char32_t(U'0' + y);
    alt.resize(4, 2, false);
    WIM_CHECK(alt.scrollback().empty());
}

WIM_TEST(ScreenScrollUpFullRegionCaptures)
{
    ScreenBuffer s(3, 3);
    s.row(0)->at(0).ch = U'a';
    s.row(1)->at(0).ch = U'b';
    s.row(2)->at(0).ch = U'c';

    s.scrollUp(0, 2, 1, true);
    WIM_CHECK_STREQ(rowText(s, 0), "b");
    WIM_CHECK_STREQ(rowText(s, 1), "c");
    WIM_CHECK_EQ(s.scrollback().size(), size_t(1));
    WIM_CHECK_EQ(s.scrollback()[0][0].ch, U'a');
}

WIM_TEST(ScreenScrollUpPartialRegionNoCapture)
{
    ScreenBuffer s(3, 4);
    s.row(0)->at(0).ch = U't'; // above the region: untouched
    s.row(1)->at(0).ch = U'a';
    s.row(2)->at(0).ch = U'b';
    s.row(3)->at(0).ch = U'u'; // below the region: untouched

    s.scrollUp(1, 2, 1, true);
    WIM_CHECK_STREQ(rowText(s, 0), "t");
    WIM_CHECK_STREQ(rowText(s, 1), "b");
    WIM_CHECK_STREQ(rowText(s, 3), "u");
    WIM_CHECK(s.scrollback().empty());
}

WIM_TEST(ScreenScrollDownPushesBlankIn)
{
    ScreenBuffer s(3, 3);
    s.row(0)->at(0).ch = U'a';
    s.row(1)->at(0).ch = U'b';
    s.row(2)->at(0).ch = U'c';

    s.scrollDown(0, 2, 1);
    WIM_CHECK(rowText(s, 0).empty());
    WIM_CHECK_STREQ(rowText(s, 1), "a");
    WIM_CHECK_STREQ(rowText(s, 2), "b");
    WIM_CHECK(s.scrollback().empty());
}

WIM_TEST(ScreenClearRowHandlesWideSplit)
{
    ScreenBuffer s(6, 1);
    Cell wide;
    wide.ch = U'\x4E00'; // a CJK ideograph
    wide.width = 2;
    s.row(0)->at(1) = wide;
    s.row(0)->at(2).wideContinuation = true;
    s.row(0)->at(4).ch = U'z';

    // Erasing columns 1..3 removes the wide char entirely.
    s.clearRow(0, 1, 3, Attr{});
    WIM_CHECK_EQ((*s.row(0))[0].ch, U' ');
    WIM_CHECK_EQ((*s.row(0))[1].ch, U' ');
    WIM_CHECK_EQ((*s.row(0))[2].ch, U' ');
    WIM_CHECK_EQ((*s.row(0))[4].ch, U'z');
}

WIM_TEST(ScreenScrollbackCap)
{
    ScreenBuffer s(2, 1);
    s.setMaxScrollback(3);
    for (int i = 0; i < 5; ++i) {
        s.row(0)->at(0).ch = char32_t(U'0' + i);
        s.scrollUp(0, 0, 1, true);
    }
    WIM_CHECK_EQ(s.scrollback().size(), size_t(3));
    WIM_CHECK_EQ(s.scrollback()[0][0].ch, U'2');
    WIM_CHECK_EQ(s.scrollback()[2][0].ch, U'4');
}
