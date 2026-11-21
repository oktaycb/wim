// Tests for input key translation: key events -> VT bytes.
#include "core/input/keytrans.h"
#include "test.h"

using namespace wim::input;

namespace {
std::string key(unsigned vk, wchar_t ch, bool c = false, bool a = false, bool s = false)
{
    KeyEvent e;
    e.vk = vk;
    e.ch = ch;
    e.ctrl = c;
    e.alt = a;
    e.shift = s;
    return encodeKey(e, KeyModes{});
}
std::string keyApp(unsigned vk, wchar_t ch)
{
    KeyEvent e;
    e.vk = vk;
    e.ch = ch;
    KeyModes m;
    m.applicationCursorKeys = true;
    return encodeKey(e, m);
}
} // namespace

#include <windows.h>

WIM_TEST(KeyTextAndControls)
{
    WIM_CHECK(key(0x41, L'a') == "a"); // plain 'a'
    WIM_CHECK(key(0x41, L'A', false, false, true) == "A");
    WIM_CHECK(key(VK_RETURN, 0) == "\r");
    WIM_CHECK(key(VK_BACK, 0) == "\x7F");
    WIM_CHECK(key(VK_BACK, 0, true) == "\x08"); // Ctrl+Backspace
    WIM_CHECK(key(VK_TAB, 0) == "\t");
    KeyEvent shiftTab;
    shiftTab.vk = VK_TAB;
    shiftTab.shift = true;
    WIM_CHECK(encodeKey(shiftTab, KeyModes{}) == "\x1B[Z");
    WIM_CHECK(key(VK_ESCAPE, 0) == "\x1B");
    // Non-ASCII text goes through as UTF-8 (BMP; wider codepoints arrive
    // as surrogate pairs of WM_CHAR, each encoded separately).
    WIM_CHECK(key(0, 0x131) == "\xC4\xB1");   // i-dotless
    WIM_CHECK(key(0, 0x2603) == "\xE2\x98\x83"); // snowman
}

WIM_TEST(KeyCtrlAndAlt)
{
    WIM_CHECK(key(0x41, L'a', true) == "\x01"); // Ctrl+A
    WIM_CHECK(key(0x43, L'c', true) == "\x03"); // Ctrl+C
    WIM_CHECK(key(0x5A, L'z', true) == "\x1A"); // Ctrl+Z
    WIM_CHECK(key(VK_SPACE, 0, true) == std::string("\x00", 1));
    // Alt prefixes with ESC.
    WIM_CHECK(key(0x58, L'x', false, true) == "\x1Bx");
    KeyEvent up;
    up.vk = VK_UP;
    up.pressed = false;
    WIM_CHECK(encodeKey(up, KeyModes{}).empty()); // key-ups encode nothing
}

WIM_TEST(KeyArrowsAndModes)
{
    WIM_CHECK(key(VK_UP, 0) == "\x1B[A");
    WIM_CHECK(key(VK_DOWN, 0) == "\x1B[B");
    WIM_CHECK(key(VK_RIGHT, 0) == "\x1B[C");
    WIM_CHECK(key(VK_LEFT, 0) == "\x1B[D");
    WIM_CHECK(keyApp(VK_UP, 0) == "\x1BOA"); // application cursor mode
    WIM_CHECK(keyApp(VK_END, 0) == "\x1BOF");
    // Modified arrows carry the xterm modifier parameter (ctrl = 5).
    KeyEvent ctrlUp;
    ctrlUp.vk = VK_UP;
    ctrlUp.ctrl = true;
    WIM_CHECK(encodeKey(ctrlUp, KeyModes{}) == "\x1B[1;5A");
    KeyEvent shiftHome;
    shiftHome.vk = VK_HOME;
    shiftHome.shift = true;
    WIM_CHECK(encodeKey(shiftHome, KeyModes{}) == "\x1B[1;2H");
}

WIM_TEST(KeyEditKeys)
{
    WIM_CHECK(key(VK_HOME, 0) == "\x1B[H");
    WIM_CHECK(key(VK_END, 0) == "\x1B[F");
    WIM_CHECK(key(VK_DELETE, 0) == "\x1B[3~");
    WIM_CHECK(key(VK_INSERT, 0) == "\x1B[2~");
    WIM_CHECK(key(VK_PRIOR, 0) == "\x1B[5~");  // PgUp
    WIM_CHECK(key(VK_NEXT, 0) == "\x1B[6~");   // PgDn
    KeyEvent ctrlDel;
    ctrlDel.vk = VK_DELETE;
    ctrlDel.ctrl = true;
    WIM_CHECK(encodeKey(ctrlDel, KeyModes{}) == "\x1B[3;5~");
}

WIM_TEST(KeyFunctionKeys)
{
    WIM_CHECK(key(VK_F1, 0) == "\x1BOP");
    WIM_CHECK(key(VK_F4, 0) == "\x1BOS");
    WIM_CHECK(key(VK_F5, 0) == "\x1B[15~");
    WIM_CHECK(key(VK_F10, 0) == "\x1B[21~");
    WIM_CHECK(key(VK_F12, 0) == "\x1B[24~");
    KeyEvent shiftF3;
    shiftF3.vk = VK_F3;
    shiftF3.shift = true;
    WIM_CHECK(encodeKey(shiftF3, KeyModes{}) == "\x1B[1;2R");
}

WIM_TEST(KeyEncodeCharHelper)
{
    WIM_CHECK(encodeChar(L'x', false, false, false) == "x");
    WIM_CHECK(encodeChar(L'x', false, true, false) == "\x1Bx");
    WIM_CHECK(encodeChar(L'c', true, false, false) == "\x03");
}
