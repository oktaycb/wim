// Tests for log and fs: real file I/O against the temp directory.
#include "core/util/fs.h"
#include "core/util/log.h"
#include "core/util/str.h"
#include "test.h"

#include <windows.h>

namespace {
std::wstring tempDirFor(const char* name)
{
    wchar_t dir[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, dir);
    std::wstring base(dir, n);
    std::wstring p = wim::fs::join(base, wim::str::toWide(std::string("wimtest-") + name));
    wim::fs::ensureDir(p);
    return p;
}
} // namespace

WIM_TEST(LogWritesAndReopens)
{
    const std::wstring dir = tempDirFor("log");
    const std::wstring file = wim::fs::join(dir, L"wim.log");
    DeleteFileW(file.c_str());

    WIM_CHECK(wim::log::openFile(file, wim::log::Level::Trace));
    wim::log::info(__FILE__, __LINE__, "hello %d", 42);
    wim::log::warn(__FILE__, __LINE__, "line two");
    // Reopen flushes nothing (writes are unbuffered) but must keep appending.
    WIM_CHECK(wim::log::openFile(file, wim::log::Level::Trace));
    wim::log::error(__FILE__, __LINE__, "after reopen");

    const auto text = wim::fs::readText(file);
    WIM_CHECK(text.has_value());
    WIM_CHECK(wim::str::contains(*text, "hello 42"));
    WIM_CHECK(wim::str::contains(*text, "line two"));
    WIM_CHECK(wim::str::contains(*text, "after reopen"));
    WIM_CHECK(wim::str::contains(*text, "[error]"));
    // Level filter: Trace suppressed when the bar is at Info.
    wim::log::openFile(file, wim::log::Level::Info);
    wim::log::trace(__FILE__, __LINE__, "should not appear");
    const auto text2 = wim::fs::readText(file);
    WIM_CHECK(!wim::str::contains(*text2, "should not appear"));
}

WIM_TEST(FsWellKnownPaths)
{
    const std::wstring app = wim::fs::appDataDir();
    WIM_CHECK(app.size() > 4);
    WIM_CHECK(wim::str::toLowerWide(app).find(L"wim") != std::wstring::npos);
    // appDataDir is under Roaming, localDataDir under Local.
    WIM_CHECK(wim::fs::appDataDir() != wim::fs::localDataDir());
    WIM_CHECK(!wim::fs::exeDir().empty());
    WIM_CHECK(!wim::fs::homeDir().empty());
    WIM_CHECK(wim::str::toLowerWide(wim::fs::homeDir()).find(L"users") != std::wstring::npos);
}

WIM_TEST(FsJoin)
{
    WIM_CHECK(wim::fs::join(L"C:\\a", L"b") == L"C:\\a\\b");
    WIM_CHECK(wim::fs::join(L"C:\\a\\", L"b") == L"C:\\a\\b");
    WIM_CHECK(wim::fs::join(L"C:\\a", L"\\b") == L"C:\\a\\b");
    WIM_CHECK(wim::fs::join(L"", L"b") == L"b");
    WIM_CHECK(wim::fs::join(L"a", L"") == L"a");
}

WIM_TEST(FsReadWriteAtomic)
{
    const std::wstring dir = tempDirFor("rw");
    const std::wstring file = wim::fs::join(dir, L"file.txt");
    DeleteFileW(file.c_str());

    WIM_CHECK(!wim::fs::readText(file).has_value());
    WIM_CHECK(wim::fs::writeTextAtomic(file, "first"));
    const auto a = wim::fs::readText(file);
    WIM_CHECK(a.has_value() && *a == "first");
    WIM_CHECK(wim::fs::writeTextAtomic(file, "second version"));
    const auto b = wim::fs::readText(file);
    WIM_CHECK(b.has_value() && *b == "second version");
    WIM_CHECK(wim::fs::fileSize(file).value_or(0) == 14);
    // No .tmp litter left behind.
    WIM_CHECK(!wim::fs::exists(file + L".tmp"));
}

WIM_TEST(FsEnsureDirDeep)
{
    const std::wstring dir = tempDirFor("ensure");
    const std::wstring deep = wim::fs::join(dir, L"a\\b\\c\\d");
    WIM_CHECK(wim::fs::ensureDir(deep));
    WIM_CHECK(wim::fs::exists(deep));
    // Idempotent.
    WIM_CHECK(wim::fs::ensureDir(deep));
    WIM_CHECK(wim::fs::exists(deep));
}

WIM_TEST(FsExpandTilde)
{
    WIM_CHECK(wim::fs::expandTilde(L"~").size() >= 3);
    WIM_CHECK(wim::fs::expandTilde(L"~\\x").rfind(L'\\') != std::wstring::npos);
    WIM_CHECK(wim::fs::expandTilde(L"C:\\plain") == L"C:\\plain");
    WIM_CHECK(wim::fs::expandTilde(L"~bob") == L"~bob"); // unsupported, unchanged
}
