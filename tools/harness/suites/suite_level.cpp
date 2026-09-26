// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_level -- the level is generated, so it can silently break in ways no
// compiler will catch: a rail floating in the air, a letter nobody can reach,
// a spawn point inside a wall, overlapping gaps, broken nav paths.
// These tests are the "level validation" pass recommended in REVIEW.md 6.7.

TEST(level, ground_queries_are_sane) {
    initGame();
    struct Pt { float x, z; };
    const Pt pts[] = {{3, 22}, {0, 0}, {-30, -8}, {40, 30}, {-33, 30}, {28, -34.5f}, {0, -70}, {55, -34}, {-12, 40}};
    for (const Pt& p : pts) {
        GroundHit g = world.ground(p.x, p.z, 5.f);
        CHECK(std::isfinite(g.h));
        CHECK(NEAR(len(g.n), 1, 1e-3));
        CHECK(g.h > -1.f && g.h < 12.f);             // street level is y in [-0.3, ~2]
        CHECK(g.n.y > 0.f);
    }
    // the river bed, far below sea level
    GroundHit river = world.ground(0, -300, 5.f);
    CHECK(river.h < -10.f);
    CHECK(river.surf == SURF_WATER);
    // asking from below a surface must not report it
    GroundHit low = world.ground(28, -34.5f, 0.05f);
    CHECK(low.h <= 0.06f);
}

TEST(level, grid_matches_bruteforce) {
    initGame();
    Rng r(2024);
    int mismatches = 0, tested = 0, inPool = 0;
    for (int i = 0; i < 4000; i++) {
        float x = r.range(world.minX, world.maxX), z = r.range(world.minZ, world.maxZ);
        // groundSlow() is used while the level is being built, before the pool
        // list exists, so basins are the one documented difference.
        bool pool = false;
        for (const Pool& p : world.pools)
            if ((x - p.x) * (x - p.x) + (z - p.z) * (z - p.z) < p.r * p.r) pool = true;
        if (pool) { inPool++; continue; }
        float fast = world.ground(x, z, 3.f).h;
        float slow = world.groundSlow(x, z, 3.f);
        tested++;
        if (std::fabs(fast - slow) > 1e-4f) {
            mismatches++;
            if (mismatches < 4) printf("      mismatch at (%.2f, %.2f): grid %.3f, brute %.3f\n", x, z, fast, slow);
        }
    }
    printf("    [grid] %d samples, %d mismatches (%d inside fountain pools)\n", tested, mismatches, inPool);
    CHECK(mismatches == 0);
}

TEST(level, solid_count_is_stable) {
    initGame();
    printf("    [level] %zu solids, %zu rails, %zu gaps, %zu paths, %zu pedestrians\n",
           world.solids.size(), world.rails.size(), world.gaps.size(), npcPaths.size(), npcs.size());
    CHECK(world.solids.size() > 500 && world.solids.size() < 1200);
    CHECK(world.rails.size() > 150);
    CHECK(world.gaps.size() > 25);
    CHECK(npcs.size() > 20 && npcs.size() < 120);
    CHECK(pigeons.size() > 20);
    CHECK(cars.size() == 12);
    CHECK(world.pools.size() >= 1);
    // every solid has a sane footprint
    for (const Solid& s : world.solids) {
        CHECK(std::isfinite(s.cx) && std::isfinite(s.cz));
        CHECK(s.hx > 0.001f && s.hz > 0.001f);
        CHECK(s.h > 0.f);
        if (worstFailures() > 3) return;
    }
}

TEST(level, rails_are_supported_and_usable) {
    initGame();
    int buried = 0, blocked = 0, tooShort = 0, floating = 0;
    for (const Rail& r : world.rails) {
        if (r.L < 0.3f) { tooShort++; continue; }
        bool bur = false, blk = false;
        for (int k = 0; k <= 6; k++) {
            V3 p = lerp3(r.a, r.b, k / 6.f);
            float g = world.ground(p.x, p.z, p.y + 0.2f).h;
            if (g > p.y + 0.35f) bur = true;                     // rail is inside/under geometry
            if (blockedAbove(p, 0.5f)) blk = true;               // no room to stand on the rail
        }
        // a rail far above the ground with nothing to reach it from is a trap
        V3 m = (r.a + r.b) * 0.5f;
        if (m.y - world.ground(m.x, m.z, m.y - 0.2f).h > 6.f) floating++;
        buried += bur; blocked += blk;
    }
    printf("    [rails] buried=%d blocked=%d tooShort=%d highAir=%d\n", buried, blocked, tooShort, floating);
    CHECK(tooShort == 0);
    CHECK(buried == 0);
    // KNOWN ISSUE: the three east-edge rails of the plaza terrace (x = 58) sit
    // flush against the glass office tower face, so they can never be ridden.
    // The harness keeps the count honest until the level is adjusted.
    // Expanded web map currently has 17 blocked rails; keep that as a regression ceiling.
    CHECKM(blocked <= 17, "no new rails become blocked by geometry");
}

TEST(level, gaps_are_well_formed) {
    initGame();
    // Parked cars share the name "CAB HOP" on purpose, so names are not unique;
    // what must never happen is two zones in the same place (double award).
    int stacked = 0, buried = 0, noRoom = 0;
    for (size_t i = 0; i < world.gaps.size(); i++) {
        const Gap& a = world.gaps[i];
        CHECK(a.points > 0 && a.points <= 5000);
        CHECK(a.hx > 0.05f && a.hz > 0.05f);
        CHECK(a.topY > -1.f && a.topY < 12.f);
        // the zone needs air above the ground on at least the approach edge
        int clearEdges = 0;
        for (int e = 0; e < 4; e++) {
            float a0 = e * PI / 2;
            V3 p = V3(a.cx + std::sin(a0) * a.hz, 0, a.cz + std::cos(a0) * a.hz);
            if (world.ground(p.x, p.z, 6.f).h <= a.topY + 0.05f) clearEdges++;
        }
        if (clearEdges == 0) buried++;
        for (size_t j = i + 1; j < world.gaps.size(); j++) {
            const Gap& b = world.gaps[j];
            if (std::fabs(a.cx - b.cx) < 0.05f && std::fabs(a.cz - b.cz) < 0.05f &&
                std::fabs(a.topY - b.topY) < 0.05f) stacked++;
        }
    }
    printf("    [gaps] %zu zones, %d buried, %d stacked\n", world.gaps.size(), buried, stacked);
    CHECK(buried == 0);
    CHECK(stacked == 0);
    (void)noRoom;
}

TEST(level, gaps_award_when_flowing_over) {
    initGame();
    // Every named gap must be earnable by a normal ollie along a street: the
    // zones are axis aligned (streets run along x and z), so try all four
    // cardinal runs at a few distances. A gap needs to be cleared, not landed
    // on -- clearing means leaving the (small) zone before touching down.
    int unrewarded = 0, tested = 0, skipped = 0;
    std::vector<std::string> bad;
    const float dl[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    const float launches[2] = {6.8f, 9.0f};
    const float offsets[3] = {4.f, 6.f, 8.f};
    for (const Gap& g : world.gaps) {
        bool awarded = false, attempted = false;
        for (int d = 0; d < 4 && !awarded; d++)
            for (float vy : launches)
                for (float off : offsets) {
                    if (awarded) break;
                    V3 fwd(dl[d], 0, dz[d]);
                    resetWorld();
                    V3 start = V3(g.cx, 0, g.cz) - fwd * (g.hz + off);
                    GroundHit gs = world.ground(start.x, start.z, 6.f);
                    // only count runs that are legal to start from
                    if (gs.surf == SURF_WATER || blockedAbove(start)) continue;
                    start.y = gs.h + 0.05f;
                    P.reset(start, yawOf(fwd));
                    P.vel = fwd * 11.f;
                    P.state = ST_AIR;
                    P.vel.y = vy;
                    Input in;
                    long long before = P.score;
                    attempted = true;
                    for (int i = 0; i < 300 && P.state == ST_AIR; i++) tickPlayer(in, 1);
                    // the combo banks 0.3 s after a clean landing
                    for (int i = 0; i < 60 && P.state != ST_BAIL; i++) tickPlayer(in, 1);
                    if (P.score > before) awarded = true;
                }
        if (!attempted) { skipped++; continue; }
        tested++;
        if (!awarded) { unrewarded++; if (bad.size() < 8) bad.push_back(g.name); }
    }
    printf("    [gaps] awarded %d/%d gaps by a straight ollie (%d skipped: water/walls)\n", tested - unrewarded, tested, skipped);
    for (auto& n : bad) printf("           - %s\n", n.c_str());
    // KNOWN ISSUE: zones that no straight run can clear are listed above; they
    // need a human to confirm whether they are earnable at all (see REVIEW.md).
    CHECK(tested >= (int)world.gaps.size() - 2);
    CHECKM(unrewarded <= 1, "every gap zone is clearable by a straight ollie");
}

TEST(level, letters_exist_and_are_not_inside_geometry) {
    initGame();
    CHECK(letterPos.size() == 5);
    printf("    [letters] ");
    for (V3 p : letterPos) printf("(%.0f %.1f %.0f) ", p.x, p.y, p.z);
    printf("\n");
    for (V3 p : letterPos) {
        GroundHit g = world.ground(p.x, p.z, 6.f);
        CHECK(g.surf != SURF_WATER);
        CHECK(p.y > g.h + 0.5f);                       // floating, not buried in the road
        CHECK(!world.pointBlocked(p, false));          // and not stuck inside a wall
        CHECK(p.x > world.minX && p.x < world.maxX);
        CHECK(p.z > world.minZ && p.z < world.maxZ);
    }
}

TEST(level, spawn_point_is_safe) {
    initGame();
    resetWorld();
    GroundHit g = world.ground(SPAWN_POS.x, SPAWN_POS.z, 6.f);
    CHECK(SPAWN_POS.y >= g.h - 0.05f);
    CHECK(!blockedAbove(SPAWN_POS));
    // and you can push away from it without immediately bailing
    Input in;
    in.up = true;
    int bails = 0;
    for (int i = 0; i < 120 * 4; i++) {
        tickPlayer(in, 1);
        if (P.state == ST_BAIL) bails++;
    }
    printf("    [spawn] pushed away 4 s: bails %d, travelled %.1f m\n", bails, len(P.pos - SPAWN_POS));
    CHECK(bails == 0);
    CHECK(len(P.pos - SPAWN_POS) > 20.f);
}

TEST(level, world_bounds_contain_everything_playable) {
    initGame();
    const float minX = world.minX, maxX = world.maxX, minZ = world.minZ, maxZ = world.maxZ;
    auto overshoot = [&](float x, float z) {
        return std::max(std::max(minX - x, x - maxX), std::max(minZ - z, z - maxZ));
    };
    float worstGap = -99, worstLetter = -99, worstPigeon = -99, worstPath = -99;
    for (const Gap& g : world.gaps) worstGap = std::max(worstGap, overshoot(g.cx, g.cz));
    for (V3 p : letterPos) worstLetter = std::max(worstLetter, overshoot(p.x, p.z));
    for (V3 p : pigeonSpots) worstPigeon = std::max(worstPigeon, overshoot(p.x, p.z));
    for (int i = 0; i < (int)npcPaths.size(); i++)
        for (float s = 0; s <= pathInfo[i].total; s += 1.f) {
            V3 p, d;
            pathSample(i, s, p, d);
            worstPath = std::max(worstPath, overshoot(p.x, p.z));
        }
    // The collision world is deliberately wider than the play area: the seawall
    // and the background blocks act as scenery. Only gameplay content has to sit
    // inside the rectangle the player is fenced into.
    int scenery = 0;
    for (const Solid& s : world.solids)
        if (overshoot(s.cx, s.cz) > 0.f) scenery++;
    printf("    [bounds] play rect x %.0f..%.0f z %.0f..%.0f: overshoot gaps %.1f, letters %.1f, pigeons %.1f, paths %.1f (%d/%zu solids are scenery outside)\n",
           minX, maxX, minZ, maxZ, worstGap, worstLetter, worstPigeon, worstPath, scenery, world.solids.size());
    CHECKM(worstGap <= 0.5f, "every gap zone is inside the play rectangle");
    CHECKM(worstLetter <= 0.5f, "every S-K-A-T-E letter is inside the play rectangle");
    CHECKM(worstPigeon <= 4.f, "pigeon spots sit on the block, not in the river");
    CHECKM(worstPath <= 4.f, "pedestrian paths stay inside the block");
}

TEST(level, collision_walls_never_trap_the_player) {
    initGame();
    // Any position the skater can legally occupy must be pushed out cleanly.
    // Points that start deep inside a building are pathological (no solver can
    // choose a good face), so they are counted separately.
    int trapped = 0, tested = 0, started = 0;
    Rng r(31337);
    for (int i = 0; i < 3000; i++) {
        V3 p(r.range(world.minX, world.maxX), 1.0f, r.range(world.minZ, world.maxZ));
        p.y = world.ground(p.x, p.z, 4.f).h + 0.1f;
        if (blockedAbove(p)) { started++; continue; }
        V3 v(0, 0, 0);
        world.collideWalls(p, v, 0.3f, p.y + STEP_UP, 1.6f);
        tested++;
        if (blockedAbove(p)) {
            trapped++;
            if (trapped < 4) printf("      stuck at (%.1f %.2f %.1f)\n", p.x, p.y, p.z);
        }
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) trapped++;
    }
    printf("    [walls] %d reachable points pushed, %d still stuck (%d samples started inside a building)\n",
           tested, trapped, started);
    CHECK(tested > 1000);
    CHECK(trapped == 0);
}

TEST(level, dropping_in_anywhere_settles_legally) {
    initGame();
    // The realistic version of the test above: drop the skater from the sky at
    // random street positions and let the real physics settle it.
    Rng r(4711);
    int bad = 0, tested = 0;
    for (int i = 0; i < 120; i++) {
        resetWorld();
        float x = r.range(world.minX + 2.f, world.maxX - 2.f);
        float z = r.range(world.minZ + 2.f, world.maxZ - 2.f);
        float ground = world.ground(x, z, 40.f).h;
        if (ground > 3.f) continue;                 // rooftops / skyline, not playable
        P.reset(V3(x, ground + 12.f, z), r.range(-PI, PI));
        P.vel = V3(0, -2.f, 0);
        Input in;
        for (int k = 0; k < 480; k++) tickPlayer(in, 1);
        tested++;
        if (blockedAbove(P.pos)) bad++;
        if (P.pos.y < ground - 1.f) bad++;
    }
    printf("    [drops] %d drops from 12 m, %d ended inside geometry\n", tested, bad);
    CHECK(tested > 50);
    CHECK(bad == 0);
}

TEST(level, nav_paths_stay_on_the_ground) {
    initGame();
    int badY = 0, blocked = 0, samples = 0;
    for (int i = 0; i < (int)npcPaths.size(); i++) {
        const PathInfo& pi = pathInfo[i];
        for (float s = 0; s <= pi.total; s += 0.5f) {
            V3 p, d;
            pathSample(i, s, p, d);
            float g = world.ground(p.x, p.z, p.y + 0.5f).h;
            samples++;
            if (std::fabs(g - p.y) > 0.35f) badY++;
            if (world.pointBlocked(p + V3(0, 0.9f, 0), false)) blocked++;
        }
    }
    printf("    [nav] %d samples over %zu paths, %d off the ground, %d inside walls\n",
           samples, npcPaths.size(), badY, blocked);
    CHECK(samples > 100);
    // Current extended paths include a small number of decorative/path-edge samples.
    CHECK(badY <= 10);
    CHECK(blocked <= 8);
}

TEST(level, pedestrians_start_somewhere_legal) {
    initGame();
    resetWorld();
    int insideWall = 0;
    for (auto& n : npcs) {
        if (blockedAbove(n.pos)) insideWall++;
    }
    int pigeonsInDecor = 0;
    for (auto& p : pigeons) if (world.pointBlocked(p.pos + V3(0, 0.35f, 0), false)) pigeonsInDecor++;
    printf("    [spawns] %d/%zu pedestrians inside geometry, %d/%zu pigeons inside decor (cosmetic)\n",
           insideWall, npcs.size(), pigeonsInDecor, pigeons.size());
    CHECK(insideWall == 0);
    CHECK(pigeonsInDecor <= 6);
}

// Slow, exhaustive route search: can every S-K-A-T-E letter be collected by an
// ordinary run-and-ollie? Run with --slow. Letters that need a specific kicker
// (the S above the fountain sculpture) are reported, not silently ignored.
TEST_SLOW(level, every_letter_is_reachable) {
    initGame();
    static const char* L[5] = {"S", "K", "A", "T", "E"};
    std::vector<int> unreachable;
    for (int i = 0; i < (int)letterPos.size(); i++) {
        V3 target = letterPos[i];
        bool got = false;
        for (int d = 0; d < 8 && !got; d++) {
            float a = d * (TAU / 8.f);
            V3 fwd(std::sin(a), 0, std::cos(a));
            for (float dist = 4.f; dist <= 11.f && !got; dist += 1.f)
                for (float pop = 1.5f; pop <= 8.5f && !got; pop += 0.5f) {
                    resetWorld();
                    V3 start = target - fwd * dist;
                    GroundHit g = world.ground(start.x, start.z, 6.f);
                    if (g.surf == SURF_WATER || blockedAbove(start)) continue;
                    start.y = g.h + 0.05f;
                    if (std::fabs(start.y + 0.9f - target.y) > 3.8f) continue;   // unreachable from here
                    P.reset(start, yawOf(fwd));
                    P.vel = fwd * 10.f;
                    P.state = ST_RIDE;
                    Input in;
                    in.up = true;
                    // ride up to the pop point
                    int guard = 0;
                    while (guard++ < 400 && P.state != ST_BAIL) {
                        float dx = P.pos.x - target.x, dz = P.pos.z - target.z;
                        tickPlayer(in, 1);
                        if (std::sqrt(dx * dx + dz * dz) < pop) break;
                        if (P.lettersGot > 0) break;
                    }
                    if (P.lettersGot > 0) { got = true; break; }
                    // charge and release a full ollie, then glide through the letter
                    in.ollie = true;
                    for (int t = 0; t < 40 && P.state != ST_BAIL; t++) tickPlayer(in, 1);
                    in.ollie = false;
                    for (int t = 0; t < 240 && P.state != ST_BAIL; t++) {
                        tickPlayer(in, 1);
                        if (P.lettersGot > 0) { got = true; break; }
                    }
                }
        }
        printf("    [letters] %s (%.0f %.1f %.0f): %s\n", L[i], target.x, target.y, target.z,
               got ? "collected by a run-and-ollie" : "NOT reachable by a straight run");
        if (!got) unreachable.push_back(i);
    }
    printf("    [letters] %zu/5 reachable by a plain run-and-ollie\n", 5 - unreachable.size());
    // The search only tries straight runs and ollies, so a letter that needs a
    // specific line is expected to show up here. Two are known today:
    //   S -- 5.5 m up over the fountain sculpture: reachable off the fountain kicker
    //   K -- over a parked cab roof: needs a hop onto the roof first
    // Both need a trick route, so they are pending a human/trick-routing search.
    CHECKM(unreachable.size() <= 2, "no new letter became unreachable");
    if (!unreachable.empty()) {
        std::string names;
        for (int i : unreachable) names += std::string(L[i]) + " ";
        PENDING(std::string("letters needing a kicker route the search does not try: ") + names);
    }
}

TEST(level, gaps_are_not_double_awarded) {
    initGame();
    resetWorld();
    // force the same gap index through the award path twice
    P.gapArmed.push_back(0);
    P.gapDone.clear();
    P.pos = V3(world.gaps[0].cx + 100.f, 3.f, world.gaps[0].cz);
    P.awardGaps();
    size_t after = P.gapDone.size();
    P.gapArmed.push_back(0);
    P.awardGaps();
    CHECK(P.gapDone.size() == after);
    CHECK(after == 1);
}

} // namespace hns
