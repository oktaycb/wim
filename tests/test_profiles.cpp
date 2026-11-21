// Tests for shell profile detection and parsing.
#include "core/shell/profiles.h"
#include "core/util/str.h"
#include "test.h"

#include <string>
#include <vector>

using namespace wim::shell;

WIM_TEST(ProfilesQuoteIfNeeded)
{
    WIM_CHECK(quoteIfNeeded(L"cmd.exe") == L"cmd.exe");
    WIM_CHECK(quoteIfNeeded(L"C:\\Program Files\\Git\\bin\\bash.exe") ==
              L"\"C:\\Program Files\\Git\\bin\\bash.exe\"");
}

WIM_TEST(ProfilesDetectFindsCmd)
{
    const auto profiles = detectProfiles();
    WIM_CHECK(profiles.size() >= 2); // cmd + powershell always
    bool hasCmd = false, hasPwsh = false, hasDefault = false;
    for (const auto& p : profiles) {
        if (p.id == L"cmd")
            hasCmd = true;
        if (p.id == L"pwsh")
            hasPwsh = true;
        if (p.isDefault)
            hasDefault = true;
        WIM_CHECK(!p.id.empty());
        WIM_CHECK(!p.commandLine.empty());
        WIM_CHECK(!p.displayName.empty());
    }
    WIM_CHECK(hasCmd);
    WIM_CHECK(hasDefault);
    (void)hasPwsh; // machine-dependent

    const Profile def = defaultProfile();
    WIM_CHECK(!def.id.empty());
    WIM_CHECK(findProfile(profiles, L"CMD") != nullptr);   // case-insensitive
    WIM_CHECK(findProfile(profiles, L"nope") == nullptr);
}

WIM_TEST(ProfilesWslListUtf16)
{
    // wsl.exe prints UTF-16LE with a BOM; simulate it.
    const std::wstring distros = L"Ubuntu-22.04\r\nDebian\r\n";
    std::string raw = "\xFF\xFE";
    for (wchar_t c : distros) {
        raw += char(c & 0xFF);
        raw += char(c >> 8);
    }
    const auto parsed = parseWslList(raw);
    WIM_CHECK_EQ(parsed.size(), size_t(2));
    if (parsed.size() == 2) {
        WIM_CHECK(parsed[0] == L"Debian"); // sorted
        WIM_CHECK(parsed[1] == L"Ubuntu-22.04");
    }
}

WIM_TEST(ProfilesWslListUtf8)
{
    const auto parsed = parseWslList("Ubuntu\r\nopenSUSE\r\n\r\n");
    WIM_CHECK_EQ(parsed.size(), size_t(2));
    WIM_CHECK(parsed[1] == L"Ubuntu");
}

WIM_TEST(ProfilesBuildCommand)
{
    const Profile pwsh{L"pwsh", L"PowerShell", L"pwsh.exe -NoLogo", true};
    WIM_CHECK(buildCommand(pwsh, L"C:\\dev") == L"pwsh.exe -NoLogo -WorkingDirectory C:\\dev");
    WIM_CHECK(buildCommand(pwsh, L"") == L"pwsh.exe -NoLogo");

    const Profile cmd{L"cmd", L"cmd", L"cmd.exe", false};
    WIM_CHECK(buildCommand(cmd, L"C:\\Program Files") == L"cmd.exe /K cd /d \"C:\\Program Files\"");

    const Profile wsl{L"wsl-Ubuntu", L"Ubuntu", L"wsl.exe -d Ubuntu", false};
    WIM_CHECK(buildCommand(wsl, L"C:\\dev") == L"wsl.exe -d Ubuntu --cd C:\\dev");

    const Profile bash{L"gitbash", L"Git Bash", L"bash.exe -i -l", false};
    WIM_CHECK(buildCommand(bash, L"C:\\my code") == L"bash.exe -i -l --cd \"C:\\my code\"");
}
