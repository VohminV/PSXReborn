#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using TestFunction = void (*)();

struct TestCase {
    const char* name;
    TestFunction function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, TestFunction function) {
        registry().push_back({name, function});
    }
};

inline std::string cliPath;

template <typename Actual, typename Expected>
void checkEqual(const Actual& actual, const Expected& expected, const char* file, int line, const char* expression) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << file << ":" << line << ": " << expression
                << " (expected " << expected << ", got " << actual << ")";
        throw Failure(message.str());
    }
}

}  // namespace test

#define TEST_CASE(name)                                      \
    static void name();                                      \
    static ::test::Registrar name##_registrar(#name, name);  \
    static void name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::ostringstream message;                                               \
            message << __FILE__ << ":" << __LINE__ << ": check failed: " #condition;  \
            throw ::test::Failure(message.str());                                     \
        }                                                                             \
    } while (false)

#define CHECK_EQ(actual, expected) \
    ::test::checkEqual((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)
