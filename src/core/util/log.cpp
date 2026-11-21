#include "log.h"
#include "str.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace wim::log {

namespace {

constexpr size_t kMaxLogBytes = 1 * 1024 * 1024; // rotate at 1 MB
constexpr const char* kBackupSuffix = ".old";

struct State {
    std::mutex mutex;
    HANDLE file = nullptr;
    std::wstring path;
    Level level = Level::Debug;
    bool toDebugger = true;
};

State& state()
{
    static State s;
    return s;
}

void closeFileLocked(State& s)
{
    if (s.file) {
        CloseHandle(s.file);
        s.file = nullptr;
    }
}

const char* levelName(Level l)
{
    switch (l) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info ";
    case Level::Warn: return "warn ";
    case Level::Error: return "error";
    default: return "off  ";
    }
}

// "%04u-%02u-%02u %02u:%02u:%02u.%03u " from a SYSTEMTIME.
std::string timestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return str::format("%04u-%02u-%02u %02u:%02u:%02u.%03u ", st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

std::string shortenSource(const char* file)
{
    // Keep from the last component on: "src/core/util/log.cpp" -> "util/log.cpp".
    std::string_view sv(file);
    size_t cut = 0;
    for (size_t i = 0; i + 1 < sv.size(); ++i)
        if (sv[i] == '/' || sv[i] == '\\')
            cut = i + 1;
    // Find one more separator back, to keep the parent folder as context.
    for (size_t i = cut; i > 0; --i) {
        if (sv[i - 1] == '/' || sv[i - 1] == '\\')
            return std::string(sv.substr(i));
    }
    return std::string(sv.substr(cut));
}

void rotateIfNeededLocked(State& s)
{
    LARGE_INTEGER size{};
    if (s.file && GetFileSizeEx(s.file, &size) && size.QuadPart > (LONGLONG)kMaxLogBytes) {
        closeFileLocked(s);
        const std::wstring backup = s.path + L".old";
        DeleteFileW(backup.c_str());
        MoveFileW(s.path.c_str(), backup.c_str());
        s.file = CreateFileW(s.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
}

} // namespace

bool openFile(std::wstring_view path, Level level)
{
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    closeFileLocked(s);
    s.level = level;

    s.path.assign(path);
    // Ensure the parent directory exists.
    if (const size_t slash = s.path.find_last_of(L"\\/"); slash != std::wstring::npos) {
        std::wstring dir = s.path.substr(0, slash);
        CreateDirectoryW(dir.c_str(), nullptr); // fails harmlessly if present
    }
    s.file = CreateFileW(s.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return s.file != nullptr;
}

void setLevel(Level level)
{
    state().level = level;
}

Level level()
{
    return state().level;
}

void write(Level level, const char* file, int line, std::string_view message)
{
    State& s = state();
    if (level < s.level || level >= Level::Off)
        return;

    std::string text = timestamp();
    text += "[";
    text += levelName(level);
    text += "] [";
    text += shortenSource(file);
    text += ":";
    text += std::to_string(line);
    text += "] ";
    text += message;
    text += "\r\n";

    if (s.toDebugger)
        OutputDebugStringA(text.c_str());

    std::lock_guard<std::mutex> lock(s.mutex);
    rotateIfNeededLocked(s);
    if (s.file) {
        DWORD written = 0;
        WriteFile(s.file, text.data(), (DWORD)text.size(), &written, nullptr);
    }
}

namespace {

void vlogf(Level level, const char* f, int l, const char* fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string msg(size_t(needed > 0 ? needed : 0), '\0');
    if (needed > 0)
        vsnprintf(msg.data(), msg.size() + 1, fmt, args);
    write(level, f, l, msg);
}

} // namespace

#define WIM_DEFINE_LOGFN(name, lvl)                                                                \
    void name(const char* f, int l, const char* fmt, ...)                                          \
    {                                                                                              \
        va_list args;                                                                              \
        va_start(args, fmt);                                                                       \
        vlogf(lvl, f, l, fmt, args);                                                               \
        va_end(args);                                                                              \
    }

WIM_DEFINE_LOGFN(trace, Level::Trace)
WIM_DEFINE_LOGFN(debug, Level::Debug)
WIM_DEFINE_LOGFN(info, Level::Info)
WIM_DEFINE_LOGFN(warn, Level::Warn)
WIM_DEFINE_LOGFN(error, Level::Error)

#undef WIM_DEFINE_LOGFN

} // namespace wim::log
