// JSON serialization (compact and pretty) and string escaping.
#include "json.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace wim::json {

namespace {

void writeDouble(std::string& out, double d)
{
    // JSON numbers round-trip best through %.17g, but that prints
    // "1e+02" style exponents for round values; trim to the shortest of
    // the lossless candidates.
    for (int prec : {15, 16, 17}) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (strtod(buf, nullptr) == d) {
            out += buf;
            return;
        }
    }
    // Cannot happen, but never fall off the end.
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", d);
    out += buf;
}

void writeIndent(std::string& out, int depth)
{
    out += '\n';
    out.append(size_t(depth) * 2, ' ');
}

void serializeTo(std::string& out, const Value& v, bool pretty, int depth)
{
    switch (v.type()) {
    case Value::Type::Null:
        out += "null";
        break;
    case Value::Type::Bool:
        out += v.asBool() ? "true" : "false";
        break;
    case Value::Type::Number: {
        if (std::isfinite(v.asNumber()))
            writeDouble(out, v.asNumber());
        else
            out += "null"; // NaN/inf are not representable; degrade, never crash
        break;
    }
    case Value::Type::String:
        out += '"';
        out += escapeString(v.asString());
        out += '"';
        break;
    case Value::Type::Array: {
        const auto& items = v.items();
        if (items.empty()) {
            out += "[]";
            break;
        }
        out += '[';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i)
                out += ',';
            if (pretty)
                writeIndent(out, depth + 1);
            serializeTo(out, items[i], pretty, depth + 1);
        }
        if (pretty)
            writeIndent(out, depth);
        out += ']';
        break;
    }
    case Value::Type::Object: {
        const auto& members = v.members();
        if (members.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t i = 0; i < members.size(); ++i) {
            if (i)
                out += ',';
            if (pretty)
                writeIndent(out, depth + 1);
            out += '"';
            out += escapeString(members[i].first);
            out += pretty ? "\": " : "\":";
            serializeTo(out, members[i].second, pretty, depth + 1);
        }
        if (pretty)
            writeIndent(out, depth);
        out += '}';
        break;
    }
    }
}

} // namespace

std::string escapeString(std::string_view utf8)
{
    std::string out;
    out.reserve(utf8.size() + 2);
    for (const char ch : utf8) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += ch; // UTF-8 bytes pass through unescaped
            }
        }
    }
    return out;
}

std::string serialize(const Value& v, bool pretty)
{
    std::string out;
    out.reserve(64);
    serializeTo(out, v, pretty, 0);
    if (pretty)
        out += '\n';
    return out;
}

} // namespace wim::json
