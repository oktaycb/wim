// wim - a Windows-native terminal workspace for coding agents.
// Core/vt: the terminal emulator. Feeds bytes through the parser and
// applies the resulting actions to a screen buffer: cursor movement,
// SGR attributes, erase/insert/delete, scroll regions, modes (alt screen,
// autowrap, bracketed paste, mouse reporting), and OSC events (title,
// cwd, notifications, prompt marks) surfaced through TerminalEvents.
#pragma once

#include "core/vt/attrs.h"
#include "core/vt/parser.h"
#include "core/vt/screen.h"

#include <string>

namespace wim::vt {

struct TerminalEvents {
    virtual ~TerminalEvents() = default;
    // OSC 0 / OSC 2.
    virtual void onTitle(const std::string&) {}
    // OSC 7 - a file:// URL, decoded to a plain path.
    virtual void onCwd(const std::string&) {}
    // OSC 9 / 99 / 777 / wim notify: something wants the user.
    virtual void onNotify(const std::string&, const std::string&, bool) {}
    // BEL - only the app decides whether a bell means attention.
    virtual void onBell() {}
    // OSC 133 - shell integration marks: A=prompt, B=command start,
    // C=command output start, D=command end.
    virtual void onPromptMark(char) {}
    // Mouse reporting / bracketed paste state changed (input encoding).
    virtual void onInputModesChanged() {}
    // A completed line of output (plain text, main screen) - feeds the
    // find index and `wim read-pane`.
    virtual void onLine(const std::string&) {}
};

class Terminal : public Sink {
public:
    Terminal(int cols, int rows, TerminalEvents* events = nullptr);

    // The parser inside holds a pointer back to this Terminal, so copies
    // and moves would silently break the sink wiring. Not copyable.
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // Feeds output bytes from ConPTY.
    void write(const char* data, size_t len);

    // Screen geometry.
    int cols() const { return main_.cols(); }
    int rows() const { return main_.rows(); }
    void resize(int cols, int rows);

    // The visible buffer (main or alt).
    const ScreenBuffer& screen() const { return inAlt_ ? alt_ : main_; }
    bool inAltScreen() const { return inAlt_; }

    // ---- cursor state (for the renderer) ----
    int cursorX() const { return cursor_.x; }
    int cursorY() const { return cursor_.y; }
    bool cursorVisible() const { return cursorVisible_; }
    void setCursorVisible(bool visible) { cursorVisible_ = visible; }

    // ---- modes the input side needs ----
    bool mouseReporting() const { return mouseMode_ != MouseMode::Off; }
    bool bracketedPaste() const { return bracketedPaste_; }
    bool applicationCursorKeys() const { return applicationCursorKeys_; }

    // ---- scrollback viewing (main screen) ----
    // View offset in lines above the bottom; 0 = live.
    int scrollOffset() const { return scrollOffset_; }
    void setScrollOffset(int lines); // clamped to history + 0

    // Damage flag for the renderer (buffer + cursor both mark it).
    bool damaged() const { return damage_; }
    void clearDamage() { damage_ = false; }

    // ---- Sink (driven by the parser) ----
    void print(char32_t ch) override;
    void execute(unsigned char control) override;
    void escDispatch(const char* intermediates, int count, char finalByte) override;
    void csiDispatch(const CsiSequence& seq) override;
    void oscPut(const char* data, size_t len) override;
    void oscEnd(bool bellTerminated) override;

    // Soft-reset for a reused pane.
    void reset();

    // Line text (UTF-8, trailing blanks trimmed) for tests and find.
    std::string lineText(int y) const;
    std::string altLineText(int y) const;

private:

private:
    enum class MouseMode : int { Off, Press, Drag, Any };

    struct Cursor {
        int x = 0;
        int y = 0;
        bool pendingWrap = false;
    };

    void linefeed();
    void reverseLinefeed();
    void scrollOrMoveDown();
    void clampCursorToRegion();
    void moveCursor(int x, int y);
    void eraseCells(int fromX, int count);
    void blankRowRange(int y, int fromX, int toX);
    void applySgr(const CsiSequence& seq);
    void setMode(const CsiSequence& seq, bool enable);
    void enterAltScreen();
    void leaveAltScreen();
    void saveCursor();
    void restoreCursor();
    void switchScreen(bool toAlt);
    void handleOsc(const std::string& payload);
    void handleCsi(const CsiSequence& seq);
    static std::string encodeLine(const Line& line);

    ScreenBuffer main_;
    ScreenBuffer alt_;
    bool inAlt_ = false;
    ScreenBuffer* active_ = &main_;

    Cursor cursor_;
    Cursor savedCursor_;
    Attr savedPenAttr_;
    bool cursorVisible_ = true;
    char32_t lastChar_ = 0; // for REP (CSI b)

    // Scroll region (inclusive, absolute rows). Full screen by default.
    int regionTop_ = 0;
    int regionBottom_ = 0; // set in ctor to rows-1

    Pen pen_;
    bool originMode_ = false;
    bool autoWrap_ = true;
    bool bracketedPaste_ = false;
    bool applicationCursorKeys_ = false;
    MouseMode mouseMode_ = MouseMode::Off;
    bool mouseSgr_ = false;

    int scrollOffset_ = 0;
    bool damage_ = false;

    Parser parser_;
    TerminalEvents* events_ = nullptr;

    // OSC assembly.
    std::string osc_;

    // Tab stops: every 8 columns (DECSTBCS is rare; fixed stops suffice).
    int tabWidth_ = 8;
};

} // namespace wim::vt
