#include <cstdio>
#include <cstdlib>
#include "test.h"

std::vector<TestCase> &test_registry() {
    static std::vector<TestCase> r;
    return r;
}
int g_test_failures = 0;

int main() {
    // The firmware logs with printf; keep the test output readable.
    if (!std::getenv("HOST_TEST_VERBOSE")) std::freopen("/dev/null", "w", stdout);
    int failed_tests = 0;
    for (const auto &t : test_registry()) {
        const int before = g_test_failures;
        t.fn();
        const bool ok = g_test_failures == before;
        if (!ok) failed_tests++;
        std::fprintf(stderr, "%s %s\n", ok ? "ok  " : "FAIL", t.name);
    }
    std::fprintf(stderr, "%zu tests, %d failed\n", test_registry().size(), failed_tests);
    return failed_tests ? 1 : 0;
}
