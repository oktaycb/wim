#include "json.h"
#include "core/util/str.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace wim::json {

Value::Value(std::wstring_view s) : type_(Type::String), str_(str::toUtf8(s)) {}

Value::Value(const std::wstring& s) : type_(Type::String), str_(str::toUtf8(s)) {}

long long Value::asInt(long long fallback) const
{
    if (!isNumber())
        return fallback;
    return (long long)llround(num_);
}

Value* Value::find(std::string_view key)
{
    for (auto& [k, v] : obj_)
        if (k == key)
            return &v;
    return nullptr;
}

const Value* Value::find(std::string_view key) const
{
    for (const auto& [k, v] : obj_)
        if (k == key)
            return &v;
    return nullptr;
}

void Value::set(std::string key, Value v)
{
    for (auto& [k, existing] : obj_) {
        if (k == key) {
            existing = std::move(v);
            return;
        }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}

bool Value::remove(std::string_view key)
{
    for (size_t i = 0; i < obj_.size(); ++i) {
        if (obj_[i].first == key) {
            obj_.erase(obj_.begin() + long long(i));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

struct Parser {
    std::string_view in;
    size_t pos = 0;
    std::string error;

    bool fail(const std::string& what)
    {
        if (error.empty())
            error = what;
        return false;
    }

    void skipWs()
    {
        while (pos < in.size()) {
            const char c = in[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++pos;
            else
                break;
        }
    }

    char peek()
    {
        return pos < in.size() ? in[pos] : '\0';
    }

    bool expect(char c, const char* what)
    {
        if (peek() != c)
            return fail(std::string("expected ") + what);
        ++pos;
        return true;
    }

    bool literal(std::string_view word)
    {
        if (in.size() - pos < word.size() || in.compare(pos, word.size(), word) != 0)
            return fail("invalid literal");
        pos += word.size();
        return true;
    }

    bool parseString(std::string& out)
    {
        if (!expect('"', "'\"'"))
            return false;
        out.clear();
        for (;;) {
            if (pos >= in.size())
                return fail("unterminated string");
            const unsigned char c = (unsigned char)in[pos];
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c == '\\') {
                ++pos;
                if (pos >= in.size())
                    return fail("unterminated escape");
                const char e = in[pos++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!parseHex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: must be followed by \uDC00-\uDFFF.
                        if (pos + 1 < in.size() && in[pos] == '\\' && in[pos + 1] == 'u') {
                            pos += 2;
                            unsigned lo = 0;
                            if (!parseHex4(lo))
                                return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                appendUtf8(out, 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00));
                                break;
                            }
                        }
                        return fail("unpaired surrogate");
                    }
                    if (cp >= 0xDC00 && cp <= 0xDFFF)
                        return fail("unpaired surrogate");
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape");
                }
                continue;
            }
            if (c < 0x20)
                return fail("control character in string");
            // Copy raw UTF-8 bytes as-is (validation of sequences is
            // deliberately lenient; we own every producer and consumer).
            out += char(c);
            ++pos;
        }
    }

    bool parseHex4(unsigned& out)
    {
        if (in.size() - pos < 4)
            return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = in[pos++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= unsigned(c - 'A' + 10);
            else
                return fail("invalid \\u escape");
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    bool parseNumber(double& out)
    {
        const size_t start = pos;
        if (peek() == '-')
            ++pos;
        if (peek() == '0') {
            ++pos;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9')
                ++pos;
        } else {
            return fail("invalid number");
        }
        if (peek() == '.') {
            ++pos;
            if (!(peek() >= '0' && peek() <= '9'))
                return fail("digits required after '.'");
            while (peek() >= '0' && peek() <= '9')
                ++pos;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos;
            if (peek() == '+' || peek() == '-')
                ++pos;
            if (!(peek() >= '0' && peek() <= '9'))
                return fail("digits required in exponent");
            while (peek() >= '0' && peek() <= '9')
                ++pos;
        }
        const std::string text(in.substr(start, pos - start));
        out = strtod(text.c_str(), nullptr);
        return true;
    }

    bool parseValue(Value& out)
    {
        skipWs();
        const char c = peek();
        switch (c) {
        case '\0':
            return fail("unexpected end of input");
        case 'n': out = nullptr; return literal("null");
        case 't': out = true; return literal("true");
        case 'f': out = false; return literal("false");
        case '"': {
            std::string s;
            if (!parseString(s))
                return false;
            out = Value(std::move(s));
            return true;
        }
        case '[': {
            ++pos;
            Value arr = Value::array();
            skipWs();
            if (peek() == ']') {
                ++pos;
                out = std::move(arr);
                return true;
            }
            for (;;) {
                Value item;
                if (!parseValue(item))
                    return false;
                arr.push(std::move(item));
                skipWs();
                if (peek() == ',') {
                    ++pos;
                    continue;
                }
                if (peek() == ']') {
                    ++pos;
                    out = std::move(arr);
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        case '{': {
            ++pos;
            Value obj = Value::object();
            skipWs();
            if (peek() == '}') {
                ++pos;
                out = std::move(obj);
                return true;
            }
            for (;;) {
                skipWs();
                std::string key;
                if (!parseString(key))
                    return false;
                skipWs();
                if (!expect(':', "':'"))
                    return false;
                Value item;
                if (!parseValue(item))
                    return false;
                obj.members().emplace_back(std::move(key), std::move(item));
                skipWs();
                if (peek() == ',') {
                    ++pos;
                    continue;
                }
                if (peek() == '}') {
                    ++pos;
                    out = std::move(obj);
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        default: {
            double num = 0;
            if (!parseNumber(num))
                return false;
            out = Value(num);
            return true;
        }
        }
    }
};

} // namespace

ParseResult parse(std::string_view utf8)
{
    ParseResult result;
    // Strip a UTF-8 BOM if present; editors like to add one to settings.
    if (utf8.size() >= 3 && (unsigned char)utf8[0] == 0xEF &&
        (unsigned char)utf8[1] == 0xBB && (unsigned char)utf8[2] == 0xBF)
        utf8.remove_prefix(3);

    Parser p{utf8};
    if (!p.parseValue(result.value)) {
        result.error = p.error;
        result.errorAt = p.pos;
        result.value = Value();
        return result;
    }
    p.skipWs();
    if (p.pos != utf8.size()) {
        result.error = "unexpected trailing data";
        result.errorAt = p.pos;
        result.value = Value();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace wim::json
