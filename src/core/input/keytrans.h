// wim - a Windows-native terminal workspace for coding agents.
// Core/input: translating key events into the VT byte sequences ConPTY
// expects. Pure functions; the GUI layer owns interception of shortcuts
// that belong to wim itself (Ctrl+Shift+..., pane close, copy/paste).
#pragma once

#include <string>

namespace wim::input {

struct KeyEvent {
    unsigned vk = 0;   // VK_* code
    wchar_t ch = 0;    // ToUnicode result, 0 when none
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool pressed = true; // key-down vs key-up (only down is encoded)
};

// How the target terminal wants its sequences.
struct KeyModes {
    bool applicationCursorKeys = false; // SS3 arrows instead of CSI
};

// Encodes a key-down event; empty for events that produce nothing
// (key-ups, plain dead keys, unknown combos).
std::string encodeKey(const KeyEvent& e, const KeyModes& modes);

// Convenience for the GUI: encodes a WM_CHAR-style character with the
// keyboard modifiers of the moment.
std::string encodeChar(wchar_t ch, bool ctrl, bool alt, bool shift);

} // namespace wim::input
