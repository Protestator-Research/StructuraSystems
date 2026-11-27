//
// Minimal test helpers so the tests run without any additional dependency.
//

#ifndef STRUCTURASYSTEMS_TESTFRAMEWORK_H
#define STRUCTURASYSTEMS_TESTFRAMEWORK_H

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace StructuraSystems::Test {

    struct TestCase {
        std::string Name;
        std::function<void()> Body;
    };

    inline std::vector<TestCase> &registry() {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct Registrar {
        Registrar(std::string name, std::function<void()> body) {
            registry().push_back({std::move(name), std::move(body)});
        }
    };

    class AssertionFailure : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    inline std::string location(const char *file, int line) {
        return std::string(file) + ":" + std::to_string(line);
    }
}

#define STRUCTURA_CONCAT_INNER(a, b) a##b
#define STRUCTURA_CONCAT(a, b) STRUCTURA_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                              \
    static void STRUCTURA_CONCAT(test_function_, __LINE__)();                                       \
    static const ::StructuraSystems::Test::Registrar STRUCTURA_CONCAT(test_registrar_, __LINE__)(  \
            name, &STRUCTURA_CONCAT(test_function_, __LINE__));                                     \
    static void STRUCTURA_CONCAT(test_function_, __LINE__)()

#define CHECK(condition)                                                                             \
    do {                                                                                             \
        if (!(condition))                                                                            \
            throw ::StructuraSystems::Test::AssertionFailure(                                        \
                    ::StructuraSystems::Test::location(__FILE__, __LINE__) + ": CHECK(" #condition ") failed"); \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                   \
    do {                                                                                             \
        const auto actualValue_ = (actual);                                                          \
        const auto expectedValue_ = (expected);                                                      \
        if (!(actualValue_ == expectedValue_)) {                                                     \
            std::ostringstream message_;                                                             \
            message_ << ::StructuraSystems::Test::location(__FILE__, __LINE__)                       \
                     << ": CHECK_EQ(" #actual ", " #expected ") failed\n--- actual ---\n"            \
                     << actualValue_ << "\n--- expected ---\n" << expectedValue_;                    \
            throw ::StructuraSystems::Test::AssertionFailure(message_.str());                        \
        }                                                                                            \
    } while (false)

#define CHECK_THROWS_AS(expression, ExceptionType)                                                   \
    do {                                                                                             \
        bool thrown_ = false;                                                                        \
        try {                                                                                        \
            static_cast<void>(expression);                                                           \
        } catch (const ExceptionType &) {                                                            \
            thrown_ = true;                                                                          \
        }                                                                                            \
        if (!thrown_)                                                                                \
            throw ::StructuraSystems::Test::AssertionFailure(                                        \
                    ::StructuraSystems::Test::location(__FILE__, __LINE__) +                         \
                    ": expected " #ExceptionType " from " #expression);                              \
    } while (false)

#endif //STRUCTURASYSTEMS_TESTFRAMEWORK_H
