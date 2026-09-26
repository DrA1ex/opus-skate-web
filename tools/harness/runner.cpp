// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
// ============================================================================
//  OpusSkate web development harness
//
//  Runs the real game code (level, physics, tricks, NPCs, traffic, particles,
//  audio) with no GPU, no window and no audio device, so it can gate every
//  change in CI. See tools/harness/README.md.
//
//  Build and run (from the repository root):
//    tools/harness/check.sh                # or: check.sh --slow --filter tricks
//  Equivalent manual build:
//    g++ -O2 -std=c++17 -Itools/harness/stub -Itools/harness -I. -o /tmp/opus-skate-harness tools/harness/runner.cpp
// ============================================================================
#define main oskate_main
#include "skate.cpp"
#undef main

#include "harness.h"
#include "fixture.h"

#include <atomic>
#include <thread>

#include "suites/suite_math.cpp"
#include "suites/suite_level.cpp"
#include "suites/suite_physics.cpp"
#include "suites/suite_tricks.cpp"
#include "suites/suite_world.cpp"
#include "suites/suite_audio.cpp"
#include "suites/suite_runtime.cpp"
#include "suites/suite_perf.cpp"
#include "suites/suite_replay.cpp"

using namespace hns;

// ---------------------------------------------------------------------------
static void usage() {
    printf(
        "opus-skate-harness -- headless development harness for skate.cpp\n"
        "\n"
        "usage: opus-skate-harness [options]\n"
        "\n"
        "  --list                list every test and exit\n"
        "  --filter <substr>     only run tests whose suite.name contains <substr>\n"
        "  --repeat <n>          run the selection n times (regression / flake hunt)\n"
        "  --slow                include the slow suites (letter reachability)\n"
        "  --no-bench            skip the benchmarks\n"
        "  --bench-only          run benchmarks only\n"
        "  --junit <file>        write a JUnit XML report\n"
        "  --verbose             print every check, including the passing ones\n"
        "\n"
        "  --monkey <seconds>    fuzz the game with deterministic random input\n"
        "  --seed <n>            seed for --monkey (default 12345)\n"
        "  --record <file>       run the monkey and save an input replay\n"
        "  --replay <file>       replay a recording and verify its state hash\n"
        "  --goto <gap name>     start the skater at a named gap (menu for devs)\n"
        "  --dump                print level content and exit\n"
        "  -h, --help            this message\n");
}

static void dumpLevel() {
    initGame();
    printf("-- level content -----------------------------------------------------\n");
    printf("  static mesh      : %zu verts, %zu tris, %.2f MB\n", SM.v.size(), SM.idx.size() / 3,
           SM.v.size() * sizeof(Vtx) / 1048576.0);
    printf("  water mesh       : %zu verts, %zu tris\n", WM.v.size(), WM.idx.size() / 3);
    printf("  solids           : %zu (%d boxes, %d ramps, %d quarter pipes)\n", world.solids.size(),
           (int)std::count_if(world.solids.begin(), world.solids.end(), [](const Solid& s) { return s.type == S_BOX; }),
           (int)std::count_if(world.solids.begin(), world.solids.end(), [](const Solid& s) { return s.type == S_RAMP; }),
           (int)std::count_if(world.solids.begin(), world.solids.end(), [](const Solid& s) { return s.type == S_QP; }));
    printf("  rails            : %zu (metal %d, ledge %d, curb %d, wood %d, car %d)\n", world.rails.size(),
           (int)std::count_if(world.rails.begin(), world.rails.end(), [](const Rail& r) { return r.kind == RK_METAL; }),
           (int)std::count_if(world.rails.begin(), world.rails.end(), [](const Rail& r) { return r.kind == RK_LEDGE; }),
           (int)std::count_if(world.rails.begin(), world.rails.end(), [](const Rail& r) { return r.kind == RK_CURB; }),
           (int)std::count_if(world.rails.begin(), world.rails.end(), [](const Rail& r) { return r.kind == RK_WOOD; }),
           (int)std::count_if(world.rails.begin(), world.rails.end(), [](const Rail& r) { return r.kind == RK_CAR; }));
    printf("  gaps (%zu):\n", world.gaps.size());
    for (const Gap& g : world.gaps) printf("      %-22s %5d pts   at (%.0f, %.0f) top %.2f\n", g.name.c_str(), g.points, g.cx, g.cz, g.topY);
    printf("  letters (S K A T E):");
    for (V3 p : letterPos) printf(" (%.0f %.1f %.0f)", p.x, p.y, p.z);
    printf("\n  emitters (%zu):", emitters.size());
    for (auto& e : emitters) printf(" kind%d@(%.0f,%.0f)", e.kind, e.pos.x, e.pos.z);
    printf("\n  pedestrians      : %zu on %zu paths\n", npcs.size(), npcPaths.size());
    printf("  pigeons / cars   : %zu / %zu\n", pigeons.size(), cars.size());
    printf("  collision grid   : %zu cells of %.0f m\n", world.grid.size(), (double)World::CELL);
}

static void runMonkeyMode() {
    initGame();
    resetWorld();
    if (!opt().gotoSpan.empty()) {
        if (!gotoGap(opt().gotoSpan)) { printf("no such gap: %s\n", opt().gotoSpan.c_str()); return; }
        printf("-- starting at gap '%s': (%.1f, %.2f, %.1f)\n", opt().gotoSpan.c_str(), P.pos.x, P.pos.y, P.pos.z);
    }
    if (!opt().replay.empty()) {
        Replay rep;
        if (!rep.load(opt().replay)) { printf("cannot read replay %s\n", opt().replay.c_str()); return; }
        resetWorld();
        uint32_t h = playReplay(rep);
        printf("-- replay %s: %u ticks, expected hash %08x, got %08x -> %s\n", opt().replay.c_str(),
               rep.ticks, rep.hash, h, h == rep.hash ? "MATCH" : "DIVERGED");
        printf("   score %lld, state %d, pos (%.1f %.2f %.1f)\n", P.score, P.state, P.pos.x, P.pos.y, P.pos.z);
        return;
    }
    Replay rec;
    double ms = 0;
    MonkeyStats st = runMonkey(opt().monkeySeconds, opt().seed, opt().record.empty() ? nullptr : &rec, true, &ms);
    printf("-- monkey %d s (seed %u)\n", opt().monkeySeconds, opt().seed);
    printf("   score %lld (best %lld), bails %lld, max combo %d, letters %d\n",
           st.score, P.best, st.bails, st.maxCombo, P.lettersGot);
    printf("   max speed %.1f m/s, stuck frames %ld, finite %d\n", st.maxSpeed, st.stuckFrames, (int)st.finite);
    printf("   simulated %.0f frames in %.0f ms (%.0fx real time), state hash %08x\n",
           st.frames, ms, (st.frames / 120.0) / (ms / 1000.0), worldHash());
    if (!opt().record.empty()) {
        printf("   wrote %s (%u ticks, hash %08x)\n", opt().record.c_str(), rec.ticks, rec.hash);
        rec.save(opt().record);
    }
}

static void writeJUnit(const std::string& path, const std::vector<Result>& results) {
    int fails = 0, pend = 0;
    double total = 0;
    for (auto& r : results) { if (!r.passed()) fails++; if (r.skipped()) pend++; total += r.ms; }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("cannot write %s\n", path.c_str()); return; }
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<testsuites name=\"oskate\" tests=\"%zu\" failures=\"%d\" skipped=\"%d\" time=\"%.3f\">\n",
            results.size(), fails, pend, total / 1000.0);
    std::string suiteName;
    for (size_t i = 0; i < results.size(); i++) {
        const Result& r = results[i];
        if (i == 0 || r.suite != suiteName) {
            if (i) fprintf(f, "</testsuite>\n");
            suiteName = r.suite;
            fprintf(f, "<testsuite name=\"%s\">\n", xmlEscape(suiteName).c_str());
        }
        fprintf(f, "  <testcase name=\"%s\" classname=\"%s\" time=\"%.3f\"",
                xmlEscape(r.name).c_str(), xmlEscape(r.suite).c_str(), r.ms / 1000.0);
        if (r.passed() && !r.skipped()) fprintf(f, "/>\n");
        else {
            fprintf(f, ">\n");
            for (auto& m : r.failures) fprintf(f, "    <failure message=\"%s\"/>\n", xmlEscape(m).c_str());
            for (auto& m : r.unexpected) fprintf(f, "    <failure message=\"%s\"/>\n", xmlEscape(m).c_str());
            if (r.skipped()) {
                std::string why = r.pendings.empty() ? "documented gap" : r.pendings[0];
                fprintf(f, "    <skipped message=\"%s\"/>\n", xmlEscape(why).c_str());
            }
            fprintf(f, "  </testcase>\n");
        }
    }
    fprintf(f, "</testsuite>\n</testsuites>\n");
    fclose(f);
    printf("wrote JUnit report: %s\n", path.c_str());
}

int main(int argc, char** argv) {
    Options& o = opt();
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--list") o.listOnly = true;
        else if (a == "--filter") o.filter = next();
        else if (a == "--repeat") o.repeat = std::max(1, atoi(next().c_str()));
        else if (a == "--slow") o.includeSlow = true;
        else if (a == "--no-bench") o.noBench = true;
        else if (a == "--bench-only") o.benchOnly = true;
        else if (a == "--junit") o.junit = next();
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--monkey") o.monkeySeconds = atoi(next().c_str());
        else if (a == "--seed") o.seed = (uint32_t)strtoul(next().c_str(), nullptr, 10);
        else if (a == "--record") o.record = next();
        else if (a == "--replay") o.replay = next();
        else if (a == "--goto") o.gotoSpan = next();
        else if (a == "--dump") o.dump = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { printf("unknown option: %s\n\n", a.c_str()); usage(); return 2; }
    }

    if (o.dump) { dumpLevel(); return 0; }
    if (o.monkeySeconds > 0 || !o.record.empty() || !o.replay.empty()) { runMonkeyMode(); return 0; }

    std::vector<Case*> selected;
    for (Case& c : cases()) {
        if (!o.includeSlow && c.slow) continue;
        if (o.benchOnly && c.suite != "perf") continue;   // the perf suite prints the table
        if (!o.filter.empty() && fullName(c).find(o.filter) == std::string::npos) continue;
        selected.push_back(&c);
    }
    if (o.listOnly) {
        std::string suite;
        for (Case* c : selected) {
            if (c->suite != suite) { suite = c->suite; printf("%s:\n", suite.c_str()); }
            printf("  %-44s%s\n", c->name.c_str(), c->slow ? "   [slow]" : "");
        }
        printf("\n%zu tests (%zu hidden as slow; use --slow to include)\n", selected.size(),
               (size_t)std::count_if(cases().begin(), cases().end(), [](const Case& c) { return c.slow; }));
        return 0;
    }

    printf("OpusSkate web harness -- %zu tests selected", selected.size() * (size_t)o.repeat);
    if (!o.filter.empty()) printf(" (filter '%s')", o.filter.c_str());
    if (!o.includeSlow) printf("  [slow suite hidden]");
    printf("\n\n");

    std::vector<Result> results;
    double tAll = nowMs();
    std::string suite;
    int passed = 0, failed = 0, pendingTests = 0, unexpectedPass = 0;

    for (int rep = 0; rep < o.repeat; rep++) {
        for (Case* c : selected) {
            Result r;
            r.suite = c->suite;
            r.name = c->name;
            if (o.repeat > 1) r.name += fmt(" [run %d]", rep + 1);
            if (r.suite != suite) { suite = r.suite; printf("%s\n", suite.c_str()); }
            current = &r;
            Timer tm;
            c->fn();
            r.ms = tm.ms();
            current = nullptr;
            results.push_back(r);
            if (!r.passed()) {
                printf("  \x1b[31mFAIL\x1b[0m %6.0fms  %s\n", r.ms, r.name.c_str());
                auto report = [](const std::vector<std::string>& msgs, const char* colour) {
                    std::vector<std::pair<std::string, int>> uniq;
                    for (auto& m : msgs) {
                        bool found = false;
                        for (auto& u : uniq) if (u.first == m) { u.second++; found = true; break; }
                        if (!found) uniq.push_back({m, 1});
                    }
                    int shown = 0;
                    for (auto& u : uniq) {
                        if (shown++ >= 6) { printf("        ... and %zu more distinct failures\n", uniq.size() - 6); break; }
                        printf("        %s%s\n", colour, u.first.c_str());
                        if (u.second > 1) printf("           (repeated %d times)\n", u.second);
                    }
                };
                report(r.failures, "");
                report(r.unexpected, "UNEXPECTED PASS: ");
                failed++;
            } else if (r.skipped()) {
                printf("  \x1b[33mpend\x1b[0m %6.0fms  %s\n", r.ms, r.name.c_str());
                for (auto& m : r.pendings) printf("        %s\n", m.c_str());
                pendingTests++;
            } else {
                printf("  \x1b[32mok\x1b[0m   %6.0fms  %s\n", r.ms, r.name.c_str());
                passed++;
            }
        }
    }
    double totalMs = nowMs() - tAll;

    printf("\n-- summary -----------------------------------------------------------\n");
    printf("  %d passed, %d failed, %d pending, %d unexpected pass  (%.2f s)\n",
           passed, failed, pendingTests, unexpectedPass, totalMs / 1000.0);
    int checks = 0;
    for (auto& r : results) checks += r.checks;
    printf("  %d assertions (%d per test on average)\n", checks, (int)(checks / std::max<size_t>(1, results.size())));

    if (!o.noBench) {
        bool haveBench = !benchRows().empty();
        if (haveBench) printBench();
        else printf("\n  (benchmarks live in the perf suite: --filter perf)\n");
    }

    // slowest three, to spot creeping cost
    std::vector<Result> sorted = results;
    std::sort(sorted.begin(), sorted.end(), [](const Result& a, const Result& b) { return a.ms > b.ms; });
    printf("\n  slowest: ");
    for (size_t i = 0; i < sorted.size() && i < 3; i++)
        printf("%s (%.0f ms)%s", sorted[i].name.c_str(), sorted[i].ms, i + 2 < sorted.size() ? ", " : "\n");

    if (!o.junit.empty()) writeJUnit(o.junit, results);
    printf("\n%s\n", failed ? "HARNESS FAILED" : "HARNESS PASSED");
    return failed ? 1 : 0;
}
