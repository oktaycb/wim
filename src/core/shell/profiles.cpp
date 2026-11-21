#include "profiles.h"
#include "core/util/str.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace wim::shell {

namespace {

bool fileExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Resolves a program on PATH by searching PATHEXT directories the way
// CreateProcess would. Returns the full path or an empty string.
std::wstring findOnPath(const std::wstring& name)
{
    const wchar_t* exts[] = {L".exe", L".bat", L".cmd", L""};
    wchar_t* pathsVar = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pathsVar, &len, L"PATH") != 0 || !pathsVar)
        return L"";
    std::wstring result;
    const std::string paths8 = str::toUtf8(pathsVar);
    for (const auto part : str::split(paths8, ';')) {
        if (part.empty())
            continue;
        for (const wchar_t* ext : exts) {
            const std::wstring candidate =
                str::toWide(part) + L"\\" + name + ext;
            if (fileExists(candidate)) {
                result = candidate;
                break;
            }
        }
        if (!result.empty())
            break;
    }
    free(pathsVar);
    return result;
}

std::wstring gitBashPath()
{
    // Standard Git for Windows locations, newest first.
    const wchar_t* candidates[] = {
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
    };
    for (const wchar_t* c : candidates)
        if (fileExists(c))
            return c;
    // PATH fallback: the bash that ships with Git.
    return findOnPath(L"bash");
}

std::string runCapture(const wchar_t* cmd)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0))
        return {};
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW;
    si.hStdError = outW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    wchar_t buf[512];
    swprintf_s(buf, cmd);
    if (!CreateProcessW(nullptr, buf, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(outR);
        CloseHandle(outW);
        return {};
    }
    CloseHandle(pi.hThread);
    CloseHandle(outW);

    std::string out;
    char chunk[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(outR, chunk, sizeof(chunk), &got, nullptr) || got == 0)
            break;
        out.append(chunk, got);
    }
    CloseHandle(outR);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    return out;
}

} // namespace

std::vector<std::wstring> parseWslList(const std::string& raw)
{
    std::wstring wide;
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        // UTF-16LE with BOM.
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            wide += wchar_t((unsigned char)raw[i] | ((unsigned char)raw[i + 1] << 8));
    } else {
        wide = str::toWide(raw);
    }

    std::vector<std::wstring> distros;
    // Materialize before splitting: string_views must not point into a
    // temporary (range-for extension does not reliably cover this shape).
    const std::string text = str::toUtf8(wide);
    for (const auto lineView : str::split(text, '\n', true)) {
        std::wstring line = str::toWide(lineView);
        // Strip \r and any BOM remnants.
        while (!line.empty() && (line.back() == L'\r' || line.back() == 0xFEFF))
            line.pop_back();
        while (!line.empty() && line.front() == 0xFEFF)
            line.erase(0, 1);
        if (line.empty() || line.find_first_not_of(L" \t") == std::wstring::npos)
            continue;
        // Skip the banner Windows prints on some builds.
        if (line.find(L"Windows Subsystem") != std::wstring::npos)
            continue;
        if (line.find(L"SURU Sistem") != std::wstring::npos) // localized banner prefix
            continue;
        distros.push_back(line);
    }
    std::sort(distros.begin(), distros.end(), [](const std::wstring& a, const std::wstring& b) {
        return str::toLowerWide(a) < str::toLowerWide(b);
    });
    return distros;
}

std::wstring quoteIfNeeded(const std::wstring& argument)
{
    if (argument.find_first_of(L" \t") == std::wstring::npos)
        return argument;
    return L"\"" + argument + L"\"";
}

std::vector<Profile> detectProfiles()
{
    std::vector<Profile> out;

    if (const std::wstring pwsh = findOnPath(L"pwsh"); !pwsh.empty()) {
        out.push_back({L"pwsh", L"PowerShell", quoteIfNeeded(pwsh) + L" -NoLogo", true});
    }
    // Windows PowerShell exists everywhere.
    {
        std::wstring powershell = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (fileExists(powershell))
            out.push_back({L"powershell", L"Windows PowerShell",
                           quoteIfNeeded(powershell) + L" -NoLogo", out.empty()});
    }
    out.push_back({L"cmd", L"Command Prompt", L"cmd.exe", out.empty()});

    if (const std::wstring bash = gitBashPath(); !bash.empty()) {
        out.push_back({L"gitbash", L"Git Bash", quoteIfNeeded(bash) + L" -i -l", false});
    }

    const std::string wslRaw = runCapture(L"wsl.exe --list --quiet");
    if (!wslRaw.empty()) {
        for (const auto& distro : parseWslList(wslRaw)) {
            Profile p;
            p.id = L"wsl-" + distro;
            p.displayName = distro + L" (WSL)";
            p.commandLine = L"wsl.exe -d " + quoteIfNeeded(distro);
            out.push_back(std::move(p));
        }
    }

    return out;
}

Profile defaultProfile()
{
    const auto profiles = detectProfiles();
    for (const auto& p : profiles)
        if (p.isDefault)
            return p;
    return {L"cmd", L"Command Prompt", L"cmd.exe", true};
}

const Profile* findProfile(const std::vector<Profile>& profiles, const std::wstring& id)
{
    for (const auto& p : profiles)
        if (str::iequals(str::toUtf8(p.id), str::toUtf8(id)))
            return &p;
    return nullptr;
}

std::wstring buildCommand(const Profile& profile, const std::wstring& cwd)
{
    if (cwd.empty())
        return profile.commandLine;
    const std::wstring quoted = quoteIfNeeded(cwd);
    const std::string id = str::toUtf8(profile.id);
    if (str::istartsWith(id, "wsl-") || str::iequals(id, "gitbash"))
        return profile.commandLine + L" --cd " + quoted;
    if (str::iequals(id, "pwsh") || str::iequals(id, "powershell"))
        return profile.commandLine + L" -WorkingDirectory " + quoted;
    // cmd has no flag; a leading cd works for it.
    return L"cmd.exe /K cd /d " + quoted;
}

} // namespace wim::shell
