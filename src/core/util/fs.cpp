#include "fs.h"
#include "str.h"

#include <windows.h>
#include <shlobj.h>

namespace wim::fs {

namespace {

std::wstring knownDir(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) {
        out.assign(p);
        CoTaskMemFree(p);
    }
    return out;
}

bool isSlash(wchar_t c) { return c == L'\\' || c == L'/'; }

} // namespace

std::wstring appDataDir()
{
    std::wstring base = knownDir(FOLDERID_RoamingAppData);
    return base.empty() ? std::wstring(L".") : join(base, L"wim");
}

std::wstring localDataDir()
{
    std::wstring base = knownDir(FOLDERID_LocalAppData);
    return base.empty() ? std::wstring(L".") : join(base, L"wim");
}

std::wstring exeDir()
{
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf))
        return L".";
    std::wstring_view full(buf, n);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos)
        return L".";
    return std::wstring(full.substr(0, slash));
}

std::wstring homeDir()
{
    std::wstring home = knownDir(FOLDERID_Profile);
    return home.empty() ? std::wstring(L".") : home;
}

std::wstring join(std::wstring_view a, std::wstring_view b)
{
    if (a.empty())
        return std::wstring(b);
    if (b.empty())
        return std::wstring(a);
    std::wstring out(a);
    if (!isSlash(out.back()))
        out += L'\\';
    if (isSlash(b.front()))
        b.remove_prefix(1);
    out += b;
    return out;
}

bool exists(std::wstring_view path)
{
    return GetFileAttributesW(std::wstring(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::optional<unsigned long long> fileSize(std::wstring_view path)
{
    HANDLE f = CreateFileW(std::wstring(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return std::nullopt;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(f, &size) != FALSE;
    CloseHandle(f);
    if (!ok)
        return std::nullopt;
    return (unsigned long long)size.QuadPart;
}

bool ensureDir(std::wstring_view path)
{
    if (path.empty())
        return false;
    std::wstring p(path);
    for (auto& c : p)
        if (c == L'/')
            c = L'\\';
    while (p.size() > 3 && p.back() == L'\\')
        p.pop_back();

    const DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // Create the parent first, but never try to split a drive root ("C:\")
    // or the root of a UNC path.
    const size_t slash = p.find_last_of(L'\\');
    if (slash != std::wstring::npos && slash >= 3) {
        const std::wstring parent = p.substr(0, slash);
        if (parent != p && !ensureDir(parent))
            return false;
    }
    if (CreateDirectoryW(p.c_str(), nullptr))
        return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::optional<std::string> readText(std::wstring_view path)
{
    // Files may be held open by a writer (the log) or in the middle of an
    // atomic replace; reading is always allowed.
    HANDLE f = CreateFileW(std::wstring(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return std::nullopt;
    std::string out;
    char buf[64 * 1024];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf, sizeof(buf), &got, nullptr)) {
            CloseHandle(f);
            return std::nullopt;
        }
        if (got == 0)
            break;
        out.append(buf, got);
    }
    CloseHandle(f);
    return out;
}

bool writeTextAtomic(std::wstring_view path, std::string_view utf8)
{
    const std::wstring p(path);
    const std::wstring tmp = p + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr) &&
                    written == utf8.size();
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::wstring expandTilde(std::wstring_view path)
{
    if (path.empty() || path[0] != L'~')
        return std::wstring(path);
    if (path.size() == 1 || isSlash(path[1]))
        return join(homeDir(), path.substr(1));
    return std::wstring(path); // "~user" is not supported; leave as-is
}

} // namespace wim::fs
