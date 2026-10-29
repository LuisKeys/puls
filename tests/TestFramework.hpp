// Minimal test registry (no external dependency). Each TEST is run by main(); CHECK failures are counted and printed.
#pragma once
#include <cstdio>
#include <vector>

namespace testfw {
struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, void (*f)()) { cases().push_back({n, f}); } };
inline int runAll()
{
    int bad = 0;
    for (const Case& c : cases()) {
        const int before = failures();
        c.fn();
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", c.name);
        bad += ok ? 0 : 1;
    }
    std::printf("%zu tests, %d failed\n", cases().size(), bad);
    return bad == 0 ? 0 : 1;
}
} // namespace testfw

#define TEST(name) static void name(); static testfw::Reg reg_##name(#name, name); static void name()
#define CHECK(c) do { if (!(c)) { std::printf("    %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++testfw::failures(); } } while (0)
#define CHECK_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (!(va_ == vb_)) { \
    std::printf("    %s:%d: CHECK_EQ(%s, %s) -> %lld vs %lld\n", __FILE__, __LINE__, #a, #b, (long long)va_, (long long)vb_); \
    ++testfw::failures(); } } while (0)
