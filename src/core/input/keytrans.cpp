#include "keytrans.h"
#include "core/util/str.h"

#include <windows.h>

namespace wim::input {

namespace {

std::string utf8(char32_t cp)
{
    std::string out;
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
    return out;
}

// The xterm modifier parameter: 1 + (1*shift + 2*alt + 4*ctrl).
int modifierParam(const KeyEvent& e)
{
    int m = 1;
    if (e.shift)
        m += 1;
    if (e.alt)
        m += 2;
    if (e.ctrl)
        m += 4;
    return m;
}

bool hasModifier(const KeyEvent& e)
{
    return e.ctrl || e.alt || e.shift;
}

std::string csi(const std::string& params, char finalByte)
{
    return "\x1B[" + params + finalByte;
}

std::string ss3(char finalByte)
{
    return std::string("\x1BO") + finalByte;
}

// Cursor-family keys: unmodified uses app-mode-aware forms, modified
// always uses CSI with the modifier parameter.
std::string cursorKey(char csiFinal, char ss3Final, const KeyEvent& e, const KeyModes& modes)
{
    if (!hasModifier(e))
        return modes.applicationCursorKeys ? ss3(ss3Final) : csi("", csiFinal);
    return csi("1;" + std::to_string(modifierParam(e)), csiFinal);
}

} // namespace

std::string encodeChar(wchar_t ch, bool ctrl, bool alt, bool shift)
{
    KeyEvent e;
    e.ch = ch;
    e.ctrl = ctrl;
    e.alt = alt;
    e.shift = shift;
    e.vk = 0;
    if (ch >= 'a' && ch <= 'z')
        e.vk = unsigned(ch - 'a' + 'A');
    else
        e.vk = VkKeyScanW(ch) & 0xFF;
    KeyModes modes;
    return encodeKey(e, modes);
}

std::string encodeKey(const KeyEvent& e, const KeyModes& modes)
{
    if (!e.pressed)
        return {};

    // Alt prefixes everything else with ESC (the xterm meta encoding).
    const std::string esc = e.alt ? std::string("\x1B") : std::string();

    switch (e.vk) {
    case VK_RETURN:
        return e.alt ? esc + "\r" : std::string("\r");
    case VK_BACK:
        return e.ctrl ? std::string("\x08") : std::string(e.alt ? esc + "\x7F" : "\x7F");
    case VK_TAB:
        if (e.ctrl)
            return {};
        if (e.shift)
            return csi("", 'Z'); // back-tab
        return esc + "\t";
    case VK_ESCAPE:
        return e.alt ? esc + "\x1B" : std::string("\x1B");
    case VK_SPACE:
        if (e.ctrl && !e.shift)
            return std::string("\x00", 1); // NUL, what Ctrl+Space means to shells
        break;
    case VK_UP:
        return cursorKey('A', 'A', e, modes);
    case VK_DOWN:
        return cursorKey('B', 'B', e, modes);
    case VK_RIGHT:
        return cursorKey('C', 'C', e, modes);
    case VK_LEFT:
        return cursorKey('D', 'D', e, modes);
    case VK_HOME:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'H')
                              : (modes.applicationCursorKeys ? ss3('H') : csi("", 'H'));
    case VK_END:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'F')
                              : (modes.applicationCursorKeys ? ss3('F') : csi("", 'F'));
    case VK_PRIOR: // PgUp
        return hasModifier(e) ? csi("5;" + std::to_string(modifierParam(e)), '~')
                              : csi("5", '~');
    case VK_NEXT: // PgDn
        return hasModifier(e) ? csi("6;" + std::to_string(modifierParam(e)), '~')
                              : csi("6", '~');
    case VK_DELETE:
        return hasModifier(e) ? csi("3;" + std::to_string(modifierParam(e)), '~') : csi("3", '~');
    case VK_INSERT:
        return hasModifier(e) ? csi("2;" + std::to_string(modifierParam(e)), '~') : csi("2", '~');
    case VK_F1:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'P') : ss3('P');
    case VK_F2:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'Q') : ss3('Q');
    case VK_F3:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'R') : ss3('R');
    case VK_F4:
        return hasModifier(e) ? csi("1;" + std::to_string(modifierParam(e)), 'S') : ss3('S');
    case VK_F5:
        return csi("15" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F6:
        return csi("17" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F7:
        return csi("18" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F8:
        return csi("19" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F9:
        return csi("20" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F10:
        return csi("21" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F11:
        return csi("23" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    case VK_F12:
        return csi("24" + (hasModifier(e) ? ";" + std::to_string(modifierParam(e)) : ""), '~');
    default:
        break;
    }

    if (e.ctrl && e.ch >= 'a' && e.ch <= 'z') {
        // Ctrl+letter -> the matching control byte.
        return esc + std::string(1, char(e.ch - 'a' + 1));
    }
    if (e.ctrl && e.ch >= 'A' && e.ch <= 'Z') {
        return esc + std::string(1, char(e.ch - 'A' + 1));
    }
    if (e.ch != 0) {
        // Plain text (already shift-composed by ToUnicode).
        return esc + utf8(e.ch);
    }
    return {};
}

} // namespace wim::input
