// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

#ifdef HARNESS_SANITIZED
static constexpr bool enforcePerfBudgets = false;
#else
static constexpr bool enforcePerfBudgets = true;
#endif

// suite_perf -- the performance budget from REVIEW.md section 6.6 turned into
// tests. Thresholds are deliberately generous so the suite is stable on slow CI
// machines, but a structural regression (e.g. losing the collision grid) will
// blow past them by an order of magnitude.

// ---------------------------------------------------------------- benchmarks --
struct BenchRow { std::string name; double us; std::string note; };
static std::vector<BenchRow>& benchRows() { static std::vector<BenchRow> r; return r; }
static void bench(const std::string& name, double us, const std::string& note = "") { benchRows().push_back({name, us, note}); }
static void printBench() {
    printf("\n-- benchmarks --------------------------------------------------------\n");
    for (auto& b : benchRows())
        printf("  %-34s %10.3f us   %s\n", b.name.c_str(), b.us, b.note.c_str());
}

TEST(perf, level_build_time) {
    initGame();
    size_t tris = SM.idx.size() / 3, solids = world.solids.size(), rails = world.rails.size();
    size_t npcCount = npcs.size(), paths = npcPaths.size();
    double t0 = nowMs();
    rebuildLevel();
    double ms = nowMs() - t0;
    bench("buildLevel()", ms * 1000.0, "full city + collision + rails + gaps");
    printf("    [build] full level rebuild in %.1f ms\n", ms);
    CHECK(!enforcePerfBudgets || ms < 1500.0);
    // the level must be deterministic: identical content on every rebuild
    CHECK(SM.idx.size() / 3 == tris);
    CHECK(world.solids.size() == solids);
    CHECK(world.rails.size() == rails);
    CHECK(npcPaths.size() == paths);
    CHECK(npcs.size() == npcCount);
    initGame();                      // leave the fixture as we found it
}

TEST(perf, startup_budget) {
    // initGame() has already run in earlier tests; measure the audio/level split
    double t0 = nowMs();
    genSfx();
    double audioMs = nowMs() - t0;
    bench("genSfx()", audioMs * 1000.0, "17 one-shots, synthesised + normalised");
    printf("    [startup] genSfx %.1f ms\n", audioMs);
    CHECK(!enforcePerfBudgets || audioMs < 1200.0);
    normaliseSfx();
}

TEST(perf, ground_query_cost) {
    initGame();
    const int N = 200000;
    double t0 = nowMs();
    volatile float acc = 0;
    for (int i = 0; i < N; i++)
        acc += world.ground((i % 300) - 150 + 0.5f, (i % 200) - 100 + 0.5f, 3.f).h;
    double us = (nowMs() - t0) * 1000.0 / N;
    bench("world.ground()", us, "uniform-grid lookup");
    printf("    [ground] %.4f us/call over %d samples\n", us, N);
    CHECK(!enforcePerfBudgets || us < 2.0);
    CHECK(world.grid.size() > 0);
}

TEST(perf, wall_collision_cost) {
    initGame();
    const int N = 20000;
    // sample a few places, including the densest part of the collision grid
    const float spots[][2] = {{3, 22}, {28, -34.5f}, {-33, 30}, {47, 20}, {38, 25}};
    double worst = 0;
    for (auto& s : spots) {
        V3 home(s[0], 0.3f, s[1]);
        home.y = world.ground(home.x, home.z, 2.f).h + 0.2f;
        V3 p = home, v(1, 0, 1);
        double t0 = nowMs();
        for (int i = 0; i < N / 5; i++) {
            world.collideWalls(p, v, 0.3f, p.y + STEP_UP, 1.6f);
            p = home;
            v = V3(1, 0, 1);
        }
        worst = std::max(worst, (nowMs() - t0) * 1000.0 / (N / 5));
    }
    bench("world.collideWalls()", worst, "capsule vs grid, worst of 5 dense spots");
    printf("    [walls] worst case %.2f us/call\n", worst);
    CHECK(!enforcePerfBudgets || worst < 60.0);
}

TEST(perf, physics_tick_cost) {
    initGame();
    resetWorld();
    Input up;
    up.up = true;
    const int N = 120 * 60;                  // one minute of ticks
    double t0 = nowMs();
    for (int i = 0; i < N; i++) {
        P.update(up, 1.f / 120.f);
        P.prevPos = P.pos;
    }
    double us = (nowMs() - t0) * 1000.0 / N;
    bench("Player::update()", us, "one 120 Hz physics tick");
    printf("    [physics] %.2f us/tick -> %.1f%% of one core at 120 Hz\n", us, us * 120.0 / 10000.0);
    CHECK(!enforcePerfBudgets || us < 120.0);
}

TEST(perf, rendering_cpu_budget) {
    initGame();
    resetWorld();
    Input in;
    in.up = true;
    M4 vp = mPerspective(1.1f, 1.7f, 0.1f, 1000.f) * mLookAt(V3(0, 2, 30), V3(0, 1, 0), V3(0, 1, 0));
    Sim sim;
    size_t verts = 0, tris = 0, hudQuads = 0;
    double tBuild = 0, tHud = 0;
    const int frames = 600;
    double t0 = nowMs();
    for (int f = 0; f < frames; f++) {
        if (f % 90 == 0) { in.ollie = true; in.olliePress = true; }
        if (f % 90 == 45) { in.ollie = false; in.flipPress = true; }
        sim.tick(in, 1.f / 60.f);
        DM.clear();
        double a = nowMs();
        drawSkater(DM, P, in);
        drawNpcs(DM, cam.pos);
        drawPigeons(DM, cam.pos);
        drawTraffic(DM, cam.pos);
        drawSignals(DM);
        drawLetters(DM, P, f / 60.f);
        tBuild += nowMs() - a;
        verts += DM.v.size();
        tris += DM.idx.size() / 3;
        a = nowMs();
        hud.begin(1920, 1080);
        hud.vignette(0.55f);
        drawGameHud(P, f / 60.f, 60.f, true, 1, 1.f, vp, cam.pos, true, 60.f);
        tHud += nowMs() - a;
        hudQuads += hud.idx.size() / 6;
    }
    double totalMs = nowMs() - t0;
    double meshUs = tBuild * 1000.0 / frames, hudUs = tHud * 1000.0 / frames;
    bench("mesh build / frame", meshUs, fmt("%.0f verts, %.0f tris uploaded", verts / (double)frames, tris / (double)frames));
    bench("HUD build / frame", hudUs, fmt("%.0f quads", hudQuads / (double)frames));
    bench("CPU total / frame", totalMs * 1000.0 / frames, "mesh + HUD + world systems");
    printf("    [frame] mesh %.0f us, HUD %.0f us, total %.0f us (budget 16600 us at 60 FPS)\n",
           meshUs, hudUs, totalMs * 1000.0 / frames);
    CHECK(!enforcePerfBudgets || meshUs < 2500.0);
    CHECK(!enforcePerfBudgets || hudUs < 1200.0);
    CHECK(!enforcePerfBudgets || totalMs * 1000.0 / frames < 12000.0);
}

TEST(perf, world_systems_tick_cost) {
    initGame();
    resetWorld();
    Sim sim;
    Input in;
    const int N = 120 * 30;
    double t0 = nowMs();
    for (int i = 0; i < N; i++) sim.tick(in, 1.f / 120.f);
    double us = (nowMs() - t0) * 1000.0 / N;
    bench("world systems / tick", us, "npcs + pigeons + traffic + particles");
    printf("    [world] %.2f us/tick (120 Hz) with %zu pedestrians, %zu pigeons, %zu cars, %zu particles\n",
           us, npcs.size(), pigeons.size(), cars.size(), parts.size());
    CHECK(!enforcePerfBudgets || us < 200.0);
}

TEST(perf, simulation_runs_far_faster_than_realtime) {
    initGame();
    resetWorld();
    Sim sim;
    Input in;
    in.up = true;
    const int N = 120 * 120;                 // two simulated minutes
    double t0 = nowMs();
    for (int i = 0; i < N; i++) {
        if (i % 300 == 0) { in.ollie = true; in.olliePress = true; }
        if (i % 300 == 40) { in.ollie = false; }
        if (i % 300 == 60) { in.flipPress = true; }
        sim.tick(in, 1.f / 120.f);
    }
    double ms = nowMs() - t0;
    double ratio = (N / 120.0) / (ms / 1000.0);
    bench("simulation speed", ms * 1000.0 / N, fmt("%.0fx real time (120 s in %.0f ms)", ratio, ms));
    printf("    [sim] 120 s of gameplay in %.0f ms = %.0fx real time\n", ms, ratio);
    CHECK(!enforcePerfBudgets || ratio > 20.0);                     // headroom for CI machines
}

TEST(perf, mesh_budget_of_the_static_world) {
    initGame();
    size_t tris = SM.idx.size() / 3;
    double mb = SM.v.size() * sizeof(Vtx) / 1048576.0;
    bench("static world mesh", (double)tris, fmt("%.2f MB vertex data, %zu verts", mb, SM.v.size()));
    printf("    [mesh] %zu static triangles (%.2f MB), %zu collision solids\n", tris, mb, world.solids.size());
    CHECK(tris < 200000);                    // a chunked/culled renderer is still a TODO
    CHECK(mb < 12.0);
    CHECK(WM.idx.size() > 0);                // water is uploaded separately
}

TEST(perf, no_unbounded_growth_over_time) {
    initGame();
    resetWorld();
    Sim sim;
    Input in;
    for (int i = 0; i < 120 * 20; i++) sim.tick(in, 1.f / 120.f);
    size_t npcsAfter = npcs.size(), partsAfter = parts.size(), popupsAfter = popups.size();
    for (int i = 0; i < 120 * 120; i++) sim.tick(in, 1.f / 120.f);
    printf("    [growth] npcs %zu->%zu, particles %zu->%zu, popups %zu->%zu\n",
           npcsAfter, npcs.size(), partsAfter, parts.size(), popupsAfter, popups.size());
    CHECK(npcs.size() == npcsAfter);
    CHECK(parts.size() <= 7000);
    CHECK(popups.size() <= 16);
    CHECK(pigeons.size() < 200);
}

TEST(perf, particle_system_scales_with_the_cap) {
    initGame();
    resetWorld();
    parts.clear();
    for (int i = 0; i < 7000; i++) {
        Particle q;
        q.p = V3(0, 1, 0);
        q.v = V3(0, 0.1f, 0);
        q.life = q.maxLife = 30.f;
        q.water = false;
        q.grav = 0.f;
        addParticle(q);
    }
    double t0 = nowMs();
    for (int i = 0; i < 300; i++) updateParticles(1.f / 60.f, V3(0, 1, 0));
    double us = (nowMs() - t0) * 1000.0 / 300;
    bench("particles (7000 alive)", us, "update pass, worst case");
    printf("    [particles] %.1f us/frame with a full pool\n", us);
    CHECK(parts.size() == 7000);
    CHECK(!enforcePerfBudgets || us < 3000.0);
}

} // namespace hns
