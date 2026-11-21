#include "parser.h"

#include <cstring>

namespace wim::vt {

namespace {

inline bool isIntermed(unsigned char b) { return b >= 0x20 && b <= 0x2F; }
inline bool isFinal(unsigned char b) { return b >= 0x40 && b <= 0x7E; }
inline bool isDigit(unsigned char b) { return b >= '0' && b <= '9'; }
inline bool isC0(unsigned char b) { return b < 0x20; }

} // namespace

void Parser::feed(const char* data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        byte((unsigned char)data[i]);
}

void Parser::feed(const char* s)
{
    feed(s, strlen(s));
}

void Parser::enter(State s)
{
    state_ = s;
}

void Parser::collect(unsigned char b)
{
    if (strLen_ + 1 < sizeof(str_))
        str_[strLen_++] = char(b);
    // Overlong strings keep accumulating (and truncating) until their
    // terminator; a hostile stream cannot wedge the parser.
}

void Parser::byte(unsigned char b)
{
    // CAN and SUB abort any sequence from any state.
    if (b == 0x18 || b == 0x1A) {
        switch (state_) {
        case State::OscString: sink_->oscEnd(false); break;
        case State::DcsEntry:
        case State::DcsParam:
        case State::DcsIntermediate:
        case State::DcsPassthrough: sink_->dcsEnd(); break;
        case State::SosPmApcString: sink_->apcEnd(); break;
        default: break;
        }
        enter(State::Ground);
        sink_->execute(b);
        return;
    }

    switch (state_) {
    case State::Ground:
        if (b == 0x1B) {
            enter(State::Escape);
        } else if (b < 0x20 || b == 0x7F) {
            sink_->execute(b); // C0 and DEL
        } else if (b < 0x80) {
            sink_->print(char32_t(b));
        } else if ((b & 0xE0) == 0xC0) {
            codepoint_ = b & 0x1F;
            utf8Needed_ = 1;
            utf8Seen_ = 1;
            enter(State::Utf8);
        } else if ((b & 0xF0) == 0xE0) {
            codepoint_ = b & 0x0F;
            utf8Needed_ = 2;
            utf8Seen_ = 1;
            enter(State::Utf8);
        } else if ((b & 0xF8) == 0xF0) {
            codepoint_ = b & 0x07;
            utf8Needed_ = 3;
            utf8Seen_ = 1;
            enter(State::Utf8);
        } else {
            sink_->invalidByte(b, int(state_));
            sink_->print(0xFFFD); // stray continuation byte
        }
        return;

    case State::Utf8:
        if ((b & 0xC0) == 0x80) {
            codepoint_ = (codepoint_ << 6) | (b & 0x3F);
            ++utf8Seen_;
            if (utf8Seen_ <= utf8Needed_)
                return;
            char32_t cp = codepoint_;
            enter(State::Ground);
            if ((utf8Needed_ == 1 && cp < 0x80) || (utf8Needed_ == 2 && cp < 0x800) ||
                (utf8Needed_ == 3 && cp < 0x10000) || cp > 0x10FFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF))
                cp = 0xFFFD; // overlong, out of range, or surrogate
            sink_->print(cp);
            return;
        }
        // Broken sequence: replacement char, then reprocess this byte.
        enter(State::Ground);
        sink_->print(0xFFFD);
        byte(b);
        return;

    case State::Escape:
        switch (b) {
        case '[':
            csi_ = CsiSequence{};
            csiOpen_ = false;
            csiSub_ = false;
            enter(State::CsiEntry);
            return;
        case ']':
            strLen_ = 0;
            enter(State::OscString);
            sink_->oscStart();
            return;
        case 'P':
            strLen_ = 0;
            enter(State::DcsEntry);
            sink_->dcsStart();
            return;
        case 'X':
        case '^':
        case '_': // SOS, PM, APC
            strLen_ = 0;
            enter(State::SosPmApcString);
            sink_->apcStart();
            return;
        default:
            if (isIntermed(b)) {
                str_[0] = char(b);
                strLen_ = 1;
                enter(State::EscapeIntermediate);
                return;
            }
            if (b >= 0x30 && b <= 0x7E) {
                enter(State::Ground);
                sink_->escDispatch(nullptr, 0, char(b));
                return;
            }
            sink_->invalidByte(b, int(state_));
            enter(State::Ground);
            byte(b);
            return;
        }

    case State::EscapeIntermediate:
        if (isIntermed(b)) {
            collect(b);
            return;
        }
        if (b >= 0x30 && b <= 0x7E) {
            enter(State::Ground);
            sink_->escDispatch(str_, int(strLen_), char(b));
            return;
        }
        sink_->invalidByte(b, int(state_));
        enter(State::Ground);
        byte(b);
        return;

    case State::CsiEntry:
        if (b == '<' || b == '=' || b == '>' || b == '?') {
            csi_.privateMarker = char(b);
            enter(State::CsiParam);
            return;
        }
        if (isDigit(b) || b == ';') {
            // Fall into param collection with this first byte.
            enter(State::CsiParam);
            byte(b);
            return;
        }
        if (isIntermed(b)) {
            csi_.intermediates[csi_.intermediateCount++] = char(b);
            enter(State::CsiIntermediate);
            return;
        }
        if (isFinal(b)) {
            csi_.finalByte = char(b);
            enter(State::Ground);
            sink_->csiDispatch(csi_);
            return;
        }
        if (isC0(b)) {
            sink_->execute(b); // C0 interleaved into CSI
            return;
        }
        enter(State::CsiIgnore);
        return;

    case State::CsiParam:
        if (isDigit(b)) {
            if (csiSub_)
                return; // sub-parameter digits are dropped
            if (!csiOpen_ && csi_.paramCount < CsiSequence::kMaxParams) {
                csiOpen_ = true;
                csi_.hasParam[csi_.paramCount] = true;
                ++csi_.paramCount;
            }
            const int slot = csi_.paramCount - 1;
            csi_.params[slot] = csi_.params[slot] * 10 + (b - '0');
            return;
        }
        if (b == ';') {
            csiSub_ = false;
            if (csiOpen_) {
                csiOpen_ = false; // close the current slot
            } else if (csi_.paramCount < CsiSequence::kMaxParams) {
                ++csi_.paramCount; // an empty param still occupies a slot
            }
            return;
        }
        if (b == ':') {
            csiSub_ = true; // sub-parameters flatten away
            return;
        }
        if (isIntermed(b)) {
            csi_.intermediates[csi_.intermediateCount++] = char(b);
            enter(State::CsiIntermediate);
            return;
        }
        if (isFinal(b)) {
            csi_.finalByte = char(b);
            enter(State::Ground);
            sink_->csiDispatch(csi_);
            return;
        }
        if (isC0(b)) {
            sink_->execute(b);
            return;
        }
        enter(State::CsiIgnore);
        return;

    case State::CsiIntermediate:
        if (isIntermed(b)) {
            if (csi_.intermediateCount < 4)
                csi_.intermediates[csi_.intermediateCount++] = char(b);
            return;
        }
        if (isFinal(b)) {
            csi_.finalByte = char(b);
            enter(State::Ground);
            sink_->csiDispatch(csi_);
            return;
        }
        if (isC0(b)) {
            sink_->execute(b);
            return;
        }
        enter(State::CsiIgnore);
        return;

    case State::CsiIgnore:
        if (isFinal(b))
            enter(State::Ground);
        return;

    case State::OscString:
        if (b == 0x07) {
            enter(State::Ground);
            sink_->oscEnd(true);
            return;
        }
        if (b == 0x1B) {
            // ESC terminates the string (ESC \ is the proper ST; any other
            // ESC sequence also ends the OSC before it starts).
            enter(State::Escape);
            sink_->oscEnd(false);
            return;
        }
        // Remaining bytes, including tolerated C0, are payload - bounded, so
        // a hostile stream cannot flood the sink through oscPut.
        if (strLen_ + 1 < sizeof(str_)) {
            collect(b);
            sink_->oscPut(reinterpret_cast<const char*>(&b), 1);
        }
        return;

    case State::DcsEntry:
    case State::DcsParam:
    case State::DcsIntermediate:
        if (isIntermed(b)) {
            state_ = State::DcsIntermediate;
            return;
        }
        if (isDigit(b) || b == ';' || b == ':' || b == '<' || b == '=' || b == '>' ||
            b == '?') {
            state_ = State::DcsParam;
            return;
        }
        if (isFinal(b)) {
            state_ = State::DcsPassthrough;
            return;
        }
        if (b == 0x1B) {
            state_ = State::Escape;
            sink_->dcsEnd();
            return;
        }
        if (b == 0x9C) { // 8-bit ST tolerated
            enter(State::Ground);
            sink_->dcsEnd();
            return;
        }
        return;

    case State::DcsPassthrough:
        if (b == 0x1B) {
            state_ = State::Escape;
            sink_->dcsEnd();
            return;
        }
        if (b == 0x9C) {
            enter(State::Ground);
            sink_->dcsEnd();
            return;
        }
        return; // payload discarded

    case State::DcsIgnore:
        if (b == 0x1B)
            state_ = State::Escape;
        return;

    case State::SosPmApcString:
        if (b == 0x1B) {
            state_ = State::Escape;
            sink_->apcEnd();
            return;
        }
        if (b == 0x9C) {
            enter(State::Ground);
            sink_->apcEnd();
            return;
        }
        return;
    }
}

} // namespace wim::vt
