namespace hns {

// Focused regressions adapted from intra-secdsm/OpusSkate commit 62815a0.

TEST(runtime, transition_ollie_preserves_vert_air) {
    Player steep;
    steep.n = V3(0, 0.5f, 0.866f);
    steep.ollie(0.5f, false);
    CHECK(steep.qpAir);

    Player flat;
    flat.ollie(0.5f, false);
    CHECK(!flat.qpAir);

    Player rail;
    rail.n = V3(0, 0.5f, 0.866f);
    rail.ollie(0.5f, true);
    CHECK(!rail.qpAir);
}

TEST(runtime, queued_press_survives_until_a_physics_tick) {
    for (int fps : {60, 120, 144, 240}) {
        Input pending;
        double acc = 0;
        int delivered = 0;
        for (int frame = 0; frame < 8; ++frame) {
            Input in;
            if (frame == 0) in.flipPress = true;
            queuePresses(pending, in, true);
            acc += 1.0 / fps;
            while (acc >= 1.0 / 120.0) {
                Input tick = in;
                takePresses(pending, tick);
                delivered += tick.flipPress ? 1 : 0;
                acc -= 1.0 / 120.0;
            }
        }
        CHECK(delivered == 1);
    }
}

TEST(runtime, leaving_play_clears_pending_presses) {
    Input pending, press, empty, tick;
    press.olliePress = true;
    queuePresses(pending, press, true);
    queuePresses(pending, empty, false);
    takePresses(pending, tick);
    CHECK(!tick.olliePress);
}

TEST(runtime, screenshot_writer_reports_failures) {
    const std::vector<uint8_t> pixels = {255, 0, 0, 0, 255, 0};
    CHECK(!writePpm("/tmp/opus-skate-missing-dir/shot.ppm", 2, 1, pixels));
    const char* shot = "/tmp/opus-skate-regression.ppm";
    CHECK(writePpm(shot, 2, 1, pixels));
    FILE* f = fopen(shot, "rb");
    CHECK(f != nullptr);
    if (f) fclose(f);
    std::remove(shot);
}

} // namespace hns
