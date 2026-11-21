// Integration tests for the ConPTY host.
//
// ConPTY child attachment depends on the PARENT's console state: when this
// test binary runs inside an already-attached console (a terminal, CI job,
// or test harness), a fresh console child can attach to that inherited
// console instead of the pseudoconsole. wim.exe itself is a GUI process
// and never hits this; to test the real path the "live" tests re-launch
// this binary with DETACHED_PROCESS (no console at all) and only the pty
// tests enabled, then report through the exit code.
#include "core/conpty/host.h"
#include "core/util/str.h"
#include "test.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace wim::conpty;

namespace {

int runPtyChildTests()
{
    int failed = 0;

    {
        Host host;
        std::wstring error;
        if (!host.start(L"cmd.exe /c echo wim-conpty-ok", L"", 80, 25, std::nullopt, error)) {
            ++failed;
        } else {
            std::string output;
            char buf[4096];
            for (int i = 0; i < 200 && host.running(); ++i) {
                const auto got = host.readOutput(buf, sizeof(buf));
                if (!got.has_value())
                    break;
                if (*got > 0)
                    output.append(buf, *got);
                else
                    Sleep(20);
            }
            for (int i = 0; i < 25; ++i) { // drain after exit
                const auto got = host.readOutput(buf, sizeof(buf));
                if (!got.has_value() || *got == 0)
                    break;
                output.append(buf, *got);
                Sleep(20);
            }
            if (!wim::str::contains(wim::str::toLower(output), "wim-conpty-ok"))
                ++failed;
        }
    }

    {
        Host host;
        std::wstring error;
        if (!host.start(L"cmd.exe /k prompt $G", L"", 80, 25, std::nullopt, error)) {
            ++failed;
        } else {
            char buf[4096];
            for (int i = 0; i < 100; ++i) { // wait for the banner
                const auto got = host.readOutput(buf, sizeof(buf));
                if (got.has_value() && *got > 0)
                    break;
                if (!got.has_value())
                    break;
                Sleep(20);
            }
            host.writeInput("echo wim-input-ok\r", 18);
            std::string output;
            for (int i = 0; i < 150; ++i) {
                const auto got = host.readOutput(buf, sizeof(buf));
                if (got.has_value() && *got > 0) {
                    output.append(buf, *got);
                    if (wim::str::contains(output, "wim-input-ok"))
                        break;
                }
                Sleep(20);
            }
            if (!wim::str::contains(wim::str::toLower(output), "wim-input-ok"))
                ++failed;
            host.writeInput("exit\r", 5);
            for (int i = 0; i < 100 && host.running(); ++i)
                Sleep(20);
            host.terminate();
        }
    }

    return failed;
}

// True when this process has no console (the detached child mode).
bool consoleLess()
{
    return GetConsoleWindow() == nullptr;
}

} // namespace

// Called from tests/main.cpp when argv contains --conpty-child.
int wimConPtyChildMain()
{
    return runPtyChildTests();
}

WIM_TEST(ConPtySuiteViaDetachedChild)
{
    (void)consoleLess();
    wchar_t exe[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)std::size(exe));
    WIM_CHECK(n > 0 && n < std::size(exe));

    std::wstring cmd = std::wstring(exe) + L" --conpty-child";
    // A hidden console of its own gives the child the clean, proven-working
    // attachment context (same as the GUI app's); inheriting this process's
    // console state is exactly what misroutes children.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    wchar_t mutableCmd[1024];
    wcscpy_s(mutableCmd, cmd.c_str());
    const BOOL ok = CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE,
                                   CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    WIM_CHECK(ok);
    if (!ok)
        return;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    WIM_CHECK_EQ(code, DWORD(0)); // child ran both pty scenarios clean
}

WIM_TEST(ConPtyEnvironmentBlock)
{
    const std::wstring env = buildEnvironment({L"WIM_PANE_TEST=42", L"WIM_EXTRA=hello"});
    WIM_CHECK(env.find(L"WIM_PANE_TEST=42") != std::wstring::npos);
    WIM_CHECK(env.find(L"WIM_EXTRA=hello") != std::wstring::npos);
    WIM_CHECK(env.find(L"PATH=") != std::wstring::npos || env.find(L"path=") != std::wstring::npos);
    // Replacement, not duplication.
    size_t count = 0;
    for (size_t pos = 0; (pos = env.find(L"WIM_PANE_TEST=", pos)) != std::wstring::npos; ++pos)
        ++count;
    WIM_CHECK_EQ(count, size_t(1));
    // Well-formed: ends with two NULs.
    WIM_CHECK(env.size() >= 2 && env[env.size() - 1] == L'\0' && env[env.size() - 2] == L'\0');
}

WIM_TEST(ConPtyStartRejectsBadCommand)
{
    Host host;
    std::wstring error;
    WIM_CHECK(!host.start(L"definitely-not-a-real-program-xyz.exe", L"", 80, 25,
                          std::nullopt, error));
    WIM_CHECK(!error.empty());
    WIM_CHECK(!host.valid());
}
