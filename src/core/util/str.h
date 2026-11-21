// wim - a Windows-native terminal workspace for coding agents.
// Core/util: string helpers. UTF-8 is the wire format (ConPTY, JSON, pipe
// protocol); UTF-16 is the UI format (Win32, DirectWrite). Everything that
// crosses that boundary goes through this header.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wim::str {

// ---- encoding -------------------------------------------------------------

// UTF-8 -> UTF-16. Invalid sequences become U+FFFD, never an error: terminal
// output is untrusted and must not crash the reader thread.
std::wstring toWide(std::string_view utf8);
// UTF-16 -> UTF-8.
std::string toUtf8(std::wstring_view wide);

// ---- trimming / splitting / joining ---------------------------------------

std::string_view trim(std::string_view s);
std::string_view trimStart(std::string_view s);
std::string_view trimEnd(std::string_view s);

std::vector<std::string_view> split(std::string_view s, char sep,
                                    bool keepEmpty = false);
std::string join(const std::vector<std::string_view>& parts, std::string_view sep);
std::string join(const std::vector<std::string>& parts, std::string_view sep);

// ---- predicates and case --------------------------------------------------

bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool contains(std::string_view s, std::string_view needle);
// ASCII-only case-insensitive comparison, for command names and keys.
bool iequals(std::string_view a, std::string_view b);
bool istartsWith(std::string_view s, std::string_view prefix);
bool iendsWith(std::string_view s, std::string_view suffix);
bool icontains(std::string_view s, std::string_view needle);

std::string toLower(std::string_view s);      // ASCII
std::wstring toLowerWide(std::wstring_view s); // full (LCMapStringEx, invariant)

// ---- mutation helpers -----------------------------------------------------

std::string replaceAll(std::string_view s, std::string_view from, std::string_view to);

// ---- formatting -----------------------------------------------------------

// printf-style formatting into a fresh std::string (UTF-8, narrow).
std::string format(const char* fmt, ...);
std::wstring wformat(const wchar_t* fmt, ...);

// Renders a size in bytes the way Explorer does: "1.2 MB".
std::string humanBytes(unsigned long long bytes);

} // namespace wim::str
