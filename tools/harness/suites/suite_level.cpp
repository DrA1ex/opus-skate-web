// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {
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
