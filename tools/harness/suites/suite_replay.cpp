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
    Replay rec;
    MonkeyStats live = runMonkey(15, 2024u, &rec, true);
    uint32_t liveHash = rec.hash;
    CHECK(rec.ticks == (uint32_t)(15 * 60));
    resetWorld();
    uint32_t replayHash = playReplay(rec);
    printf("    [replay] live %08x, replayed %08x (%u ticks)\n", liveHash, replayHash, rec.ticks);
    CHECK(replayHash == liveHash);
    CHECK(P.score == live.score);
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
