#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// Minimal test framework -- no external dependencies.

#define ASSERT_TRUE(x)                                                         \
    do {                                                                        \
        if (!(x)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: ASSERT_TRUE(%s)\n", __FILE__,         \
                    __LINE__, #x);                                             \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(x)                                                        \
    do {                                                                        \
        if ((x)) {                                                             \
            fprintf(stderr, "FAIL %s:%d: ASSERT_FALSE(%s)\n", __FILE__,        \
                    __LINE__, #x);                                             \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b)                                                        \
    do {                                                                        \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (_a != _b) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a,  \
                    #b);                                                        \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_NE(a, b)                                                        \
    do {                                                                        \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (_a == _b) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s == %s (expected not equal)\n",     \
                    __FILE__, __LINE__, #a, #b);                               \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

// Compare std::string with string literal without triggering sign conversion
// warnings.
#define ASSERT_STREQ(a, b)                                                     \
    do {                                                                        \
        std::string _sa = (a);                                                 \
        std::string _sb = (b);                                                 \
        if (_sa != _sb) {                                                      \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\" (%s vs %s)\n",       \
                    __FILE__, __LINE__, _sa.c_str(), _sb.c_str(), #a, #b);     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

// Simple test registration.
struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_registry().push_back({name, std::move(fn)});
    }
};

#define TEST(name)                                                             \
    static void test_##name();                                                 \
    static TestRegistrar _reg_##name(#name, test_##name);                      \
    static void test_##name()

#define RUN_ALL_TESTS()                                                        \
    do {                                                                        \
        int passed = 0;                                                        \
        int total = static_cast<int>(test_registry().size());                  \
        for (auto& tc : test_registry()) {                                     \
            printf("  %-60s ", tc.name);                                        \
            fflush(stdout);                                                    \
            tc.fn();                                                           \
            printf("OK\n");                                                    \
            ++passed;                                                          \
        }                                                                      \
        printf("\n%d/%d tests passed.\n", passed, total);                      \
        return (passed == total) ? 0 : 1;                                      \
    } while (0)
