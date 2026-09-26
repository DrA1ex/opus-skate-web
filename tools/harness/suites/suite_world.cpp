// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_world -- the living-city systems: pedestrians, pigeons, traffic,
// signals and particles. These run every frame and are the easiest place for a
// subtle regression to hide, because nothing crashes when a pedestrian walks
// through a wall.

TEST(world, pedestrians_follow_their_paths) {
    initGame();
    resetWorld();
    std::vector<V3> start;
    for (auto& n : npcs) start.push_back(n.pos);
    Sim sim;
    sim.run(Input(), 20.f);
    int moved = 0, offPath = 0, offGround = 0;
    for (size_t i = 0; i < npcs.size(); i++) {
        if (lenXZ(npcs[i].pos - start[i]) > 0.5f) moved++;
        // nearest point on the path polyline
        const NpcPath& np = npcPaths[npcs[i].path];
        float best = 1e9f;
        for (size_t k = 0; k + 1 < np.pts.size(); k++) {
            V3 a = np.pts[k], b = np.pts[k + 1];
            V3 ab = b - a;
            float t = clampf(dot(npcs[i].pos - a, ab) / std::max(1e-4f, dot(ab, ab)), 0.f, 1.f);
            best = std::min(best, lenXZ(npcs[i].pos - (a + ab * t)));
        }
        if (best > 2.2f) offPath++;
        if (std::fabs(npcs[i].pos.y - world.ground(npcs[i].pos.x, npcs[i].pos.z, npcs[i].pos.y + 0.6f).h) > 0.5f) offGround++;
    }
    printf("    [npcs] %d/%zu moved, %d off-path, %d off-ground\n", moved, npcs.size(), offPath, offGround);
    CHECK(moved == (int)npcs.size());
    CHECK(offPath == 0);
    CHECK(offGround == 0);
}

TEST(world, pedestrians_stay_inside_the_map) {
    initGame();
    resetWorld();
    Sim sim;
    Input in;
    // 60 s with the skater roaming the whole block
    Rng r(11);
    for (int f = 0; f < 60 * 60; f++) {
        if (f % 60 == 0) {
            in.up = r.chance(0.7f);
            in.left = r.chance(0.2f);
            in.right = r.chance(0.2f);
        }
        sim.tick(in);
    }
    for (auto& n : npcs) {
        CHECK(n.pos.x > world.minX - 2.f && n.pos.x < world.maxX + 2.f);
        CHECK(n.pos.z > world.minZ - 2.f && n.pos.z < world.maxZ + 2.f);
        CHECK(!world.pointBlocked(n.pos + V3(0, 0.5f, 0), false));
        if (worstFailures() > 3) return;
    }
}

TEST(world, pedestrians_dodge_a_charging_skater) {
    initGame();
    resetWorld();                        // primes every pedestrian position
    Npc& n = npcs[0];
    // put the skater 5 m behind the pedestrian on its own path, aimed at it
    V3 dir = norm(V3(std::sin(n.yaw), 0, std::cos(n.yaw)));
    P.reset(n.pos - dir * 5.f, yawOf(dir));
    P.vel = dir * 9.f;
    P.state = ST_RIDE;
    float lat0 = n.targetLat;
    updateNpcs(1.f / 60.f, P, P.score);
    bool reacted = n.dodgeCool > 0.f || n.targetLat != lat0;
    printf("    [dodge] reacted=%d targetLat %.2f -> %.2f (cool %.2f)\n",
           (int)reacted, lat0, n.targetLat, n.dodgeCool);
    CHECKM(reacted, "a pedestrian steps aside for a skater on a collision course");
    // and a slow skater is ignored: no dodging without speed
    resetWorld();
    Npc& m = npcs[0];
    V3 d2 = norm(V3(std::sin(m.yaw), 0, std::cos(m.yaw)));
    P.reset(m.pos - d2 * 5.f, yawOf(d2));
    P.vel = d2 * 1.f;                    // under the 3 m/s reaction threshold
    P.state = ST_RIDE;
    float lat1 = m.targetLat;
    updateNpcs(1.f / 60.f, P, P.score);
    printf("    [dodge] creeping up at 1 m/s: targetLat %.2f -> %.2f (cool %.2f)\n",
           lat1, m.targetLat, m.dodgeCool);
    CHECK(m.dodgeCool <= 0.f && m.targetLat == lat1);
}

TEST(world, running_into_a_pedestrian_bails_the_skater) {
    initGame();
    resetWorld();
    Npc& n = npcs[0];
    V3 dir = norm(V3(std::sin(n.yaw), 0, std::cos(n.yaw)));
    P.reset(n.pos, yawOf(dir));
    P.pos = n.pos + V3(-0.2f, 0, 0);     // right on top of them
    P.vel = dir * 7.f;
    P.state = ST_RIDE;
    long long dummy = 0;
    updateNpcs(1.f / 60.f, P, dummy);
    printf("    [hit] state=%d why='%s' knocked=%.2f\n", P.state, P.bailWhy.c_str(), n.knocked);
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("PEDESTRIAN") != std::string::npos);
    CHECK(n.knocked > 0.f);
}

TEST(world, pedestrians_cheer_a_big_line) {
    initGame();
    resetWorld();
    bubbles.clear();
    // drop the skater next to a pedestrian and jump the score
    Npc& n = npcs[0];
    P.reset(n.pos + V3(1.5f, 0, 0), PI);
    long long seen = P.score;
    P.score = 5000;                     // a big bank in one go
    updateNpcs(1.f / 60.f, P, seen);
    printf("    [cheer] bubbles=%zu text='%s'\n", bubbles.size(), bubbles.empty() ? "" : bubbles[0].text.c_str());
    CHECK(!bubbles.empty());
    CHECK(bubbles[0].t > 0.f);
    // and the bubble expires
    for (auto& b : bubbles) b.t -= 3.f;
    bubbles.erase(std::remove_if(bubbles.begin(), bubbles.end(), [](const Bubble& b) { return b.t <= 0; }), bubbles.end());
    CHECK(bubbles.empty());
}

TEST(world, pigeons_fly_off_and_come_home) {
    initGame();
    resetWorld();
    Pigeon& p = pigeons[0];
    P.reset(p.pos + V3(0.5f, 0, 0), 0);
    P.vel = V3(3, 0, 0);
    P.state = ST_RIDE;
    updatePigeons(1.f / 60.f, P);
    CHECK(p.state == 1);                       // startled
    float y0 = p.pos.y;
    for (int i = 0; i < 60; i++) updatePigeons(1.f / 60.f, P);
    CHECK(p.pos.y > y0);
    CHECK(p.flap > 0.f);
    // state machine: 1 = fleeing (4 s), 2 = gone, 0 = back on the ground. It
    // only comes home when the skater is more than 15 m away for 6 s.
    int guard = 0;
    while (p.state == 1 && guard++ < 60 * 20) updatePigeons(1.f / 60.f, P);
    CHECK(p.state == 2);                       // out of sight
    for (int i = 0; i < 60 * 8; i++) updatePigeons(1.f / 60.f, P);
    printf("    [pigeons] still with the skater nearby: state %d (t %.1f s)\n", p.state, p.t);
    CHECKM(p.state == 2, "a pigeon does not walk back home while the skater is next to it");
    P.pos = V3(p.home.x + 60.f, 0, p.home.z);  // skate away
    guard = 0;
    while (p.state != 0 && guard++ < 60 * 20) updatePigeons(1.f / 60.f, P);
    printf("    [pigeons] back home after %.1f s away: state %d, %.2f m from home\n",
           guard / 60.f, p.state, len(p.pos - p.home));
    CHECK(p.state == 0);                       // back on the ground
    CHECK(NEAR(len(p.pos - p.home), 0, 0.01f));
}

TEST(world, traffic_light_cycle) {
    initGame();
    resetWorld();
    // sample the whole 32 s cycle
    int ewGreen = 0, ewYellow = 0, ewRed = 0, nsGreen = 0, bothGreen = 0;
    for (int i = 0; i < 320; i++) {
        tlTimer = i * 0.1f;
        int e = ewPhase(), n = nsPhase();
        if (e == 0) ewGreen++;
        if (e == 1) ewYellow++;
        if (e == 2) ewRed++;
        if (n == 0) nsGreen++;
        if (e == 0 && n == 0) bothGreen++;
    }
    printf("    [signals] ew green=%d yellow=%d red=%d, ns green=%d, both-green=%d\n", ewGreen, ewYellow, ewRed, nsGreen, bothGreen);
    CHECK(ewGreen > 0 && ewYellow > 0 && ewRed > 0);
    CHECK(nsGreen > 0);
    CHECK(bothGreen == 0);                     // never both directions green
}

TEST(world, cars_stop_at_red_and_go_on_green) {
    initGame();
    resetWorld();
    Car& c = cars[0];                          // lane 0 has a traffic light
    CHECK(LANES[c.lane].hasLight);
    c.x = -30.f;
    c.speed = c.target = 9.f;
    tlTimer = 20.f;                            // east-west red
    for (int i = 0; i < 60 * 4; i++) updateTraffic(1.f / 60.f, P);
    float stoppedX = c.x, stoppedSpeed = c.speed;
    tlTimer = 1.f;                             // green
    for (int i = 0; i < 60 * 4; i++) updateTraffic(1.f / 60.f, P);
    printf("    [traffic] red: x=%.1f v=%.2f | green: x=%.1f v=%.2f\n", stoppedX, stoppedSpeed, c.x, c.speed);
    CHECK(stoppedSpeed < 1.2f);
    CHECK(stoppedX < -12.f);                   // held before the crosswalk
    CHECK(c.speed > 4.f);                      // moving again
}

TEST(world, cars_keep_a_gap_and_never_overlap) {
    initGame();
    resetWorld();
    float minGap = 1e9f;
    float minGapEver = 1e9f;
    for (int i = 0; i < 60 * 60; i++) {
        updateTraffic(1.f / 60.f, P);
        for (auto& a : cars)
            for (auto& b : cars) {
                if (&a == &b || a.lane != b.lane) continue;
                float g = std::fabs(a.x - b.x);
                minGap = std::min(minGap, g);
                minGapEver = std::min(minGapEver, g);
            }
    }
    printf("    [traffic] closest same-lane gap over 60 s: %.2f m\n", minGapEver);
    CHECK(minGapEver > 3.f);
}

TEST(world, being_hit_by_a_car_bails) {
    initGame();
    resetWorld();
    Car& c = cars[0];
    const Lane& L = LANES[c.lane];
    c.speed = 9.f;
    P.reset(V3(c.x + 0.5f, 0, L.z), 0);
    P.state = ST_RIDE;
    P.vel = V3(0, 0, 0);
    updateTraffic(1.f / 60.f, P);
    printf("    [car] state=%d why='%s'\n", P.state, P.bailWhy.c_str());
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("CAB") != std::string::npos || P.bailWhy.find("CAR") != std::string::npos);
    CHECK(P.vel.y > 0.f);                      // thrown into the air
}

TEST(world, cars_slow_for_the_skater) {
    initGame();
    resetWorld();
    Car& c = cars[0];
    const Lane& L = LANES[c.lane];
    c.speed = c.target = 9.f;
    c.x = -40.f;
    P.reset(V3(c.x + 12.f * L.dir, 0, L.z), 0);
    P.state = ST_RIDE;
    P.vel = V3(0, 0, 0);
    c.honk = 0;
    for (int i = 0; i < 60 * 3; i++) updateTraffic(1.f / 60.f, P);
    printf("    [traffic] speed near skater: %.2f (target %.2f), honked=%d\n", c.speed, c.target, (int)(c.honk != 0.f));
    CHECK(c.speed < c.target);
}

TEST(world, particle_emitters_produce_and_expire) {
    initGame();
    resetWorld();
    CHECK(emitters.size() >= 4);
    parts.clear();
    for (int i = 0; i < 60 * 5; i++) updateParticles(1.f / 60.f, emitters[0].pos);
    printf("    [particles] %zu alive after 5 s of fountain\n", parts.size());
    CHECK(parts.size() > 50);
    for (auto& p : parts) CHECK(std::isfinite(p.p.x) && std::isfinite(p.v.y));
    // stop the emitters: everything must die out
    std::vector<float> rates;
    for (auto& e : emitters) { rates.push_back(e.rate); e.rate = 0.f; }
    for (int i = 0; i < 60 * 10; i++) updateParticles(1.f / 60.f, V3(0, 0, 0));
    printf("    [particles] %zu alive after emitters stop\n", parts.size());
    CHECK(parts.size() == 0);
    for (size_t i = 0; i < emitters.size(); i++) emitters[i].rate = rates[i];
}

TEST(world, particle_budget_is_capped) {
    initGame();
    resetWorld();
    parts.clear();
    for (int i = 0; i < 12000 && parts.size() < 7000; i++) {
        Particle q;
        q.p = V3(0, 1, 0);
        q.v = V3(0, 1, 0);
        q.life = q.maxLife = 5.f;
        addParticle(q);
    }
    CHECK(parts.size() == 7000);
    addParticle(parts[0]);
    CHECK(parts.size() == 7000);              // hard cap holds
}

TEST(world, water_particles_die_at_the_pool_surface) {
    initGame();
    resetWorld();
    parts.clear();
    std::vector<Emitter> savedEmitters = emitters;
    emitters.clear();                    // only the droplet under test exists
    float surface = SH + 0.38f;          // fountain water, a little above the floor
    // 1) a droplet over the basin is gone as soon as it crosses the surface
    Particle q;
    q.p = V3(28.f, surface + 0.05f, -34.5f);
    q.v = V3(0, -4.f, 0);
    q.life = q.maxLife = 3.f;
    q.size = 0.06f;
    q.water = true;
    addParticle(q);
    updateParticles(1.f / 120.f, V3(28, 3, -34.5f));
    CHECK(parts.size() <= 2);            // gone, or replaced by one bounce droplet
    // 2) the same droplet over dry road keeps falling (the pool surface is what
    //    stops water in a basin, not the ground query)
    Particle r;
    r.p = V3(3.f, surface + 0.05f, 22.f);
    r.v = V3(0, -4.f, 0);
    r.life = r.maxLife = 3.f;
    r.size = 0.06f;
    r.water = true;
    addParticle(r);
    updateParticles(1.f / 120.f, V3(3, 3, 22.f));
    CHECK(parts.size() == 1);
    // 3) steam over the same basin is not water, so it must survive
    parts.clear();
    Particle st;
    st.p = V3(28.f, surface + 0.05f, -34.5f);
    st.v = V3(0, 1.f, 0);
    st.life = st.maxLife = 3.f;
    st.size = 0.3f;
    st.water = false;
    addParticle(st);
    updateParticles(1.f / 120.f, V3(28, 3, -34.5f));
    CHECK(parts.size() == 1);
    emitters = savedEmitters;            // leave the world as we found it
}

TEST(world, dynamic_draw_lists_are_populated) {
    initGame();
    resetWorld();
    DM.clear();
    Input in;
    drawSkater(DM, P, in);
    size_t skaterVerts = DM.v.size();
    CHECK(skaterVerts > 300 && skaterVerts < 4000);
    CHECK(DM.idx.size() % 3 == 0);
    DM.clear();
    drawNpcs(DM, cam.pos);
    CHECK(DM.v.size() > 5000);                 // all pedestrians are built
    DM.clear();
    drawTraffic(DM, cam.pos);
    CHECK(DM.v.size() > 500);
    DM.clear();
    drawSignals(DM);
    CHECK(DM.v.size() > 0);
    DM.clear();
    drawLetters(DM, P, 1.23f);
    CHECK(DM.v.size() > 0);
    DM.clear();
    drawPigeons(DM, cam.pos);
    CHECK(DM.v.size() > 0);
    // every index must be inside the vertex array
    for (uint32_t i : DM.idx) CHECK(i < DM.v.size());
}

TEST(world, hud_builds_without_index_overrun) {
    initGame();
    resetWorld();
    M4 vp = mPerspective(1.f, 1.7f, 0.1f, 100.f) * mLookAt(V3(0, 2, 10), V3(0, 1, 0), V3(0, 1, 0));
    for (int mode = 0; mode < 4; mode++) {
        hud.begin(1920, 1080);
        hud.vignette(0.55f);
        if (mode == 0) drawGameHud(P, 1.f, 60.f, true, 1, 1.f, vp, cam.pos, true, 60.f);
        else if (mode == 1) drawTitle(1.f);
        else if (mode == 2) drawPause();
        else drawResults(1234567, 999, true, 1.f);
        CHECK(hud.v.size() > 0);
        CHECK(hud.v.size() % 8 == 0);
        for (uint32_t i : hud.idx) CHECK(i < hud.v.size() / 8);
        if (worstFailures() > 3) return;
    }
    // every help/trick page renders
    hud.begin(1280, 720);
    drawHelpPanel(0, 0, 1.f);
    drawTrickPanel(0, 0, 1.f);
    CHECK(hud.v.size() > 0);
}

TEST(world, hud_scales_with_resolution) {
    initGame();
    for (float h : {720.f, 1080.f, 1440.f, 2160.f}) {
        hud.begin(h * 16.f / 9.f, h);
        CHECK(hud.U >= 1.f);
        CHECK(hud.U <= h / 720.f + 0.6f);
    }
}

TEST(world, no_game_rng_draws_in_the_world_update) {
    initGame();
    resetWorld();
    // The particle emitters are the one system that is *supposed* to draw from
    // the gameplay RNG, so park them first: everything else in the world update
    // must leave that stream alone or replays stop being reproducible.
    std::vector<Emitter> savedEmitters = emitters;
    emitters.clear();
    Sim sim;
    uint64_t game0 = prng.draws, npc0 = npcRng.draws, pig0 = pigeonRng.draws;
    sim.run(Input(), 5.f);
    uint64_t game = prng.draws - game0, npc = npcRng.draws - npc0, pig = pigeonRng.draws - pig0;
    emitters = savedEmitters;
    printf("    [rng] 5 s of world updates: game RNG %llu draws, npc %llu, pigeon %llu\n",
           (unsigned long long)game, (unsigned long long)npc, (unsigned long long)pig);
    // world systems own separate streams: the gameplay/particle RNG must stay
    // untouched or replays and determinism checks cannot hold
    CHECKM(game == 0, "the world update draws nothing from the gameplay RNG");
    // ...while the cosmetic streams are alive: pigeons jitter every frame, and
    // pedestrians draw only when they dodge or shout
    CHECKM(pig > 1000, "pigeons use their own stream");
    (void)npc;
}

TEST(world, npc_behaviour_does_not_depend_on_the_particle_rng) {
    initGame();
    uint32_t h[2];
    for (int k = 0; k < 2; k++) {
        resetWorld();
        prng = Rng(k == 0 ? 111u : 999u);        // only the particle stream differs
        Sim sim;
        sim.run(Input(), 5.f);
        h[k] = worldHash();
    }
    CHECK(h[0] == h[1]);
}


TEST(world, runtime_collections_stay_bounded) {
    initGame();
    resetWorld();
    Sim sim;
    Input in;

    for (int i = 0; i < 120 * 10; i++) sim.tick(in, 1.f / 120.f);
    const size_t npcsAfterWarmup = npcs.size();

    for (int i = 0; i < 120 * 60; i++) sim.tick(in, 1.f / 120.f);

    CHECK(npcs.size() == npcsAfterWarmup);
    CHECK(parts.size() <= 7000);
    CHECK(popups.size() <= 16);
    CHECK(pigeons.size() < 200);
}

} // namespace hns
