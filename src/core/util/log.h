// wim - a Windows-native terminal workspace for coding agents.
// Core/util: diagnostics log. One process-wide log file under
// %LOCALAPPDATA%\wim\wim.log, written from any thread.
#pragma once

#include <string_view>

namespace wim::log {

enum class Level : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5,
};

// Opens (or switches) the log file. Creates parent directories. Safe to
// call once at startup; without a file, messages go to OutputDebugString
// only. Returns false when the file cannot be opened (logging then stays
// in-memory/ODS, never fatal).
bool openFile(std::wstring_view path, Level level = Level::Debug);

// Sets the minimum level that is written (runtime filter).
void setLevel(Level level);
Level level();

// Writes one line. Use the typed helpers below instead of calling this.
void write(Level level, const char* file, int line, std::string_view message);

// printf-style helpers. The formatted result must stay on one line.
void trace(const char* f, int l, const char* fmt, ...);
void debug(const char* f, int l, const char* fmt, ...);
void info(const char* f, int l, const char* fmt, ...);
void warn(const char* f, int l, const char* fmt, ...);
void error(const char* f, int l, const char* fmt, ...);

} // namespace wim::log

#define WIM_LOG_TRACE(...) ::wim::log::trace(__FILE__, __LINE__, __VA_ARGS__)
#define WIM_LOG_DEBUG(...) ::wim::log::debug(__FILE__, __LINE__, __VA_ARGS__)
#define WIM_LOG_INFO(...) ::wim::log::info(__FILE__, __LINE__, __VA_ARGS__)
#define WIM_LOG_WARN(...) ::wim::log::warn(__FILE__, __LINE__, __VA_ARGS__)
#define WIM_LOG_ERROR(...) ::wim::log::error(__FILE__, __LINE__, __VA_ARGS__)
