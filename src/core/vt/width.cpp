#include "width.h"

#include <iterator>

namespace wim::vt {

namespace {

struct Range {
    char32_t lo, hi;
};

// Zero-width: combining marks, ZWJ/RLM family, variation selectors.
constexpr Range kZeroWidth[] = {
    {0x0300, 0x036F},   // combining diacritical marks
    {0x0483, 0x0489},   // Cyrillic combining
    {0x0591, 0x05BD},   // Hebrew points
    {0x0610, 0x061A},   // Arabic marks
    {0x064B, 0x065F},   // Arabic vowels
    {0x0670, 0x0670},
    {0x06D6, 0x06DC},   // Arabic signs
    {0x0730, 0x074A},
    {0x07A6, 0x07B0},
    {0x0E31, 0x0E31},   // Thai
    {0x0E34, 0x0E3A},
    {0x0E47, 0x0E4E},
    {0x0EB1, 0x0EB1},   // Lao
    {0x0EB4, 0x0EBC},
    {0x0EC8, 0x0ECD},
    {0x1AB0, 0x1AFF},
    {0x1DC0, 0x1DFF},
    {0x200B, 0x200F},   // ZWSP..RLM (incl. ZWJ 200D)
    {0x20D0, 0x20F0},   // combining symbols
    {0xFE00, 0xFE0F},   // variation selectors
    {0xFE20, 0xFE2F},   // combining half marks
    {0xFEFF, 0xFEFF},   // BOM
    {0xE0100, 0xE01EF}, // tag characters
};

// Double-width: CJK, Hangul, Kana, full-width forms, emoji.
constexpr Range kWide[] = {
    {0x1100, 0x115F},   // Hangul Jamo
    {0x2329, 0x232A},
    {0x2E80, 0x303E},   // CJK radicals, Kangxi
    {0x3041, 0x33FF},   // Kana, CJK composites
    {0x3400, 0x4DBF},   // CJK ext A
    {0x4E00, 0x9FFF},   // CJK unified
    {0xA000, 0xA4CF},   // Yi
    {0xA960, 0xA97F},   // Hangul Jamo extended-A
    {0xAC00, 0xD7A3},   // Hangul syllables
    {0xF900, 0xFAFF},   // CJK compatibility
    {0xFE10, 0xFE19},   // vertical forms
    {0xFE30, 0xFE6F},   // CJK compatibility forms
    {0xFF00, 0xFF60},   // full-width forms
    {0xFFE0, 0xFFE6},
    {0x16FE0, 0x16FE4},
    {0x17000, 0x18AFF}, // Tangut
    {0x1B000, 0x1B2FF}, // Kana supplements
    {0x1F004, 0x1F004},
    {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A},
    {0x1F200, 0x1F320}, // enclosed Ideographic supplement + emoji
    {0x1F32D, 0x1F335},
    {0x1F337, 0x1F37C},
    {0x1F37E, 0x1F393},
    {0x1F3A0, 0x1F3CA},
    {0x1F3CF, 0x1F3D3},
    {0x1F3E0, 0x1F3F0},
    {0x1F3F4, 0x1F3F4},
    {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440},
    {0x1F442, 0x1F4FC},
    {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E},
    {0x1F550, 0x1F567},
    {0x1F57A, 0x1F57A},
    {0x1F595, 0x1F596},
    {0x1F5A4, 0x1F5A4},
    {0x1F5FB, 0x1F64F},
    {0x1F680, 0x1F6C5},
    {0x1F6CC, 0x1F6CC},
    {0x1F6D0, 0x1F6D2},
    {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC},
    {0x1F7E0, 0x1F7EB},
    {0x1F90C, 0x1F93A},
    {0x1F93C, 0x1F945},
    {0x1F947, 0x1F978},
    {0x1F97A, 0x1F9CB},
    {0x1F9CD, 0x1F9FF},
    {0x1FA70, 0x1FAFF},
    {0x20000, 0x2FFFD}, // CJK ext B..F
    {0x30000, 0x3FFFD}, // CJK ext G+
};

bool inRanges(char32_t cp, const Range* ranges, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (cp >= ranges[i].lo && cp <= ranges[i].hi)
            return true;
    return false;
}

} // namespace

int charWidth(char32_t cp)
{
    if (cp < 0x20)
        return 0; // callers should not hit this
    if (inRanges(cp, kZeroWidth, std::size(kZeroWidth)))
        return 0;
    if (inRanges(cp, kWide, std::size(kWide)))
        return 2;
    return 1;
}

} // namespace wim::vt
