// Tests for src/core/util/str.h.
#include "core/util/str.h"
#include "test.h"

using namespace wim::str;

WIM_TEST(StrUtf8RoundTrip)
{
    // ASCII
    WIM_CHECK_STREQ(toUtf8(toWide("hello")), "hello");
    // Turkish (combining and composed forms preserved byte-for-byte)
    const char* tr = "\xc4\xb1\xc5\x9f\xc4\x9f\xc3\xbc \xc3\xb6\xc3\xa7"; // ışğü öç
    WIM_CHECK_STREQ(toUtf8(toWide(tr)), tr);
    // CJK
    const char* cjk = "\xe6\xbc\xa2\xe5\xad\x97\xf0\x9f\x98\x80"; // 漢字😀
    WIM_CHECK_STREQ(toUtf8(toWide(cjk)), cjk);
    // Empty
    WIM_CHECK(toWide("").empty());
    WIM_CHECK(toUtf8(L"").empty());
}

WIM_TEST(StrUtf8InvalidDoesNotCrash)
{
    // Lone continuation bytes and truncated sequences must map to U+FFFD,
    // not throw or hang.
    const auto wide = toWide("\xff\xfe abc \xc3");
    WIM_CHECK(wide.size() >= 5);
    WIM_CHECK(wide.substr(wide.size() - 5) == L" abc " || wide.find(L" abc ") != std::wstring::npos);
}

WIM_TEST(StrTrim)
{
    WIM_CHECK_STREQ(trim("  hi\t\n"), "hi");
    WIM_CHECK_STREQ(trim("no-trim"), "no-trim");
    WIM_CHECK_STREQ(trim("   "), "");
    WIM_CHECK_STREQ(trim(""), "");
    WIM_CHECK_STREQ(trimStart(" \t\r\nx y "), "x y ");
    WIM_CHECK_STREQ(trimEnd(" x y \r\n"), " x y");
}

WIM_TEST(StrSplitJoin)
{
    const auto parts = split("a,b,,c", ',');
    WIM_CHECK_EQ(parts.size(), size_t(3));
    WIM_CHECK_STREQ(parts[0], "a");
    WIM_CHECK_STREQ(parts[1], "b");
    WIM_CHECK_STREQ(parts[2], "c");

    const auto kept = split("a,b,,c", ',', true);
    WIM_CHECK_EQ(kept.size(), size_t(4));
    WIM_CHECK_STREQ(kept[2], "");

    WIM_CHECK(split("", ',').empty());
    WIM_CHECK_EQ(split(",", ',', true).size(), size_t(2));

    WIM_CHECK_STREQ(join(split("a b c", ' '), "-"), "a-b-c");
    const std::vector<std::string_view> empty;
    WIM_CHECK_STREQ(join(empty, "/"), "");
}

WIM_TEST(StrPredicates)
{
    WIM_CHECK(startsWith("wim.exe", "wim"));
    WIM_CHECK(!startsWith("wi", "wim"));
    WIM_CHECK(endsWith("settings.json", ".json"));
    WIM_CHECK(!endsWith("settings.json", ".JSON"));
    WIM_CHECK(contains("hello world", "lo wo"));
    WIM_CHECK(!contains("hello", "xyz"));
}

WIM_TEST(StrCaseInsensitive)
{
    WIM_CHECK(iequals(":VSplit", ":vsplit"));
    WIM_CHECK(!iequals(":split", ":vspit"));
    WIM_CHECK(istartsWith("CLAUDE.EXE", "claude"));
    WIM_CHECK(iendsWith("read-pane", "-PANE"));
    WIM_CHECK(icontains("new-workspace", "WORK"));
    WIM_CHECK(!icontains("new-workspace", "zzz"));
    WIM_CHECK_STREQ(toLower("MiXeD CASE 123"), "mixed case 123");
    // Non-ASCII passes through untouched (documented: ASCII-only folding).
    WIM_CHECK_STREQ(toLower("\xc4\xb0"), "\xc4\xb0");
}

WIM_TEST(StrReplaceAll)
{
    WIM_CHECK_STREQ(replaceAll("a-b-c", "-", "+"), "a+b+c");
    WIM_CHECK_STREQ(replaceAll("aaa", "aa", "b"), "ba");
    WIM_CHECK_STREQ(replaceAll("abc", "", "x"), "abc"); // empty 'from' is a no-op
    WIM_CHECK_STREQ(replaceAll("nope", "xyz", "!"), "nope");
}

WIM_TEST(StrFormat)
{
    WIM_CHECK_STREQ(format("%s=%d", "answer", 42), "answer=42");
    // Longer than any stack buffer a naive implementation would use.
    const std::string long_ = format("%0200d", 7);
    WIM_CHECK_EQ(long_.size(), size_t(200));
    WIM_CHECK(wformat(L"%s!", L"wim") == L"wim!");
}

WIM_TEST(StrHumanBytes)
{
    WIM_CHECK_STREQ(humanBytes(0), "0 B");
    WIM_CHECK_STREQ(humanBytes(512), "512 B");
    WIM_CHECK_STREQ(humanBytes(2048), "2.0 KB");
    WIM_CHECK_STREQ(humanBytes(5 * 1024 * 1024), "5.0 MB");
}
