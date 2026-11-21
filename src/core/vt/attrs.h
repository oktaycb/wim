// wim - a Windows-native terminal workspace for coding agents.
// Core/vt: cell attributes. A Pen is the current drawing state mutated by
// SGR; a Cell stamps a Pen's visual essence plus its character.
#pragma once

#include <cstdint>

namespace wim::vt {

// A color: default, an ANSI palette index (0-255), or direct RGB.
struct Color {
    enum class Kind : uint8_t { Default, Indexed, Rgb };
    Kind kind = Kind::Default;
    uint8_t index = 0;              // Kind::Indexed
    uint8_t r = 0, g = 0, b = 0;    // Kind::Rgb

    static Color def() { return {}; }
    static Color indexed(uint8_t i) { return Color{Kind::Indexed, i, 0, 0, 0}; }
    static Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{Kind::Rgb, 0, r, g, b}; }
    bool operator==(const Color& o) const
    {
        if (kind != o.kind)
            return false;
        if (kind == Kind::Indexed)
            return index == o.index;
        if (kind == Kind::Rgb)
            return r == o.r && g == o.g && b == o.b;
        return true;
    }
};

// Everything one cell can carry besides the character.
struct Attr {
    Color fg;
    Color bg;
    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;
    bool blink = false;
    bool reverse = false;
    bool invisible = false;
    bool strikethrough = false;

    bool operator==(const Attr& o) const
    {
        return fg == o.fg && bg == o.bg && bold == o.bold && faint == o.faint &&
               italic == o.italic && underline == o.underline && blink == o.blink &&
               reverse == o.reverse && invisible == o.invisible &&
               strikethrough == o.strikethrough;
    }
    bool operator!=(const Attr& o) const { return !(*this == o); }
};

// The current SGR state.
struct Pen {
    Attr attr;
    bool defaultFg = true;
    bool defaultBg = true;
};

} // namespace wim::vt
