// Tests for the JSON module: parser strictness, escapes, round trips.
#include "core/json/json.h"
#include "core/util/str.h"
#include "test.h"

using wim::json::Value;

WIM_TEST(JsonParseLiterals)
{
    WIM_CHECK(wim::json::parse("null").value.isNull());
    WIM_CHECK(wim::json::parse("true").value.asBool());
    WIM_CHECK(!wim::json::parse("false").value.asBool());
    for (std::string ws : {"", " ", "\r\n\t ", "  \n"}) {
        WIM_CHECK(wim::json::parse(ws + "null" + ws).ok);
    }
}

WIM_TEST(JsonParseNumbers)
{
    WIM_CHECK_EQ(wim::json::parse("0").value.asNumber(), 0.0);
    WIM_CHECK_EQ(wim::json::parse("-7").value.asNumber(), -7.0);
    WIM_CHECK_EQ(wim::json::parse("3.25").value.asNumber(), 3.25);
    WIM_CHECK_EQ(wim::json::parse("-0.5e2").value.asNumber(), -50.0);
    WIM_CHECK_EQ(wim::json::parse("1E+3").value.asNumber(), 1000.0);
    WIM_CHECK_EQ(wim::json::parse("42").value.asInt(), 42LL);

    for (std::string bad : {"01", "1.", ".5", "1e", "-", "+1", "1e+", "infinity", "NaN"}) {
        const auto r = wim::json::parse(bad);
        WIM_CHECK(!r.ok);
        if (!r.ok)
            WIM_CHECK(!r.error.empty());
    }
}

WIM_TEST(JsonParseStrings)
{
    WIM_CHECK_STREQ(wim::json::parse("\"\"").value.asString(), "");
    WIM_CHECK_STREQ(wim::json::parse("\"hello\"").value.asString(), "hello");
    WIM_CHECK_STREQ(wim::json::parse("\"a\\nb\\tc\"").value.asString(), "a\nb\tc");
    WIM_CHECK_STREQ(wim::json::parse("\"quote:\\\" slash:\\\\\"" ).value.asString(), "quote:\" slash:\\");
    // Surrogate pair: D83D DE00 = U+1F600 grinning face = F0 9F 98 80.
    WIM_CHECK_STREQ(wim::json::parse("\"\\uD83D\\uDE00\"").value.asString(), "\xF0\x9F\x98\x80");
    // Turkish as raw UTF-8 passes through.
    WIM_CHECK_STREQ(wim::json::parse("\"\xc4\xb1\xc5\x9f\"").value.asString(), "\xc4\xb1\xc5\x9f");

    for (std::string bad : {"\"unterminated", "\"bad \\x escape\"", "\"\\u12\"",
                            "\"\\uD83D alone\"", "\"\\uD83D\\u0041\"", "\"nl\ninside\""}) {
        WIM_CHECK(!wim::json::parse(bad).ok);
    }
}

WIM_TEST(JsonParseContainers)
{
    const auto r = wim::json::parse(R"({"name":"wim","font":{"family":"Cascadia Mono","size":11},"tabs":[1,2,3],"empty":{}})");
    WIM_CHECK(r.ok);
    WIM_CHECK_STREQ(r.value.get("name", std::string()), "wim");
    const Value* font = r.value.find("font");
    WIM_CHECK(font != nullptr);
    WIM_CHECK_STREQ(font->get("family", std::string()), "Cascadia Mono");
    WIM_CHECK_EQ(font->get("size", 0.0), 11.0);
    WIM_CHECK_EQ(r.value.find("tabs")->size(), size_t(3));
    WIM_CHECK_EQ(r.value.find("tabs")->items()[2].asInt(), 3LL);
    WIM_CHECK(r.value.find("empty")->members().empty());
}

WIM_TEST(JsonParseErrorsHaveOffset)
{
    auto r = wim::json::parse("{\"a\":1,}");
    WIM_CHECK(!r.ok);
    WIM_CHECK(r.errorAt > 0);

    r = wim::json::parse("[1,2] trailing");
    WIM_CHECK(!r.ok);
    WIM_CHECK_STREQ(r.error, "unexpected trailing data");

    r = wim::json::parse("");
    WIM_CHECK(!r.ok);
    WIM_CHECK(!r.error.empty());
}

WIM_TEST(JsonParseBomTolerated)
{
    WIM_CHECK(wim::json::parse("\xEF\xBB\xBF{\"a\":1}").ok);
}

WIM_TEST(JsonSerializeRoundTrip)
{
    const std::string original = R"({"n":1.5,"s":"a\"b\\c","arr":[true,false,null,"x"],"nested":{"k":[[1],[2,3]]}})";
    const auto parsed = wim::json::parse(original);
    WIM_CHECK(parsed.ok);
    const std::string out = wim::json::serialize(parsed.value);
    const auto reparsed = wim::json::parse(out);
    WIM_CHECK(reparsed.ok);
    WIM_CHECK_STREQ(wim::json::serialize(reparsed.value), out); // stable
    WIM_CHECK_EQ(reparsed.value.find("n")->asNumber(), 1.5);
}

WIM_TEST(JsonSerializeNumbersShortest)
{
    Value v = wim::json::parse("12").value;
    WIM_CHECK_STREQ(wim::json::serialize(v), "12");
    v = wim::json::parse("0.1").value;
    WIM_CHECK_STREQ(wim::json::serialize(v), "0.1");
    v = wim::json::parse("-0.125").value;
    WIM_CHECK_STREQ(wim::json::serialize(v), "-0.125");
    v = wim::json::parse("1e100").value;
    WIM_CHECK_EQ(wim::json::parse(wim::json::serialize(v)).value.asNumber(), 1e100);
}

WIM_TEST(JsonSerializePretty)
{
    Value v = Value::object();
    v.set("a", 1);
    Value inner = Value::object();
    inner.set("b", std::string("x"));
    v.set("c", inner);
    const std::string pretty = wim::json::serialize(v, true);
    WIM_CHECK(pretty.find("\n") != std::string::npos);
    WIM_CHECK(pretty.find("\": ") != std::string::npos); // ": " separator
    WIM_CHECK(wim::json::parse(pretty).ok);
}

WIM_TEST(JsonObjectOrderAndSet)
{
    Value v = Value::object();
    v.set("zeta", 1);
    v.set("alpha", 2);
    v.set("mid", 3);
    WIM_CHECK_STREQ(v.members()[0].first, "zeta");
    // Replace in place keeps position.
    v.set("alpha", 99);
    WIM_CHECK_STREQ(v.members()[1].first, "alpha");
    WIM_CHECK_EQ(v.members()[1].second.asInt(), 99LL);
    WIM_CHECK_EQ(v.members().size(), size_t(3));
    WIM_CHECK(v.remove("mid"));
    WIM_CHECK(!v.contains("mid"));
    WIM_CHECK_EQ(v.members().size(), size_t(2));
    WIM_CHECK_STREQ(v.members()[1].first, "alpha");
}

WIM_TEST(JsonGetFallbacks)
{
    const auto r = wim::json::parse(R"({"s":"text","i":7,"d":2.5,"b":true})");
    const Value& v = r.value;
    WIM_CHECK_STREQ(v.get("s", std::string("fb")), "text");
    WIM_CHECK_STREQ(v.get("missing", std::string("fb")), "fb");
    WIM_CHECK_EQ(v.get("i", 0), 7);
    WIM_CHECK_EQ(v.get("missing", 5), 5);
    // Type mismatch also falls back: "s" is a string, not a number.
    WIM_CHECK_EQ(v.get("s", 42), 42);
    WIM_CHECK_EQ(v.get("d", 0.0), 2.5);
    WIM_CHECK(v.get("b", false));
    WIM_CHECK(!v.get("s", false));
}
