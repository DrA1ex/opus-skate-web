// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_replay -- determinism and a minimal replay format.
// REVIEW.md 6.5 recommends replay/ghost as a feature; that is only possible if
// the simulation is bit-for-bit reproducible from (initial state + inputs).
// These tests are the contract, plus a working prototype of the file format.

TEST(replay, identical_inputs_give_identical_state) {
    initGame();
    uint32_t h[2];
    long long score[2];
    for (int k = 0; k < 2; k++) {
        resetWorld();
        MonkeyStats st = runMonkey(10, 4242u, nullptr, true);
        h[k] = playerHash();
        score[k] = st.score;
    }
    printf("    [determinism] hash %08x vs %08x, score %lld vs %lld\n", h[0], h[1], score[0], score[1]);
    CHECK(h[0] == h[1]);
    CHECK(score[0] == score[1]);
}

TEST(replay, world_state_is_reproducible_too) {
    initGame();
    uint32_t h[2];
    for (int k = 0; k < 2; k++) {
        resetWorld();
        runMonkey(8, 777u, nullptr, true);
        h[k] = worldHash();
    }
    CHECK(h[0] == h[1]);
}
TEST(replay, record_then_replay_reproduces_the_run) {
    initGame();
    resetWorld();

    struct Trace {
        uint32_t player = 0, world = 0;
        V3 pos, vel, npc0, pigeon0;
        float yaw = 0, car0 = 0, timer = 0;
        int state = 0;
        long long score = 0;
    };

    Replay rec;
    std::vector<V3> liveNpcs0, livePigeons0;
    std::vector<float> liveCars0;
    MonkeyRng rng(2024u);
    Sim liveSim;
    std::vector<Trace> trace;
    for (int f = 0; f < 15 * 60; ++f) {
        Input in = monkeyInput(rng, P);
        rec.input.push_back(Replay::pack(in));
        liveSim.tick(in);
        Trace t;
        t.player = playerHash();
        t.world = worldHash();
        t.pos = P.pos; t.vel = P.vel; t.yaw = P.yaw; t.state = P.state; t.score = P.score;
        if (!npcs.empty()) t.npc0 = npcs[0].pos;
        if (!pigeons.empty()) t.pigeon0 = pigeons[0].pos;
        if (!cars.empty()) t.car0 = cars[0].x;
        t.timer = tlTimer;
        trace.push_back(t);
        if (f == 0) {
            liveNpcs0.clear(); livePigeons0.clear(); liveCars0.clear();
            for (auto& n : npcs) liveNpcs0.push_back(n.pos);
            for (auto& p : pigeons) livePigeons0.push_back(p.pos);
            for (auto& c : cars) liveCars0.push_back(c.x);
        }
    }
    rec.ticks = (uint32_t)rec.input.size();
    rec.seed = 2024u;
    rec.worldSystems = true;
    rec.hash = worldHash();
    const uint32_t liveHash = rec.hash;
    const long long liveScore = P.score;

    CHECK(rec.ticks == (uint32_t)(15 * 60));
    resetWorld();

    Sim replaySim;
    int firstMismatch = -1;
    for (uint32_t i = 0; i < rec.ticks; ++i) {
        replaySim.tick(Replay::unpack(rec.input[i]));
        if (worldHash() != trace[i].world && firstMismatch < 0) {
            firstMismatch = (int)i;
            const Trace& t = trace[i];
            printf("    [replay mismatch] frame=%u mask=%04x player=%08x/%08x world=%08x/%08x\n",
                   i, rec.input[i], t.player, playerHash(), t.world, worldHash());
            printf("      live  pos=(%.6f %.6f %.6f) vel=(%.6f %.6f %.6f) yaw=%.6f state=%d score=%lld car0=%.6f timer=%.6f\n",
                   t.pos.x,t.pos.y,t.pos.z,t.vel.x,t.vel.y,t.vel.z,t.yaw,t.state,t.score,t.car0,t.timer);
            printf("      replay pos=(%.6f %.6f %.6f) vel=(%.6f %.6f %.6f) yaw=%.6f state=%d score=%lld car0=%.6f timer=%.6f\n",
                   P.pos.x,P.pos.y,P.pos.z,P.vel.x,P.vel.y,P.vel.z,P.yaw,P.state,P.score,
                   cars.empty()?0.f:cars[0].x,tlTimer);
            printf("      npc0 live=(%.6f %.6f %.6f) replay=(%.6f %.6f %.6f)\n",
                   t.npc0.x,t.npc0.y,t.npc0.z,
                   npcs.empty()?0.f:npcs[0].pos.x,npcs.empty()?0.f:npcs[0].pos.y,npcs.empty()?0.f:npcs[0].pos.z);
            printf("      pigeon0 live=(%.6f %.6f %.6f) replay=(%.6f %.6f %.6f)\n",
                   t.pigeon0.x,t.pigeon0.y,t.pigeon0.z,
                   pigeons.empty()?0.f:pigeons[0].pos.x,pigeons.empty()?0.f:pigeons[0].pos.y,pigeons.empty()?0.f:pigeons[0].pos.z);
            if (i == 0) {
                for (size_t k = 0; k < std::min(liveNpcs0.size(), npcs.size()); ++k)
                    if (memcmp(&liveNpcs0[k], &npcs[k].pos, sizeof(V3)) != 0) {
                        printf("      first npc diff #%zu live=(%.9f %.9f %.9f) replay=(%.9f %.9f %.9f)\n",
                               k, liveNpcs0[k].x,liveNpcs0[k].y,liveNpcs0[k].z,
                               npcs[k].pos.x,npcs[k].pos.y,npcs[k].pos.z); break;
                    }
                for (size_t k = 0; k < std::min(livePigeons0.size(), pigeons.size()); ++k)
                    if (memcmp(&livePigeons0[k], &pigeons[k].pos, sizeof(V3)) != 0) {
                        printf("      first pigeon diff #%zu live=(%.9f %.9f %.9f) replay=(%.9f %.9f %.9f)\n",
                               k, livePigeons0[k].x,livePigeons0[k].y,livePigeons0[k].z,
                               pigeons[k].pos.x,pigeons[k].pos.y,pigeons[k].pos.z); break;
                    }
                for (size_t k = 0; k < std::min(liveCars0.size(), cars.size()); ++k)
                    if (memcmp(&liveCars0[k], &cars[k].x, sizeof(float)) != 0) {
                        printf("      first car diff #%zu live=%.9f replay=%.9f\n", k, liveCars0[k], cars[k].x); break;
                    }
            }
        }
    }
    uint32_t replayHash = worldHash();
    printf("    [replay] live %08x, replayed %08x (%u ticks), first mismatch %d\n",
           liveHash, replayHash, rec.ticks, firstMismatch);
    CHECK(replayHash == liveHash);
    CHECK(P.score == liveScore);
}

TEST(replay, round_trips_through_a_file) {
    initGame();
    resetWorld();
    Replay rec;
    runMonkey(5, 31337u, &rec, true);
    std::string path = "/tmp/oskate_harness_replay.bin";
    CHECK(rec.save(path));
    Replay back;
    CHECK(back.load(path));
    CHECK(back.ticks == rec.ticks);
    CHECK(back.hash == rec.hash);
    CHECK(back.seed == rec.seed);
    CHECK(back.input.size() == rec.input.size());
    bool same = true;
    for (size_t i = 0; i < rec.input.size(); i++) same = same && (rec.input[i] == back.input[i]);
    CHECK(same);
    resetWorld();
    CHECK(playReplay(back) == rec.hash);
    remove(path.c_str());
    // a corrupt / missing file must be reported, not crash
    Replay missing;
    CHECK(!missing.load("/tmp/oskate_harness_no_such_file.bin"));
}

TEST(replay, changed_input_changes_the_outcome) {
    initGame();
    resetWorld();
    Replay rec;
    runMonkey(10, 5150u, &rec, true);
    uint32_t original = rec.hash;
    Replay tampered = rec;
    tampered.input[tampered.ticks / 2] ^= 1u << 9;      // flip the kickflip edge
    resetWorld();
    uint32_t changed = playReplay(tampered);
    printf("    [replay] tampered hash %08x vs %08x\n", original, changed);
    CHECK(changed != original);
}
TEST(replay, long_run_stays_deterministic) {
    initGame();
    uint32_t h[2];
    for (int k = 0; k < 2; k++) {
        resetWorld();
        runMonkey(60, 123456u, nullptr, true);
        h[k] = worldHash();
    }
    printf("    [determinism] 60 s random run hash %08x vs %08x\n", h[0], h[1]);
    CHECK(h[0] == h[1]);
}

TEST(replay, fuzzing_never_produces_invalid_state) {
    initGame();
    int runs = 0, bad = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        resetWorld();
        MonkeyStats st = runMonkey(5, seed * 101u, nullptr, true);
        runs++;
        if (!st.finite) bad++;
        CHECK(std::isfinite(P.pos.x) && std::isfinite(P.pos.y) && std::isfinite(P.pos.z));
        CHECK(P.pos.y > -35.f);
        CHECK(P.score >= 0);
        CHECK(P.combo.base >= 0.f);
        CHECK(P.combo.mult >= 0);
        CHECK(P.bailT >= 0.f);
        CHECK(std::fabs(P.bal) < 8.f);
        if (worstFailures() > 3) break;
    }
    printf("    [fuzz] %d runs, %d non-finite\n", runs, bad);
    CHECK(bad == 0);
}
TEST(replay, state_is_not_corrupted_by_a_bail_then_respawn_loop) {
    initGame();
    resetWorld();
    // force repeated, instant bails
    for (int i = 0; i < 40; i++) {
        P.bail("TEST LOOP");
        Input in;
        tickPlayer(in, 240);                 // let it respawn
        CHECK(P.state == ST_RIDE);
        CHECK(std::isfinite(P.pos.y));
        CHECK(!world.pointBlocked(P.pos + V3(0, 0.5f, 0), false));
        if (worstFailures() > 3) return;
    }
}

} // namespace hns
