#include <exception>
#include <iostream>
#include <string>

#include "test_framework.hpp"

int main(int argc, char** argv) {
    if (argc > 1) {
        test::cliPath = argv[1];
    }

    int failures = 0;
    for (const test::TestCase& testCase : test::registry()) {
        try {
            testCase.function();
            std::cout << "[PASS] " << testCase.name << "\n";
        } catch (const std::exception& error) {
            std::cout << "[FAIL] " << testCase.name << ": " << error.what() << "\n";
            ++failures;
        } catch (...) {
            std::cout << "[FAIL] " << testCase.name << ": unknown exception\n";
            ++failures;
        }
    }

    std::cout << (failures == 0 ? "all tests passed" : "tests failed")
              << " (" << test::registry().size() << " tests, " << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
