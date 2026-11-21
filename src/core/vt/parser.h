// wim - a Windows-native terminal workspace for coding agents.
// Core/vt: the escape-sequence parser. An incremental byte-stream state
// machine in the shape of Paul Williams' classic VT500 parser: bytes in,
// semantic actions out through a Sink. The parser owns no screen state, so
// tests drive it with a recording sink and the terminal emulator is just
// another sink.
//
// Handled streams: printable text (UTF-8 decoded to char32_t), C0
// controls, ESC sequences, CSI with private markers/params/intermediates,
// OSC strings (BEL- or ST-terminated), and DCS/SOS/PM/APC which are
// consumed and discarded with their boundaries reported.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wim::vt {

// One complete CSI sequence, handed to Sink::csiDispatch.
struct CsiSequence {
    static constexpr int kMaxParams = 16;

    int params[kMaxParams] = {0};    // missing params read as 0; use hasParam
    bool hasParam[kMaxParams] = {false};
    int paramCount = 0;              // number of parameter slots seen
    char intermediates[4] = {0};     // 0x20-0x2F collected before the final
    int intermediateCount = 0;
    char privateMarker = 0;          // one of '<', '=', '>', '?' when present
    char finalByte = 0;              // 0x40-0x7E

    int param(int index, int fallback) const
    {
        return index < paramCount && hasParam[index] ? params[index] : fallback;
    }
    // Sub-parameters (colon groups) are not tracked; '3:4' reads as 3.
};

class Sink {
public:
    virtual ~Sink() = default;

    virtual void print(char32_t) {}
    virtual void execute(unsigned char) {} // C0 (CR, LF, BEL, ...)
    virtual void escDispatch(const char*, int, char) {}
    virtual void csiDispatch(const CsiSequence&) {}
    virtual void oscStart() {}
    virtual void oscPut(const char*, size_t) {}
    virtual void oscEnd(bool) {}
    virtual void dcsStart() {} // content is discarded
    virtual void dcsEnd() {}
    virtual void apcStart() {}
    virtual void apcEnd() {}
    // A byte that fits no valid sequence was consumed; parsers must survive
    // hostile output, so this is a report, not an error.
    virtual void invalidByte(unsigned char, int) {}
};

class Parser {
public:
    void setSink(Sink* sink) { sink_ = sink; }

    // Feeds bytes; safe to call with partial UTF-8 or split sequences.
    void feed(const char* data, size_t len);
    // NUL-terminated convenience overload.
    void feed(const char* s);

private:
    enum class State : int {
        Ground, Utf8, Escape, EscapeIntermediate,
        CsiEntry, CsiParam, CsiIntermediate, CsiIgnore,
        OscString, DcsEntry, DcsParam, DcsIntermediate, DcsPassthrough, DcsIgnore,
        SosPmApcString,
    };

    void byte(unsigned char b);
    void enter(State s);
    // Collects an OSC/DCS payload byte; oversized payloads are truncated.
    void collect(unsigned char b);

    Sink* sink_ = nullptr;
    State state_ = State::Ground;

    // CSI assembly. csiOpen_ is true while digits are being collected for
    // the current (last) slot; a ';' closes it and an empty slot counts.
    // csiSub_ is true after a ':' - those digits belong to a sub-parameter
    // and are dropped, so '4:3m' reads as just 4, never 43.
    CsiSequence csi_;
    bool csiOpen_ = false;
    bool csiSub_ = false;

    // String (OSC/DCS/APC) assembly.
    char str_[8192];
    size_t strLen_ = 0;

    // UTF-8 assembly.
    uint32_t codepoint_ = 0;
    int utf8Needed_ = 0;   // continuation bytes still expected
    int utf8Seen_ = 0;
};

} // namespace wim::vt
