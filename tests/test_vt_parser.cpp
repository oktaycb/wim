// Tests for vt/parser.h: the escape-sequence state machine.
// Written via the Write tool: this file's strings must survive verbatim
// (backslash escapes included), which shell heredocs do not guarantee.
#include "core/vt/parser.h"
#include "test.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace wim::vt;

namespace {

// Records parser events into readable strings.
struct Recorder : Sink {
    std::vector<std::string> events;
    std::string oscPayload;

    void print(char32_t ch) override
    {
        std::string s = "P:";
        if (ch < 0x80) {
            s += char(ch);
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "U+%04X", unsigned(ch));
            s += buf;
        }
        events.push_back(std::move(s));
    }
    void execute(unsigned char c) override
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "X:%02X", c);
        events.push_back(buf);
    }
    void escDispatch(const char* inter, int count, char finalByte) override
    {
        std::string s = "E:";
        s.append(inter, size_t(count));
        s += finalByte;
        events.push_back(std::move(s));
    }
    void csiDispatch(const CsiSequence& q) override
    {
        std::string s = "C:";
        if (q.privateMarker)
            s += q.privateMarker;
        for (int i = 0; i < q.paramCount; ++i) {
            if (i)
                s += ';';
            if (q.hasParam[i])
                s += std::to_string(q.params[i]);
        }
        for (int i = 0; i < q.intermediateCount; ++i)
            s += q.intermediates[i];
        s += q.finalByte;
        events.push_back(std::move(s));
    }
    void oscStart() override { events.push_back("OSC<"); }
    void oscPut(const char* data, size_t len) override { oscPayload.append(data, len); }
    void oscEnd(bool bell) override
    {
        events.push_back(std::string("OSC>") + (bell ? "(bel)" : "(st)") + oscPayload);
        oscPayload.clear();
    }
    void dcsStart() override { events.push_back("DCS<"); }
    void dcsEnd() override { events.push_back("DCS>"); }
    void apcStart() override { events.push_back("APC<"); }
    void apcEnd() override { events.push_back("APC>"); }
};

Recorder parse(const std::string& bytes)
{
    Recorder r;
    Parser p;
    p.setSink(&r);
    p.feed(bytes.data(), bytes.size());
    return r;
}

} // namespace

WIM_TEST(ParserPrintsPlain)
{
    const auto r = parse("hello");
    WIM_CHECK_EQ(r.events.size(), size_t(5));
    WIM_CHECK(r.events[0] == "P:h");
    WIM_CHECK(r.events[4] == "P:o");
}

WIM_TEST(ParserC0Controls)
{
    const auto r = parse("a\r\n\t\b");
    WIM_CHECK_EQ(r.events.size(), size_t(5));
    WIM_CHECK(r.events[1] == "X:0D");
    WIM_CHECK(r.events[2] == "X:0A");
    WIM_CHECK(r.events[3] == "X:09");
    WIM_CHECK(r.events[4] == "X:08");
    const auto r2 = parse(std::string("\x7F"));
    WIM_CHECK(r2.events[0] == "X:7F");
    const auto r3 = parse(std::string("\x07"));
    WIM_CHECK(r3.events[0] == "X:07"); // BEL drives attention
}

WIM_TEST(ParserUtf8)
{
    const auto r = parse(std::string("a\xC4\xB1")); // a + U+0131
    WIM_CHECK_EQ(r.events.size(), size_t(2));
    WIM_CHECK(r.events[1] == "P:U+0131");

    const auto emoji = parse(std::string("\xF0\x9F\x98\x80"));
    WIM_CHECK(emoji.events[0] == "P:U+1F600");

    Recorder r2;
    Parser p;
    p.setSink(&r2);
    p.feed("\xF0", 1);
    p.feed("\x9F", 1);
    p.feed("\x98", 1);
    WIM_CHECK(r2.events.empty());
    p.feed("\x80", 1);
    WIM_CHECK_EQ(r2.events.size(), size_t(1));
    WIM_CHECK(r2.events[0] == "P:U+1F600");

    const auto broken = parse(std::string("\xC3\x28"));
    WIM_CHECK_EQ(broken.events.size(), size_t(2));
    WIM_CHECK(broken.events[0] == "P:U+FFFD");
    WIM_CHECK(broken.events[1] == "P:(");

    const auto over = parse(std::string("\xC0\xAF")); // overlong '/'
    WIM_CHECK(over.events[0] == "P:U+FFFD");

    const auto stray = parse(std::string("\x80z"));
    WIM_CHECK_EQ(stray.events.size(), size_t(2));
    WIM_CHECK(stray.events[0] == "P:U+FFFD");
}

WIM_TEST(ParserEscSequences)
{
    const auto r = parse(std::string("\x1B(M\x1B" "7\x1B=", 7));
    WIM_CHECK_EQ(r.events.size(), size_t(3));
    WIM_CHECK(r.events[0] == "E:(M"); // charset with intermediate
    WIM_CHECK(r.events[1] == "E:7");
    WIM_CHECK(r.events[2] == "E:=");
}

WIM_TEST(ParserCsiSequences)
{
    const auto r = parse(std::string("\x1B[2J\x1B[H\x1B[38;5;196m\x1B[?1049h\x1B[1;5A"));
    WIM_CHECK_EQ(r.events.size(), size_t(5));
    WIM_CHECK(r.events[0] == "C:2J");
    WIM_CHECK(r.events[1] == "C:H");
    WIM_CHECK(r.events[2] == "C:38;5;196m");
    WIM_CHECK(r.events[3] == "C:?1049h");
    WIM_CHECK(r.events[4] == "C:1;5A");
}


WIM_TEST(ParserCsiWithIntermediates)
{
    const auto r = parse(std::string("\x1B[!p")); // DECSTR
    WIM_CHECK(r.events[0] == "C:!p");
    const auto r2 = parse(std::string("\x1B[0 q")); // cursor style
    WIM_CHECK(r2.events[0] == "C:0 q");
}

WIM_TEST(ParserCsiC0Interleaved)
{
    const auto r = parse(std::string("\x1B[2\rJ"));
    WIM_CHECK_EQ(r.events.size(), size_t(2));
    WIM_CHECK(r.events[0] == "X:0D");
    WIM_CHECK(r.events[1] == "C:2J");
}

WIM_TEST(ParserCsiGarbageIgnored)
{
    // '<' after a param started fits nowhere: sequence dropped entirely.
    const auto r = parse(std::string("\x1B[2<J"));
    WIM_CHECK_EQ(r.events.size(), size_t(0));
    // '%' is a valid intermediate, not garbage.
    const auto ok = parse(std::string("\x1B[2%J"));
    WIM_CHECK_EQ(ok.events.size(), size_t(1));
    WIM_CHECK(ok.events[0] == "C:2%J");
}

WIM_TEST(ParserOscBelTerminated)
{
    const auto r = parse(std::string("\x1B]0;my title\x07rest"));
    WIM_CHECK_EQ(r.events.size(), size_t(6));
    WIM_CHECK(r.events[0] == "OSC<");
    WIM_CHECK(r.events[1] == "OSC>(bel)0;my title");
    WIM_CHECK(r.events[2] == "P:r");
    WIM_CHECK(r.events[5] == "P:t");
}

WIM_TEST(ParserOscStTerminated)
{
    // ESC \ (ST) ends the string; the backslash then arrives as a plain
    // ESC-dispatch which sinks ignore; text continues ("after" = 5 chars).
    const auto r = parse(std::string("\x1B]9;Some notified\x1B\\after"));
    WIM_CHECK_EQ(r.events.size(), size_t(8));
    WIM_CHECK(r.events[0] == "OSC<");
    WIM_CHECK(r.events[1] == "OSC>(st)9;Some notified");
    WIM_CHECK(r.events[2] == "E:\\");
    WIM_CHECK(r.events[3] == "P:a");
    WIM_CHECK(r.events[7] == "P:r");
}

WIM_TEST(ParserOscOversizedTruncates)
{
    std::string big = "\x1B]0;";
    big.append(20000, 'x');
    big += "\x07";
    Recorder r;
    Parser p;
    p.setSink(&r);
    p.feed(big.data(), big.size());
    WIM_CHECK_EQ(r.events.size(), size_t(2));
    // The payload the sink sees is bounded by the parser's buffer.
    WIM_CHECK(r.events[1].size() <= std::string("OSC>(bel)0;").size() + 8190);
    WIM_CHECK(r.events[1].size() > std::string("OSC>(bel)0;").size() + 100);
}

WIM_TEST(ParserDcsDiscarded)
{
    const auto r = parse(std::string("\x1BPq#0;2;0;0;0#0~~\x1B\\ok"));
    WIM_CHECK_EQ(r.events.size(), size_t(5));
    WIM_CHECK(r.events[0] == "DCS<");
    WIM_CHECK(r.events[1] == "DCS>");
    WIM_CHECK(r.events[2] == "E:\\");
    WIM_CHECK(r.events[3] == "P:o");
    WIM_CHECK(r.events[4] == "P:k");
}

WIM_TEST(ParserApcDiscarded)
{
    // Kitty graphics live in APC.
    const auto r = parse(std::string("\x1B_Ga=f,u=64\x1B\\z"));
    WIM_CHECK_EQ(r.events.size(), size_t(4));
    WIM_CHECK(r.events[0] == "APC<");
    WIM_CHECK(r.events[1] == "APC>");
    WIM_CHECK(r.events[2] == "E:\\");
    WIM_CHECK(r.events[3] == "P:z");
}

WIM_TEST(ParserCanAbortsSequence)
{
    Recorder rr;
    Parser p;
    p.setSink(&rr);
    p.feed("\x1B[38;5;\x18m", 9);
    WIM_CHECK_EQ(rr.events.size(), size_t(2));
    WIM_CHECK(rr.events[0] == "X:18");
    WIM_CHECK(rr.events[1] == "P:m"); // after CAN, 'm' is plain text
}

// CSI default/sub-parameter semantics, asserted structurally.
namespace {
struct Capture : Sink {
    std::vector<CsiSequence> csis;
    void csiDispatch(const CsiSequence& q) override { csis.push_back(q); }
};
} // namespace

WIM_TEST(ParserCsiDefaultsAndSubparams)
{
    Capture cap;
    Parser p;
    p.setSink(&cap);
    p.feed("\x1B[;5H\x1B[;m\x1B[H\x1B[4:3m");

    WIM_CHECK_EQ(cap.csis.size(), size_t(4));

    // ";5H": empty row + col 5.
    WIM_CHECK_EQ(cap.csis[0].paramCount, 2);
    WIM_CHECK(!cap.csis[0].hasParam[0]);
    WIM_CHECK(cap.csis[0].hasParam[1] && cap.csis[0].params[1] == 5);

    // ";m": one empty param.
    WIM_CHECK_EQ(cap.csis[1].paramCount, 1);
    WIM_CHECK(!cap.csis[1].hasParam[0]);

    // "[H": no params at all.
    WIM_CHECK_EQ(cap.csis[2].paramCount, 0);

    // "4:3m": sub-parameter flattened to just 4.
    WIM_CHECK_EQ(cap.csis[3].paramCount, 1);
    WIM_CHECK(cap.csis[3].hasParam[0] && cap.csis[3].params[0] == 4);
}
