// A tiny test harness: TEST(name) registers a function, CHECK/CHECK_EQ report
// to stderr (the firmware's printf logging goes to stdout, silenced by main).
#pragma once
#include <cstdio>
#include <functional>
#include <vector>

struct TestCase { const char *name; void (*fn)(); };
std::vector<TestCase> &test_registry();
extern int g_test_failures;

struct TestRegistrar {
    TestRegistrar(const char *name, void (*fn)()) { test_registry().push_back({name, fn}); }
};
#define TEST(name)                                              \
    static void test_##name();                                  \
    static TestRegistrar registrar_##name(#name, test_##name);  \
    static void test_##name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_test_failures++;                                                           \
        }                                                                                \
    } while (0)
#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        const auto va_ = (a);                                                            \
        const auto vb_ = (b);                                                            \
        if (!(va_ == vb_)) {                                                             \
            std::fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__,    \
                         __LINE__, #a, #b, (long long) va_, (long long) vb_);            \
            g_test_failures++;                                                           \
        }                                                                                \
    } while (0)
