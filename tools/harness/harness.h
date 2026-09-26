// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
// ============================================================================
//  harness.h -- micro test framework for the OSkate development harness.
//
//  Design goals (see REVIEW.md, sections 6.7 & 10):
//    * zero dependencies: header-only, single translation unit, no GPU/SDL
//    * fast: tests run in-process against the real game code
//    * CI friendly: exit code, JUnit XML, per-test timing, `--filter`
//    * honest: three outcomes -- CHECK (must pass), XCHECK (expected to fail,
//      flips to a failure the moment the feature lands), PENDING (documented
//      gap, counted but never red).
// ============================================================================
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

namespace hns {

// ---------------------------------------------------------------- options ---
struct Options {
    std::string filter;          // substring matched against "suite.name"
    std::string junit;           // JUnit XML output path
    std::string record;          // monkey-run, then write an input replay here
    std::string replay;          // replay a recorded file and verify the hash
    std::string gotoSpan;        // start the skater at this named gap
    int repeat = 1;
    int monkeySeconds = 0;       // run the random-input monkey for N sim seconds
    uint32_t seed = 12345u;
    bool listOnly = false;
    bool verbose = false;
    bool dump = false;           // print level content and exit
};
inline Options& opt() { static Options o; return o; }

// ------------------------------------------------------------------ timing --
inline double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
struct Timer {
    double t0 = nowMs();
    double ms() const { return nowMs() - t0; }
    void reset() { t0 = nowMs(); }
};

// --------------------------------------------------------------- registry ---
struct Case {
    std::string suite, name;
    void (*fn)() = nullptr;
    bool slow = false;
};
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline std::string fullName(const Case& c) { return c.suite + "." + c.name; }

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)(), bool slow) {
        cases().push_back({suite, name, fn, slow});
    }
};

// ---------------------------------------------------------------- results ---
struct Result {
    std::string suite, name;
    double ms = 0;
    int checks = 0;
    int pendingCount = 0;
    std::vector<std::string> failures;      // CHECK failures
    std::vector<std::string> pendings;      // PENDING reasons
    std::vector<std::string> unexpected;    // XCHECK that started passing
    bool passed() const { return failures.empty() && unexpected.empty(); }
    bool skipped() const { return pendingCount > 0 && failures.empty() && unexpected.empty(); }
};
inline Result* current = nullptr;           // test being executed

inline void failAt(const char* file, int line, const std::string& msg) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s:%d: %s", file, line, msg.c_str());
    if (current) current->failures.push_back(buf);
    else printf("  FAIL (outside a test) %s\n", buf);
}
inline void expect(bool ok, const char* file, int line, const std::string& what) {
    if (current) current->checks++;
    if (!ok) failAt(file, line, what);
}
inline int worstFailures() { return current ? (int)current->failures.size() : 0; }
inline void pending(const std::string& why) {
    if (current) { current->pendingCount++; current->pendings.push_back(why); }
}
// An expectation that is known to be unmet today (e.g. a roadmap feature).
// It documents the gap; the moment it passes the harness turns red so the
// expectation gets promoted to a normal CHECK.
inline void xcheck(bool wasExpectedToFail, const char* file, int line, const std::string& what) {
    if (current) current->checks++;
    if (wasExpectedToFail) {
        char buf[512];
        snprintf(buf, sizeof buf, "%s:%d: UNEXPECTED PASS (feature landed?) -- %s", file, line, what.c_str());
        if (current) current->unexpected.push_back(buf);
    }
}

// ------------------------------------------------------------------ macros --
#define TEST(suite, name)                                                                  \
    static void htest_##suite##_##name();                                                  \
    static ::hns::Registrar hreg_##suite##_##name(#suite, #name, htest_##suite##_##name, false); \
    static void htest_##suite##_##name()
#define TEST_SLOW(suite, name)                                                             \
    static void htest_##suite##_##name();                                                  \
    static ::hns::Registrar hreg_##suite##_##name(#suite, #name, htest_##suite##_##name, true);  \
    static void htest_##suite##_##name()

#define CHECK(cond)          ::hns::expect((cond), __FILE__, __LINE__, #cond)
#define CHECKM(cond, msg)    ::hns::expect((cond), __FILE__, __LINE__, std::string(msg) + " | " #cond)
// NEAR is an expression so it can be combined: CHECK(NEAR(x, 1, 1e-4) && NEAR(y, 2, 1e-4))
#define NEAR(a, b, tol)      (std::fabs((double)(a) - (double)(b)) <= (double)(tol))
#define XCHECK(cond, why)    ::hns::xcheck((cond), __FILE__, __LINE__, why)
#define PENDING(why)         ::hns::pending(why)

// ------------------------------------------------------------------- utils --
inline uint32_t fnv1a(uint32_t h, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
inline uint32_t hashF(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline std::string xmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}
inline std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap; va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace hns
