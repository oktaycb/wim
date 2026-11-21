// wim - a Windows-native terminal workspace for coding agents.
// Core/json: a small, strict JSON module for settings, session state, the
// pipe protocol and CLI output. Objects preserve insertion order so a
// settings file written back keeps the user's own keys in their order.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace wim::json {

class Value;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;

class Value {
public:
    enum class Type : int { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(unsigned n) : type_(Type::Number), num_(n) {}
    Value(long n) : type_(Type::Number), num_(double(n)) {}
    Value(long long n) : type_(Type::Number), num_(double(n)) {}
    Value(unsigned long long n) : type_(Type::Number), num_(double(n)) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), str_(s) {}
    Value(std::wstring_view s); // UTF-16 -> UTF-8 (defined in json.cpp)
    Value(const std::wstring& s);
    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return isBool() ? bool_ : fallback; }
    double asNumber(double fallback = 0) const { return isNumber() ? num_ : fallback; }
    long long asInt(long long fallback = 0) const;
    const std::string& asString() const { return str_; }

    // ---- array access ----
    Array& items() { return arr_; }
    const Array& items() const { return arr_; }
    void push(Value v) { arr_.push_back(std::move(v)); }
    size_t size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }

    // ---- object access ----
    Object& members() { return obj_; }
    const Object& members() const { return obj_; }
    Value* find(std::string_view key);
    const Value* find(std::string_view key) const;
    // Read a member with a default: v.get("fontSize", 12.0).
    template <typename T>
    T get(std::string_view key, T fallback) const;
    void set(std::string key, Value v); // replaces or appends (order kept)
    bool remove(std::string_view key);  // erased keys are gone from the file
    bool contains(std::string_view key) const { return find(key) != nullptr; }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;
};

template <typename T>
T Value::get(std::string_view key, T fallback) const
{
    const Value* v = find(key);
    if (!v)
        return fallback;
    if constexpr (std::is_same_v<T, std::string>) {
        return v->isString() ? v->asString() : fallback;
    } else if constexpr (std::is_same_v<T, bool>) {
        return v->asBool(fallback);
    } else if constexpr (std::is_integral_v<T>) {
        return T(v->asInt(static_cast<long long>(fallback)));
    } else {
        return T(v->asNumber(double(fallback)));
    }
}

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;   // human-readable, empty when ok
    size_t errorAt = 0;  // byte offset into the input

    explicit operator bool() const { return ok; }
};

// Strict RFC 8259 parse of a complete document (one value, optional
// trailing whitespace). Tolerates a UTF-8 BOM.
ParseResult parse(std::string_view utf8);

// Serializes. pretty=false emits compact one-line; pretty=true indents
// with two spaces and puts a newline after the document.
std::string serialize(const Value& v, bool pretty = false);

// Escapes a string for use inside a JSON document (adds no quotes).
std::string escapeString(std::string_view utf8);

} // namespace wim::json
