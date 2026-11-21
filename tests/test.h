// wimtests - a one-header test harness.
//
// Tests register themselves at static-init time via WIM_TEST and run in
// registration order. A failed CHECK throws, aborting the rest of the test
// but letting the suite continue; the process exit code is the failure count.
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace wimtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)())
    {
        registry().push_back({name, fn});
    }
};

struct Failure {
    std::string message;
};

inline int runAll()
{
    int failed = 0;
    for (auto& test : registry()) {
        try {
            test.fn();
            std::printf("[ok]   %s\n", test.name);
        } catch (const Failure& f) {
            ++failed;
            std::printf("[FAIL] %s\n      %s\n", test.name, f.message.c_str());
        } catch (...) {
            ++failed;
            std::printf("[FAIL] %s\n      unexpected exception\n", test.name);
        }
    }
    std::printf("%zu tests, %d failed\n", registry().size(), failed);
    return failed;
}

inline void failHere(const char* file, int line, const std::string& what)
{
    throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + what};
}

inline std::string repr(std::string_view s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if ((unsigned char)c < 0x20)
            out += "?";
        else
            out += c;
    }
    return out + "\"";
}

} // namespace wimtest

#define WIM_TEST(name)                                                                             \
    static void wim_test_fn_##name();                                                              \
    static const ::wimtest::Registrar wim_test_reg_##name(#name, &wim_test_fn_##name);            \
    static void wim_test_fn_##name()

#define WIM_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ::wimtest::failHere(__FILE__, __LINE__, "CHECK(" #cond ") failed");                    \
    } while (0)

#define WIM_CHECK_EQ(a, b)                                                                         \
    do {                                                                                           \
        const auto& va_ = (a);                                                                     \
        const auto& vb_ = (b);                                                                     \
        if (!(va_ == vb_))                                                                         \
            ::wimtest::failHere(__FILE__, __LINE__,                                                 \
                                std::string("CHECK_EQ(" #a ", " #b ") failed: ") +                  \
                                    ::wimtest::repr(std::to_string(va_)) + " != " +                 \
                                    ::wimtest::repr(std::to_string(vb_)));                          \
    } while (0)

#define WIM_CHECK_STREQ(a, b)                                                                      \
    do {                                                                                           \
        const std::string va_ = std::string(a); /* copy: (a) may be a temporary */                 \
        const std::string vb_ = std::string(b);                                                    \
        if (!(va_ == vb_))                                                                         \
            ::wimtest::failHere(__FILE__, __LINE__,                                                 \
                                std::string("CHECK_STREQ(" #a ", " #b ") failed: ") +               \
                                    ::wimtest::repr(va_) + " != " + ::wimtest::repr(vb_));          \
    } while (0)
