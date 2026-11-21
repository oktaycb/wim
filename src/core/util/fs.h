// wim - a Windows-native terminal workspace for coding agents.
// Core/util: filesystem helpers for the app's well-known locations and
// small text files (settings, session, log).
#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace wim::fs {

// %APPDATA%\wim - settings.json, session.json live here.
std::wstring appDataDir();
// %LOCALAPPDATA%\wim - wim.log lives here.
std::wstring localDataDir();
// Directory of the running wim.exe (for PATH setup in panes).
std::wstring exeDir();
// The user's profile folder (%USERPROFILE%).
std::wstring homeDir();

std::wstring join(std::wstring_view a, std::wstring_view b);

bool exists(std::wstring_view path);
std::optional<unsigned long long> fileSize(std::wstring_view path);

// Creates every missing component. Returns false on failure (GetLastError()
// holds the reason). Existing directories are success.
bool ensureDir(std::wstring_view path);

// Reads a whole small file as UTF-8. nullopt when missing/unreadable.
std::optional<std::string> readText(std::wstring_view path);
// Writes text atomically: <path>.tmp then MoveFileEx REPLACE_EXISTING, so a
// crash mid-write can never leave a truncated settings.json behind.
bool writeTextAtomic(std::wstring_view path, std::string_view utf8);

// Expands a leading "~" to the profile folder. Paths without one return
// unchanged.
std::wstring expandTilde(std::wstring_view path);

} // namespace wim::fs
