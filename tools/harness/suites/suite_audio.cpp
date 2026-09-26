// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_audio -- the game synthesises every sound at startup and mixes the
// loops live. The harness can generate the buffers and run the mixer without an
// audio device, so sample-level regressions are catchable in CI.

// Run `frames` of the audio mixer exactly like the SDL callback would, with the
// live parameters taken from `params`. Returns the peak absolute sample.
static float renderAudio(int frames, const AudioParams& params, bool withMusic, std::vector<float>* out = nullptr) {
    for (auto& v : voices) { v.id = -1; v.pos = 0; }   // each render starts from silence
    aud = params;
    musicOn = withMusic;
    publishAudio(aud, musicOn);
    std::vector<float> buf((size_t)frames * 2, 0.f);
    float peak = 0.f;
    for (int chunk = 0; chunk < frames; chunk += 1024) {
        int n = std::min(1024, frames - chunk);
        audioCallback(nullptr, (Uint8*)(buf.data() + (size_t)chunk * 2), n * 2 * (int)sizeof(float));
        for (int i = 0; i < n * 2; i++) peak = std::max(peak, std::fabs(buf[(size_t)chunk * 2 + i]));
    }
    if (out) *out = buf;
    return peak;
}

// Same, but skip a settling window first: the live parameters are smoothed with
// a ~30 ms time constant, so a jump from one set of values to another is audible
// for the first few blocks. Steady-state levels are what the tests care about.
static float renderSteady(int frames, const AudioParams& params, bool withMusic) {
    renderAudio(44100 / 2, params, withMusic);      // 0.5 s to settle
    return renderAudio(frames, params, withMusic);
}

TEST(audio, every_sfx_is_generated_and_normalised) {
    initGame();
    static const float target[SFX_COUNT] = {0.9f, 0.85f, 0.95f, 0.7f, 0.7f, 0.95f, 0.9f, 0.6f, 0.65f, 0.6f, 0.6f, 0.4f, 0.6f, 0.7f, 0.8f, 0.5f, 0.55f};
    int hardAttack = 0;
    for (int id = 0; id < SFX_COUNT; id++) {
        CHECKM(!sfxBuf[id].empty(), "sfx buffer " + std::to_string(id) + " generated");
        float pk = 0, dc = 0;
        for (float v : sfxBuf[id]) {
            CHECK(std::isfinite(v));
            pk = std::max(pk, std::fabs(v));
            dc += v;
            if (worstFailures() > 3) return;
        }
        dc /= std::max<size_t>(1, sfxBuf[id].size());
        CHECKM(NEAR(pk, target[id], target[id] * 0.05f + 0.01f), "sfx " + std::to_string(id) + " peak matches the normalisation target");
        CHECKM(std::fabs(dc) < 0.02f, "sfx " + std::to_string(id) + " has no DC offset");
        // the tail must fade to silence or the loop clicks
        CHECK(NEAR(sfxBuf[id].back(), 0.f, 0.01f));
        // percussive one-shots (pop, clack, bail) legitimately start at full
        // level -- that IS the transient -- but nothing may start clipped
        CHECKM(std::fabs(sfxBuf[id][0]) <= 0.6f,
               "sfx " + std::to_string(id) + " does not start at full scale");
        if (std::fabs(sfxBuf[id][0]) > 0.1f) hardAttack++;
    }
    printf("    [audio] %d of %d one-shots start with a hard attack (>0.1 of full scale)\n",
           hardAttack, SFX_COUNT);
}

TEST(audio, sfx_buffers_have_sensible_durations) {
    initGame();
    CHECK(NEAR(sfxBuf[SFX_POP].size() / (float)AR, 0.20f, 0.02f));
    CHECK(NEAR(sfxBuf[SFX_CLACK].size() / (float)AR, 0.08f, 0.01f));
    CHECK(sfxBuf[SFX_BAIL].size() > sfxBuf[SFX_CLACK].size());
    CHECK(sfxBuf[SFX_SPLASH].size() / (float)AR > 1.f);
    long total = 0;
    for (int id = 0; id < SFX_COUNT; id++) total += (long)sfxBuf[id].size();
    printf("    [audio] %.0f MB of one-shot samples (%.0f ms total)\n",
           total * 4 / 1048576.0, total * 1000.0 / AR);
    CHECK(total * 4 < 24 * 1024 * 1024);
}

TEST(audio, voice_allocation_is_bounded) {
    initGame();
    for (auto& v : voices) v.id = -1;
    openFakeAudioDevice();                    // sfx() is a no-op without a device
    for (int i = 0; i < 200; i++) sfx(SFX_CLACK, 1.f, 1.f);
    int used = 0, inRange = 0;
    for (auto& v : voices) {
        if (v.id >= 0) used++;
        if (v.id >= 0 && v.id < SFX_COUNT) inRange++;
    }
    printf("    [voices] %d/32 voices allocated by 200 triggers, %d valid ids\n", used, inRange);
    CHECK(used == 32);                       // voices saturate, never overflow
    CHECK(inRange == used);
    // the mixer advances the voices and frees them when the sample ends
    std::vector<float> buf(2048 * 2, 0.f);
    for (int i = 0; i < 40; i++) {
        audioCallback(nullptr, (Uint8*)buf.data(), (int)(buf.size() * sizeof(float)));
        for (float v : buf) CHECK(std::isfinite(v));
    }
    int alive = 0;
    for (auto& v : voices) if (v.id >= 0) alive++;
    printf("    [voices] %d still playing after 40 blocks (clack is %.0f ms)\n",
           alive, sfxBuf[SFX_CLACK].size() * 1000.0 / AR);
    CHECK(alive < 32);
    closeFakeAudioDevice();
}
TEST(audio, mixer_output_is_finite_and_bounded) {
    initGame();
    AudioParams p;
    p.roll = 0.6f; p.rollPitch = 1.2f; p.rollSurf = (float)SURF_WOOD;
    p.grind = 0.8f; p.grindMetal = 1.f; p.wind = 0.4f; p.water = 0.5f;
    float peak = renderSteady(44100, p, true);
    printf("    [mix] peak %.3f with everything running\n", peak);
    CHECK(peak > 0.01f);
    CHECK(peak <= 1.02f);                    // tanh limiter keeps it inside [-1,1]
    // no skater, no music: only the distant traffic bed
    AudioParams silence;
    float quiet = renderSteady(44100, silence, false);
    printf("    [mix] peak %.4f with nothing happening\n", quiet);
    CHECK(quiet < 0.05f);
    // music alone is a steady level (measured ~0.35): loud, never clipping
    float music = renderSteady(44100, silence, true);
    printf("    [mix] peak %.3f with music only\n", music);
    CHECK(music > 0.05f && music <= 0.6f);
    // and the bed must not be so loud that it buries the music
    CHECK(quiet < music * 0.25f);
}

TEST(audio, mixer_survives_extreme_parameters) {
    initGame();
    AudioParams p;
    p.roll = 12.f; p.grind = -5.f; p.wind = 99.f; p.water = 3.f; p.rollPitch = -4.f; p.grindMetal = 2.f;
    float peak = renderAudio(12000, p, true);
    printf("    [mix] peak %.3f with out-of-range parameters\n", peak);
    CHECK(std::isfinite(peak) && peak <= 1.02f);
    // parameters must not be able to push the mixer into NaN
    AudioParams nan;
    nan.roll = NAN;
    float peak2 = renderAudio(2048, nan, false);
    (void)peak2;
    CHECK(std::isfinite(peak2));
}

TEST(audio, live_parameters_follow_the_player) {
    initGame();
    resetWorld();
    Input in;
    in.up = true;
    tickPlayer(in, 120 * 3);
    P.state = ST_RIDE;
    P.update(in, 0.f);                        // refresh the audio params
    printf("    [live] rolling at %.1f m/s -> roll %.2f, pitch %.2f, wind %.2f\n",
           len(P.vel), aud.roll, aud.rollPitch, aud.wind);
    CHECK(aud.roll > 0.1f);
    CHECK(aud.rollPitch > 0.6f);
    CHECK(aud.grind == 0.f);
    // genuinely airborne: raised off the ground and moving up, so the tick
    // cannot land the skater again
    P.pos.y = world.ground(P.pos.x, P.pos.z, P.pos.y + 0.1f).h + 2.0f;
    P.vel = V3(0, 3.f, 12.f);
    P.state = ST_AIR;
    P.update(in, 1.f / 120.f);
    printf("    [live] airborne at y=%.2f (state %d) -> roll %.2f, wind %.2f\n", P.pos.y, P.state, aud.roll, aud.wind);
    CHECK(P.state == ST_AIR);
    CHECK(aud.roll == 0.f);                   // wheels are off the ground
    CHECK(aud.wind > 0.f);
    // and rolling again after landing: let the real physics land the skater
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 600) tickPlayer(in, 1);
    CHECK(P.state == ST_RIDE || P.state == ST_MANUAL);
    if (P.state == ST_RIDE || P.state == ST_MANUAL) {
        tickPlayer(in, 3);
        printf("    [live] landed at %.1f m/s -> roll %.2f\n", len(P.vel), aud.roll);
        CHECK(aud.roll > 0.f);
    }
}

// Exercise the published audio snapshot under concurrent writes. TSan should stay clean.
TEST(audio, concurrent_parameter_writes_never_break_the_mixer) {
    initGame();
    std::atomic<bool> stop{false};
    std::atomic<int> writes{0};
    std::thread writer([&] {
        Rng r(5);
        while (!stop.load()) {
            AudioParams next;
            next.roll = r.range(0.f, 1.f);
            next.grind = r.range(0.f, 1.f);
            next.wind = r.range(0.f, 1.f);
            next.water = r.range(0.f, 1.f);
            next.grindMetal = r.chance(0.5f) ? 1.f : 0.f;
            next.rollPitch = r.range(0.5f, 1.5f);
            publishAudio(next, r.chance(0.5f));
            writes++;
        }
    });
    float peak = 0.f;
    for (int i = 0; i < 40; i++) {
        std::vector<float> buf(2048 * 2, 0.f);
        audioCallback(nullptr, (Uint8*)buf.data(), (int)buf.size() * (int)sizeof(float));
        for (float v : buf) { CHECK(std::isfinite(v)); peak = std::max(peak, std::fabs(v)); }
        if (worstFailures() > 3) break;
    }
    stop = true;
    writer.join();
    printf("    [race] %d parameter writes during 40 blocks, peak %.3f, all finite\n", writes.load(), peak);
    CHECK(writes.load() > 100);
    CHECK(peak <= 1.02f);
    musicOn = true;
}

TEST(audio, sfx_triggering_is_safe_without_a_device) {
    initGame();
    // audioDev is 0 in the harness: sfx() must simply do nothing
    int before = 0;
    for (auto& v : voices) if (v.id >= 0) before++;
    sfx(SFX_POP);
    sfx(SFX_BAIL, 2.f, -1.f);
    sfx(9999);                                // out of range id
    sfx(-3);
    int after = 0;
    for (auto& v : voices) if (v.id >= 0) after++;
    CHECK(after == before);
}
} // namespace hns
