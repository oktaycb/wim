#include "str.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace wim::str {

// ---- encoding -------------------------------------------------------------

std::wstring toWide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        utf8.data(), (int)utf8.size(), nullptr, 0);
    if (len > 0) {
        std::wstring out(size_t(len), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                            (int)utf8.size(), out.data(), len);
        return out;
    }
    // Invalid input: replace faulty sequences instead of failing (MB_ERR_INVALID_CHARS
    // is off for this pass, so bad bytes map to U+FFFD individually).
    const int len2 = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         (int)utf8.size(), nullptr, 0);
    std::wstring out(size_t(len2), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), len2);
    return out;
}

std::string toUtf8(std::wstring_view wide)
{
    if (wide.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::string out(size_t(len), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                            (int)wide.size(), out.data(), len, nullptr, nullptr);
        return out;
    }
    const int len2 = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(len2), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), out.data(), len2,
                        nullptr, nullptr);
    return out;
}

// ---- trimming / splitting / joining ---------------------------------------

std::string_view trimStart(std::string_view s)
{
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
    return s.substr(i);
}

std::string_view trimEnd(std::string_view s)
{
    size_t end = s.size();
    while (end > 0) {
        const char c = s[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            --end;
        else
            break;
    }
    return s.substr(0, end);
}

std::string_view trim(std::string_view s) { return trimEnd(trimStart(s)); }

std::vector<std::string_view> split(std::string_view s, char sep, bool keepEmpty)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (;;) {
        const size_t next = s.find(sep, start);
        if (next == std::string_view::npos) {
            const auto last = s.substr(start);
            if (keepEmpty || !last.empty())
                parts.push_back(last);
            break;
        }
        const auto part = s.substr(start, next - start);
        if (keepEmpty || !part.empty())
            parts.push_back(part);
        start = next + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string_view>& parts, std::string_view sep)
{
    std::string out;
    size_t total = sep.size() * (parts.size() > 0 ? parts.size() - 1 : 0);
    for (auto& p : parts)
        total += p.size();
    out.reserve(total);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.append(sep);
        out.append(parts[i]);
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep)
{
    std::vector<std::string_view> views(parts.begin(), parts.end());
    return join(views, sep);
}

// ---- predicates and case ----------------------------------------------------

namespace {
bool lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
char foldChar(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
} // namespace

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(std::string_view s, std::string_view needle)
{
    return s.find(needle) != std::string_view::npos;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (foldChar(a[i]) != foldChar(b[i]))
            return false;
    return true;
}

bool istartsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iendsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() &&
           iequals(s.substr(s.size() - suffix.size()), suffix);
}

bool icontains(std::string_view s, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > s.size())
        return false;
    for (size_t i = 0; i + needle.size() <= s.size(); ++i)
        if (iequals(s.substr(i, needle.size()), needle))
            return true;
    return false;
}

std::string toLower(std::string_view s)
{
    std::string out(s);
    for (auto& c : out)
        c = foldChar(c);
    return out;
}

std::wstring toLowerWide(std::wstring_view s)
{
    std::wstring out(s);
    if (!out.empty())
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, out.data(), (int)out.size(),
                      out.data(), (int)out.size(), nullptr, nullptr, 0);
    return out;
}

// ---- mutation helpers -------------------------------------------------------

std::string replaceAll(std::string_view s, std::string_view from, std::string_view to)
{
    if (from.empty())
        return std::string(s);
    std::string out;
    out.reserve(s.size());
    size_t start = 0;
    for (;;) {
        const size_t next = s.find(from, start);
        if (next == std::string_view::npos) {
            out.append(s.substr(start));
            return out;
        }
        out.append(s.substr(start, next - start));
        out.append(to);
        start = next + from.size();
    }
}

// ---- formatting -------------------------------------------------------------

std::string format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(size_t(needed > 0 ? needed : 0), '\0');
    if (needed > 0)
        vsnprintf(out.data(), out.size() + 1, fmt, args);
    va_end(args);
    return out;
}

std::wstring wformat(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = _vsnwprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::wstring out(size_t(needed > 0 ? needed : 0), L'\0');
    if (needed > 0)
        _vsnwprintf(out.data(), out.size() + 1, fmt, args);
    va_end(args);
    return out;
}

std::string humanBytes(unsigned long long bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return format("%llu B", bytes);
    return format("%.1f %s", value, units[unit]);
}

} // namespace wim::str
