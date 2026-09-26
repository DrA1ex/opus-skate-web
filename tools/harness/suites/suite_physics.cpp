// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_physics -- movement, curbs, ramps, quarter pipes, water, walls.
// The numbers here are the "feel" of the game: they come straight from the
// constants in skate.cpp, so the tests fail when a tuning change goes too far.

TEST(physics, push_reaches_expected_top_speed) {
    initGame();
    resetWorld();
    Input in;
    in.up = true;
    float prev = -1;
    std::vector<float> log;
    bool monotonicEarly = true;
    for (int sec = 0; sec < 8; sec++) {
        tickPlayer(in, 120);
        float v = len(P.vel);
        log.push_back(v);
        if (sec < 3 && v <= prev) monotonicEarly = false;
        prev = v;
    }
    printf("    [push] m/s per second:");
    for (float v : log) printf(" %.1f", v);
    printf("\n");
    float top = len(P.vel);
    CHECKM(top <= PUSH_MAX + 0.4f, "pushing never exceeds PUSH_MAX");
    CHECK(top > 8.5f);                                    // it actually gets moving
    CHECK(monotonicEarly);                                // accelerates cleanly from a standstill
    CHECK(NEAR(P.pushPhase, 0.f, 1.2f));                  // push cycle stays in range
}

TEST(physics, coasting_slows_down_and_stops) {
    initGame();
    resetWorld();
    P.vel = fwdYaw(P.yaw) * 5.f;
    Input in;
    for (int i = 0; i < 120 * 20 && len(P.vel) > 0.2f; i++) tickPlayer(in, 1);
    printf("    [coast] final speed %.3f m/s\n", len(P.vel));
    CHECK(len(P.vel) < 0.6f);
    CHECK(len(P.vel) > 0.05f);                            // but it does stop from rolling resistance
}

TEST(physics, brakes_kill_speed_quickly) {
    initGame();
    resetWorld();
    Input up;
    up.up = true;
    tickPlayer(up, 120 * 4);
    float fast = len(P.vel);
    up.up = false;
    up.down = true;
    tickPlayer(up, 120);
    printf("    [brake] %.1f -> %.1f m/s in 1 s\n", fast, len(P.vel));
    CHECK(len(P.vel) < fast * 0.45f);
    CHECK(P.braking || len(P.vel) < 0.5f);
}

TEST(physics, ollie_height_and_airtime) {
    initGame();
    resetWorld();
    Input in;
    in.up = true;
    tickPlayer(in, 120 * 3);
    double maxH = 0;
    // full charge
    in.ollie = true; in.olliePress = true;
    tickPlayer(in, 40);
    in.ollie = false;
    tickPlayer(in, 1);
    int frames = 0;
    while (P.state == ST_AIR && frames < 400) {
        tickPlayer(in, 1);
        frames++;
        maxH = std::max(maxH, (double)(P.pos.y - world.ground(P.pos.x, P.pos.z, P.pos.y + 0.05f).h));
    }
    printf("    [ollie] apex %.2f m, air %.2f s\n", maxH, frames / 120.f);
    CHECK(maxH > 1.4 && maxH < 2.4);
    CHECK(frames > 100 && frames < 170);
    // a short tap must be clearly lower than a full charge
    resetWorld();
    Input q;
    q.up = true;
    tickPlayer(q, 120 * 3);
    q.ollie = true; q.olliePress = true;
    tickPlayer(q, 2);
    q.ollie = false;
    tickPlayer(q, 1);
    double shortH = 0;
    frames = 0;
    while (P.state == ST_AIR && frames < 400) {
        tickPlayer(q, 1);
        frames++;
        shortH = std::max(shortH, (double)(P.pos.y - world.ground(P.pos.x, P.pos.z, P.pos.y + 0.05f).h));
    }
    printf("    [ollie] tap %.2f m\n", shortH);
    CHECK(shortH < maxH);
    CHECKM(shortH > 0.55, "a tapping ollie still clears a curb");
}

TEST(physics, coyote_time_allows_a_late_pop) {
    initGame();
    // roll off the avenue curb: the 0.15 m drop opens the late-ollie window
    auto rollOffCurb = [](V3& start) {
        start = V3(-9.6f, 0, -30.f);
        start.y = world.ground(start.x, start.z, 2.f).h;
        P.reset(start, PI / 2);                 // facing +x, towards the road
        P.vel = V3(6, 0, 0);
    };
    V3 start;
    // control: no pop at all, the skater just drops to the road
    resetWorld();
    rollOffCurb(start);
    Input in;
    in.up = true;
    float peakNoPop = start.y;
    int guard = 0;
    while (guard++ < 240 && P.state != ST_BAIL) {
        tickPlayer(in, 1);
        if (P.state != ST_AIR && guard > 60) break;
        peakNoPop = std::max(peakNoPop, P.pos.y);
    }
    // now the same drop, but pop inside the coyote window
    resetWorld();
    rollOffCurb(start);
    Input in2;
    in2.up = true;
    guard = 0;
    while (guard++ < 600 && P.state != ST_AIR) tickPlayer(in2, 1);
    CHECK(P.state == ST_AIR);
    float coyote = P.coyote;
    float dropY = P.pos.y;
    in2.ollie = true;                           // hold: the pop charges in the air
    in2.olliePress = true;
    tickPlayer(in2, 8);
    CHECKM(P.coyote > 0.f, "the late-ollie window is still open after the drop");
    in2.ollie = false;                          // release: the late ollie fires
    tickPlayer(in2, 1);
    float vyAfterPop = P.vel.y;
    float peakPopped = P.pos.y;
    for (int i = 0; i < 60 && P.state == ST_AIR; i++) {
        tickPlayer(in2, 1);
        peakPopped = std::max(peakPopped, P.pos.y);
    }
    printf("    [coyote] window %.3f s at y %.2f -> pop gives vy %.2f, peaks %.2f m\n",
           coyote, dropY, vyAfterPop, peakPopped - dropY);
    printf("    [coyote] without the pop the same drop peaks at %.2f m\n", peakNoPop - dropY);
    CHECKM(vyAfterPop > 3.f, "the late ollie pushed the skater up");
    CHECKM(peakPopped - dropY > 0.25f, "the late ollie gained height above the drop point");
    CHECK(peakPopped - dropY > (peakNoPop - dropY) + 0.2f);
}

TEST(physics, curb_is_either_dropped_or_stepped_up) {
    initGame();
    resetWorld();
    // the west sidewalk of the avenue sits one curb above the road (SPAWN side)
    // 1) roll OFF the curb: the 0.15 m drop must not bail or launch the skater
    V3 up = V3(-9.6f, 0, -30.f);
    up.y = world.ground(up.x, up.z, 2.f).h;
    P.reset(up, PI / 2);                 // facing +x, towards the road
    P.vel = V3(6, 0, 0);
    Input in;
    for (int i = 0; i < 180 && P.pos.x < -8.4f; i++) tickPlayer(in, 1);
    printf("    [curb] dropped to x=%.2f y=%.3f (road is %.2f)\n", P.pos.x, P.pos.y,
           world.ground(-8.f, -30.f, 2.f).h);
    CHECK(P.pos.x > -8.8f);
    CHECK(P.state != ST_BAIL);
    CHECK(P.pos.y <= up.y + 0.05f);
    // 2) roll INTO the curb: 0.15 m is under STEP_UP (0.19 m), so you get lifted
    resetWorld();
    V3 low = V3(-8.0f, 0, -30.f);
    low.y = world.ground(low.x, low.z, 2.f).h;
    P.reset(low, -PI / 2);               // facing -x, towards the sidewalk
    P.vel = V3(-6, 0, 0);
    Input in2;
    for (int i = 0; i < 180 && P.pos.x > -9.4f; i++) tickPlayer(in2, 1);
    printf("    [curb] stepped up to x=%.2f y=%.3f (sidewalk is %.2f)\n", P.pos.x, P.pos.y, SH);
    CHECK(P.pos.x < -8.8f);
    CHECK(NEAR(P.pos.y, SH, 0.02f));
    CHECK(P.state != ST_BAIL);
    // 3) a real wall must stop you
    resetWorld();
    P.reset(V3(-13.4f, 0, -30.f), PI / 2);
    P.vel = V3(9, 0, 0);
    Input w;
    for (int i = 0; i < 240; i++) tickPlayer(w, 1);
    printf("    [wall] ended at x=%.2f (why '%s')\n", P.pos.x, P.bailWhy.c_str());
    CHECK(P.pos.x < -12.5f);
}

TEST(physics, wall_slam_bails_and_respawns) {
    initGame();
    resetWorld();
    P.reset(V3(-13.4f, 0, -30.f), PI / 2);
    P.vel = V3(14, 0, 0);
    Input in;
    tickPlayer(in, 60);
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("WALL") != std::string::npos);
    tickPlayer(in, 300);                     // 2.5 s later
    CHECK(P.state == ST_RIDE);
    CHECK(!world.pointBlocked(P.pos + V3(0, 0.5f, 0), false));
    CHECK(P.pos.y > -1.f);
    CHECK(NEAR(len(P.vel), 0, 0.6f));
}

TEST(physics, river_splashes_and_recovers) {
    initGame();
    resetWorld();
    // note: P.reset() snaps y to the ground, and the river bed is at -40 m --
    // far below the -30 m kill plane -- so set the height AFTER the reset.
    P.reset(V3(0, 0, RIVER_EDGE_Z - 4.f), 0);
    P.pos.y = 6.f;
    P.state = ST_AIR;
    P.vel = V3(0, -2, -4);
    printf("    [river] bed is %.1f m, water surface %.2f m, kill plane -30 m\n",
           world.ground(0, RIVER_EDGE_Z - 4.f, 60.f).h, WATER_LEVEL);
    Input in;
    for (int i = 0; i < 240 && P.state != ST_BAIL; i++) tickPlayer(in, 1);
    CHECK(P.state == ST_BAIL);
    CHECKM(P.bailWhy.find("SPLASH") != std::string::npos || P.bailWhy.find("SWIM") != std::string::npos,
           "bailing in the river explains why");
    tickPlayer(in, 400);
    CHECK(P.state == ST_RIDE);
    CHECK(P.pos.z > RIVER_EDGE_Z);           // respawned on dry land
}

TEST(physics, landing_hard_does_not_teleport_or_launch) {
    initGame();
    resetWorld();
    P.reset(V3(3, 8, 22), 0);
    P.state = ST_AIR;
    P.vel = V3(0, -3, 0);
    Input in;
    for (int i = 0; i < 600 && P.state == ST_AIR; i++) tickPlayer(in, 1);
    CHECK(P.state == ST_RIDE || P.state == ST_BAIL);
    if (P.state == ST_RIDE) {
        CHECK(NEAR(P.pos.y, world.ground(P.pos.x, P.pos.z, P.pos.y + 0.5f).h, 0.05f));
        CHECK(P.landSquash >= 0.f);
        CHECK(P.landSquash <= 1.f);
    } else {
        PENDING("a 8 m drop bails -- acceptable design choice, tracked for balance");
    }
}

TEST(physics, ramps_accelerate_and_launch) {
    initGame();
    resetWorld();
    // the fountain kicker at (19.6, -34.5) faces +x
    V3 start(19.6f - 4.f, 0, -34.5f);
    start.y = world.ground(start.x, start.z, 3.f).h + 0.05f;
    P.reset(start, PI / 2);
    Input in;
    in.up = true;
    tickPlayer(in, 120);
    float entering = lenXZ(P.vel);
    bool leftGround = false, gainedHeight = false;
    for (int i = 0; i < 240; i++) {
        tickPlayer(in, 1);
        if (P.state == ST_AIR && P.vel.y > 1.f) leftGround = true;
        if (leftGround && P.pos.y > start.y + 0.8f) gainedHeight = true;
    }
    printf("    [ramp] entering %.1f m/s, left ground=%d, gained height=%d\n", entering, (int)leftGround, (int)gainedHeight);
    CHECK(gainedHeight);
    CHECK(P.state != ST_BAIL || P.bailWhy.find("WALL") == std::string::npos);
}

TEST(physics, quarter_pipe_returns_you_to_the_deck) {
    initGame();
    resetWorld();
    // The cage quarter pipe: quarterPipe(-33, 44.9, 0, ...) faces -z (ride towards +z? check both)
    V3 qp;
    bool found = false;
    for (const Solid& s : world.solids)
        if (s.type == S_QP) { qp = V3(s.cx, s.y0, s.cz); found = true; break; }
    CHECK(found);
    if (!found) return;
    V3 fz = V3(0, 0, 1);
    V3 start = qp - fz * 12.f;
    start.y = world.ground(start.x, start.z, 3.f).h + 0.05f;
    P.reset(start, 0);
    Input in;
    in.up = true;
    tickPlayer(in, 120 * 2);
    float minY = P.pos.y, maxY = P.pos.y;
    int airFrames = 0;
    for (int i = 0; i < 240; i++) {
        tickPlayer(in, 1);
        minY = std::min(minY, P.pos.y);
        maxY = std::max(maxY, P.pos.y);
        if (P.state == ST_AIR) airFrames++;
    }
    printf("    [qp] y range %.2f..%.2f, air frames %d, qpAir=%d\n", minY, maxY, airFrames, (int)P.qpAir);
    CHECK(maxY > SH + 0.8f);                 // it does launch
    CHECK(minY > -0.5f);                     // and never falls through the world
    CHECK(P.state != ST_BAIL || P.bailWhy.find("SWIM") == std::string::npos);
}

TEST(physics, step_up_does_not_climb_stairs) {
    initGame();
    resetWorld();
    // brownstone stoops are stairs -- the skater must not walk up them
    float top = 0;
    V3 stoop;
    bool found = false;
    for (const Gap& g : world.gaps)
        if (g.name == "STOOP GAP") { stoop = V3(g.cx, 0, g.cz); top = g.topY; found = true; break; }
    CHECK(found);
    if (!found) return;
    V3 start = stoop + V3(0, 0, 3.f);
    start.y = world.ground(start.x, start.z, 3.f).h + 0.05f;
    P.reset(start, PI);
    Input in;
    in.up = true;
    tickPlayer(in, 120 * 2);
    printf("    [stairs] y=%.2f (stoop top %.2f)\n", P.pos.y, top);
    CHECK(P.pos.y < top - 0.3f);             // rolled into the steps, not up them
}

TEST(physics, slopes_keep_momentum) {
    initGame();
    resetWorld();
    // find a ramp and check speed is not destroyed by the surface normal snap
    const Solid* ramp = nullptr;
    for (const Solid& s : world.solids) if (s.type == S_RAMP && s.hz > 0.8f) { ramp = &s; break; }
    CHECK(ramp != nullptr);
    if (!ramp) return;
    V3 fwd(ramp->s, 0, ramp->c);
    V3 start = V3(ramp->cx, 0, ramp->cz) - fwd * (ramp->hz + 3.f);
    start.y = world.ground(start.x, start.z, 3.f).h + 0.05f;
    P.reset(start, yawOf(fwd));
    P.vel = fwd * 9.f;
    Input in;
    float before = lenXZ(P.vel);
    for (int i = 0; i < 90; i++) tickPlayer(in, 1);
    float after = lenXZ(P.vel);
    printf("    [slope] %.1f -> %.1f m/s over the transition\n", before, after);
    CHECK(after > before * 0.6f);
}

TEST(physics, never_falls_out_of_the_world) {
    initGame();
    resetWorld();
    // drop the skater from high above 60 random spots at speed
    Rng r(4242);
    for (int i = 0; i < 60; i++) {
        resetWorld();
        float x = r.range(world.minX, world.maxX), z = r.range(world.minZ, world.maxZ);
        P.reset(V3(x, 12.f, z), r.range(-PI, PI));
        P.state = ST_AIR;
        P.vel = V3(r.range(-8, 8), 0, r.range(-8, 8));
        Input in;
        for (int k = 0; k < 600; k++) tickPlayer(in, 1);
        CHECK(std::isfinite(P.pos.x) && std::isfinite(P.pos.y) && std::isfinite(P.pos.z));
        CHECK(P.pos.y > -35.f);
        CHECK(P.pos.x >= world.minX - 0.5f && P.pos.x <= world.maxX + 0.5f);
        CHECK(P.pos.z >= world.minZ - 0.5f && P.pos.z <= world.maxZ + 0.5f);
        if (worstFailures() > 3) return;
    }
}

TEST(physics, physics_is_stable_at_any_tick_rate) {
    initGame();
    // The game runs a fixed 120 Hz step, but a dropped frame doubles the frame
    // dt. Verify a 30 Hz-ish accumulation produces the same behaviour.
    double apexAt[3] = {0, 0, 0};
    float rates[3] = {1.f / 120.f, 1.f / 60.f, 1.f / 30.f};
    for (int k = 0; k < 3; k++) {
        resetWorld();
        Sim sim;
        Input in;
        in.up = true;
        sim.run(in, 3.f, rates[k]);
        in.ollie = true; in.olliePress = true;
        sim.run(in, 0.33f, rates[k]);
        in.ollie = false;
        double apex = 0;
        for (int i = 0; i < 200; i++) {
            sim.tick(in, rates[k]);
            apex = std::max(apex, (double)(P.pos.y - world.ground(P.pos.x, P.pos.z, P.pos.y + 0.05f).h));
            if (P.state != ST_AIR && i > 4) break;
        }
        apexAt[k] = apex;
    }
    printf("    [tickrate] apex: 120Hz %.2f m, 60Hz %.2f m, 30Hz %.2f m\n", apexAt[0], apexAt[1], apexAt[2]);
    CHECK(NEAR(apexAt[0], apexAt[1], 0.25));
    CHECK(NEAR(apexAt[0], apexAt[2], 0.4));
}
} // namespace hns
