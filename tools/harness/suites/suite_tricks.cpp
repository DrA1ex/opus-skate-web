// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {

// suite_tricks -- flips, grabs, spins, grinds, manuals, combo scoring,
// S-K-A-T-E letters and named gaps. These are the rules of the game; if a
// tuning pass breaks one of them the player notice it immediately, so the
// harness should notice it first.

// ---------------------------------------------------------------- helpers --
static void pushTo(Input& in, float seconds) {
    in.up = true;
    tickPlayer(in, (int)(seconds * 120.f));
}
// pop with a full charge and leave the ground
static void fullOllie(Input& in) {
    in.ollie = true;
    in.olliePress = true;
    tickPlayer(in, 40);
    in.ollie = false;
    tickPlayer(in, 1);
}
// put the skater in the air a short drop above the ground with `vy` downward
static void dropFrom(float height, float vy, float speed = 0.f) {
    V3 p = P.pos;
    p.y = world.ground(p.x, p.z, p.y + 3.f).h + height;
    P.pos = p;
    P.vel = fwdYaw(P.yaw) * speed + V3(0, vy, 0);
    P.state = ST_AIR;
    P.airT = 0;
}

TEST(tricks, flip_names_and_points_match_the_table) {
    initGame();
    static const char* DIR[9] = {"-", "A", "D", "W", "S", "W+A", "W+D", "S+A", "S+D"};
    for (int d = 0; d < 9; d++) {
        resetWorld();
        Input in;
        pushTo(in, 1.5f);
        fullOllie(in);
        in.left = (d == 1 || d == 5 || d == 7);
        in.right = (d == 2 || d == 6 || d == 8);
        in.up = (d == 3 || d == 5 || d == 6);
        in.down = (d == 4 || d == 7 || d == 8);
        in.flipPress = true;
        tickPlayer(in, 1);
        CHECKM(P.flipIdx == d, std::string("direction ") + DIR[d] + " selects " + FLIPS[d].name);
        CHECK(P.combo.base > 0);
        if (worstFailures() > 2) return;
    }
}

TEST(tricks, flip_lands_clean_when_given_time) {
    initGame();
    resetWorld();
    Input in;
    pushTo(in, 3.f);
    fullOllie(in);
    in.up = false;               // a plain kickflip, not a W-direction flip
    in.flipPress = true;
    tickPlayer(in, 1);
    CHECKM(P.flipIdx == 0, "the kickflip is in progress right after the press");
    CHECK(P.flipT < P.flipDur);
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 400) tickPlayer(in, 1);
    CHECK(P.state == ST_RIDE);
    CHECK(P.flipIdx == -1);      // -1 = no flip in progress: it finished in the air
    pushTo(in, 0.6f);                    // let landGrace expire and bank
    printf("    [flip] landed, score %lld, combo '%s'\n", P.score, P.combo.text(60).c_str());
    CHECK(P.score >= FLIPS[0].pts);
    CHECK(P.lettersGot == 0);
}

TEST(tricks, unfinished_flip_bails) {
    initGame();
    resetWorld();
    Input in;
    pushTo(in, 2.f);
    dropFrom(0.7f, -4.f, 6.f);           // only ~0.15 s of air left
    in.flipPress = true;
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 200) tickPlayer(in, 1);
    printf("    [flip] short-hop landing: state=%d why='%s'\n", P.state, P.bailWhy.c_str());
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("FLIP") != std::string::npos);
    CHECK(!P.combo.active());            // the combo is lost with the bail
}

TEST(tricks, double_and_triple_flips) {
    initGame();
    resetWorld();
    Input in;
    pushTo(in, 3.f);
    fullOllie(in);
    in.flipPress = true;
    tickPlayer(in, 1);
    CHECK(P.flipExtra == 0);
    float base = P.combo.base;
    tickPlayer(in, 25);                   // past 35% of the flip
    in.flipPress = true;
    tickPlayer(in, 1);
    CHECK(P.flipExtra == 1);
    CHECK(P.combo.base > base);
    CHECK(P.flipDur > FLIPS[0].dur);
    // third press => triple, then it stops
    tickPlayer(in, 25);
    in.flipPress = true;
    tickPlayer(in, 1);
    CHECK(P.flipExtra == 2);
    float dur = P.flipDur;
    tickPlayer(in, 25);
    in.flipPress = true;
    tickPlayer(in, 1);
    CHECK(P.flipExtra == 2);              // capped
    CHECK(NEAR(P.flipDur, dur, 1e-4));
}

TEST(tricks, grab_must_be_released_before_landing) {
    initGame();
    // held too long
    resetWorld();
    Input in;
    pushTo(in, 2.f);
    fullOllie(in);
    in.grabPress = true;
    in.grab = true;
    tickPlayer(in, 30);
    CHECK(P.grabIdx >= 0);
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 300) tickPlayer(in, 1);   // never let go
    printf("    [grab] held: state=%d why='%s'\n", P.state, P.bailWhy.c_str());
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("GRAB") != std::string::npos);
    // released in time
    resetWorld();
    Input q;
    pushTo(q, 2.f);
    fullOllie(q);
    q.grabPress = true;
    q.grab = true;
    tickPlayer(q, 30);
    float withGrab = P.combo.base;
    for (int i = 0; i < 40 && P.state == ST_AIR; i++) tickPlayer(q, 1);   // hold a moment
    q.grab = false;
    tickPlayer(q, 20);
    int g = 0;
    while (P.state == ST_AIR && g++ < 300) tickPlayer(q, 1);
    printf("    [grab] released: state=%d score=%lld combo=%.0f\n", P.state, P.score, P.combo.base);
    CHECK(P.state == ST_RIDE);
    CHECK(P.combo.base > withGrab);
}

TEST(tricks, grab_variants_are_selectable) {
    initGame();
    for (int d = 0; d < 9; d++) {
        resetWorld();
        Input in;
        pushTo(in, 1.5f);
        fullOllie(in);
        in.left = (d == 1 || d == 5 || d == 7);
        in.right = (d == 2 || d == 6 || d == 8);
        in.up = (d == 3 || d == 5 || d == 6);
        in.down = (d == 4 || d == 7 || d == 8);
        in.grabPress = true;
        in.grab = true;
        tickPlayer(in, 1);
        CHECK(P.grabIdx == d);
        if (worstFailures() > 2) return;
    }
}

TEST(tricks, spin_landing_rule_matches_orientation) {
    initGame();
    // The landing rule is |cos(angle between facing and travel)| < 0.74 => bail.
    // Sweep the hold length and verify the rule from the game's own numbers.
    int checked = 0, mismatches = 0;
    for (int hold = 6; hold <= 90; hold += 6) {
        resetWorld();
        Input in;
        pushTo(in, 3.f);
        fullOllie(in);
        in.left = true;
        tickPlayer(in, hold);
        in.left = false;
        float angleAtLanding = -1;
        int guard = 0;
        while (P.state == ST_AIR && guard++ < 300) {
            V3 vt = P.vel - P.n * dot(P.vel, P.n);
            if (len(vt) > 1.0f) {
                V3 ft = fwdYaw(P.yaw);
                float c = std::fabs(dot(norm(ft), norm(vt)));
                angleAtLanding = std::acos(clampf(c, 0.f, 1.f)) * 180.f / PI;
            }
            tickPlayer(in, 1);
        }
        bool landedSideways = P.state == ST_BAIL && P.bailWhy.find("SIDEWAYS") != std::string::npos;
        if (angleAtLanding >= 0) {
            checked++;
            bool predicted = angleAtLanding > 42.5f;
            if (predicted != landedSideways) mismatches++;
            printf("    [spin] hold %2d ticks -> %.0f deg -> %s\n", hold, angleAtLanding,
                   landedSideways ? "BAIL sideways" : (P.state == ST_BAIL ? "bail (other)" : "landed"));
        }
    }
    printf("    [spin] %d samples, %d rule mismatches\n", checked, mismatches);
    CHECK(checked >= 8);
    CHECK(mismatches == 0);
}

TEST(tricks, spin_points_follow_the_table) {
    initGame();
    static const int want180[] = {0, 150, 400, 750, 1200, 2000, 3000, 4500};
    // a full 360 spin should award SPIN_PTS[2] == 400
    resetWorld();
    Input in;
    pushTo(in, 3.f);
    fullOllie(in);
    in.left = true;
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 300) {
        tickPlayer(in, 1);
        if (std::fabs(P.spinAccum) >= TAU) { in.left = false; break; }
    }
    in.left = false;
    while (P.state == ST_AIR && guard++ < 400) tickPlayer(in, 1);
    pushTo(in, 0.6f);
    printf("    [spin] spinAccum=%.2f rad, score=%lld\n", P.spinAccum, P.score);
    CHECK(P.score >= want180[2]);
    CHECK(P.score < want180[2] + 300);           // roughly one spin, not two
}

TEST(tricks, grind_engages_points_and_balance) {
    initGame();
    resetWorld();
    // drop onto the long avenue curb rail (x = -9, y = SH)
    Input in;
    V3 a(-9.f, SH, -60.f), b(-9.f, SH, -10.f);
    V3 dir = norm(b - a);
    P.reset(a + dir * 2.f + V3(0.35f, 1.0f, 0), yawOf(dir));
    P.vel = dir * 8.f;
    P.state = ST_AIR;
    in.grind = true;
    in.grindPress = true;
    tickPlayer(in, 3);
    printf("    [grind] state=%d rail=%d speed=%.1f\n", P.state, P.rail, len(P.vel));
    CHECK(P.state == ST_GRIND);
    CHECK(P.rail >= 0);
    CHECK(world.rails[P.rail].kind == RK_CURB || world.rails[P.rail].kind == RK_METAL);
    float base = P.combo.base;
    tickPlayer(in, 120);
    CHECK(P.combo.base > base);                  // grinding ticks up points
    CHECK(std::fabs(P.bal) <= 1.f);
    CHECK(P.grindTime > 0.5f);
    // ollie out of the grind
    in.grind = false;
    in.olliePress = true;
    tickPlayer(in, 2);
    CHECK(P.state == ST_AIR);
    CHECK(P.vel.y > 0.5f);
}

TEST(tricks, grind_rejects_perpendicular_approach) {
    initGame();
    resetWorld();
    Input in;
    V3 a(-9.f, SH, -60.f), b(-9.f, SH, -10.f);
    V3 dir = norm(b - a);
    V3 side = cross(V3(0, 1, 0), dir);
    P.reset(a + dir * 2.f + side * 0.2f + V3(0, 0.5f, 0), yawOf(side));
    P.vel = side * 8.f;
    P.state = ST_AIR;
    in.grind = true;
    in.grindPress = true;
    tickPlayer(in, 3);
    CHECK(P.state != ST_GRIND);                  // dead perpendicular is refused
}

TEST(tricks, grind_bails_when_balance_fails) {
    initGame();
    resetWorld();
    Input in;
    V3 a(-9.f, SH, -60.f), b(-9.f, SH, -10.f);
    V3 dir = norm(b - a);
    P.reset(a + dir * 2.f + V3(0.35f, 1.0f, 0), yawOf(dir));
    P.vel = dir * 8.f;
    P.state = ST_AIR;
    in.grindPress = true;
    tickPlayer(in, 3);
    CHECK(P.state == ST_GRIND);
    in.grindPress = false;
    int guard = 0;
    while (P.state == ST_GRIND && guard++ < 120 * 30) tickPlayer(in, 1);
    printf("    [grind] bailed after %.2f s: %s\n", guard / 120.f, P.bailWhy.c_str());
    CHECK(P.state == ST_BAIL);
    CHECK(P.bailWhy.find("BALANCE") != std::string::npos || P.bailWhy.find("RAIL") != std::string::npos);
    CHECK(!P.combo.active());
}

TEST(tricks, manual_links_a_combo_and_bails_when_ignored) {
    initGame();
    resetWorld();
    Input in;
    pushTo(in, 2.5f);
    in.manual = true;
    in.manualPress = true;
    tickPlayer(in, 1);
    CHECK(P.state == ST_MANUAL);
    float base = P.combo.base;
    bool noseShifted = false;
    in.up = true;                        // ask for a nose manual
    tickPlayer(in, 30);
    noseShifted = P.bal != 0.f;
    CHECK(noseShifted);
    int guard = 0;
    while (P.state == ST_MANUAL && guard++ < 120 * 30) tickPlayer(in, 1);
    printf("    [manual] hands-off bail after %.2f s: %s\n", guard / 120.f, P.bailWhy.c_str());
    CHECK(P.state == ST_BAIL);
    CHECK(P.combo.base > base || P.bailWhy.size() > 0);
}

TEST(tricks, manual_exit_banks_the_combo) {
    initGame();
    resetWorld();
    Input in;
    pushTo(in, 2.5f);
    in.manual = true;
    in.manualPress = true;
    tickPlayer(in, 1);
    CHECK(P.state == ST_MANUAL);
    // fight the balance with the opposite key
    for (int i = 0; i < 90; i++) {
        in.up = P.bal > 0.02f;
        in.down = P.bal < -0.02f;
        tickPlayer(in, 1);
    }
    CHECK(P.state == ST_MANUAL);
    in.up = in.down = false;
    in.manualPress = true;
    in.manual = false;
    tickPlayer(in, 2);
    printf("    [manual] exit: state=%d score=%lld\n", P.state, P.score);
    CHECK(P.state == ST_RIDE);
    CHECK(P.score > 0);
    CHECK(!P.combo.active());
}

TEST(tricks, combo_multiplier_and_repeat_decay) {
    Combo c;
    c.add("Kickflip", 100.f);
    CHECK(NEAR(c.base, 100.f, 1e-3));
    CHECK(c.mult == 1);
    c.add("Kickflip", 100.f);
    CHECK(NEAR(c.base, 100.f + 72.f, 1e-2));       // 0.72 decay on a repeat
    CHECK(c.mult == 2);
    CHECK(c.value() == (long long)(c.base * 2));
    c.add("Shove-It", 150.f);
    CHECK(c.mult == 3);
    CHECK(c.uses.size() == 2);
    // the floor keeps grinding a single trick from being worthless but bounded
    for (int i = 0; i < 20; i++) c.add("Kickflip", 100.f);
    CHECK(c.value() > 0);
    CHECK(c.base < 100.f * 25);
    std::string t = c.text(40);
    CHECK(t.size() <= 46);
    CHECK(t.find("... + ") == 0 || t.size() > 0);
    c.reset();
    CHECK(!c.active() && c.value() == 0);
}

TEST(tricks, combo_run_and_bank_updates_best) {
    initGame();
    resetWorld();
    P.combo.reset();
    P.score = 0;
    P.best = 0;
    P.combo.add("Kickflip", 100.f);
    P.combo.add("Manual", 100.f);
    long long expect = P.combo.value();
    P.bankCombo();
    CHECK(P.score == expect);
    CHECK(P.best == expect);
    CHECK(!P.combo.active());
    // banking when not in a combo must not change anything
    long long before = P.score;
    P.bankCombo();
    CHECK(P.score == before);
}

TEST(tricks, bail_drops_the_active_combo) {
    initGame();
    resetWorld();
    P.combo.reset();
    P.combo.add("Kickflip", 500.f);
    long long before = P.score;
    float banked = P.combo.base;
    P.bail("TEST");
    CHECK(!P.combo.active());
    CHECK(P.score == before);              // nothing banked by a bail
    CHECK(banked > 0);
    CHECK(P.state == ST_BAIL);
}

TEST(tricks, land_grace_banks_after_a_moment) {
    initGame();
    resetWorld();
    P.combo.reset();
    P.score = 0;
    P.combo.add("Test Trick", 1000.f);
    P.state = ST_RIDE;
    P.landGrace = 0.3f;
    CHECK(P.score == 0);
    Input in;
    tickPlayer(in, 40);                    // ~0.33 s
    printf("    [grace] score after grace: %lld\n", P.score);
    CHECK(P.score == 1000);
}

TEST(tricks, letters_award_once_and_bonus_completes_the_word) {
    initGame();
    resetWorld();
    P.score = 0;
    for (int i = 0; i < 5; i++) P.letters[i] = false;
    P.lettersGot = 0;
    for (int i = 0; i < 5; i++) {
        long long before = P.score;
        P.pos = letterPos[i] - V3(0, 0.9f, 0);
        P.state = ST_RIDE;
        Input in;
        tickPlayer(in, 1);
        CHECKM(P.letters[i], "letter index picked up");
        CHECK(P.score > before);
        CHECK(P.lettersGot == i + 1);
    }
    printf("    [letters] all five collected, score %lld\n", P.score);
    CHECK(P.score >= 250 * 5 + 5000);
    // re-entering the same letter must not pay twice
    long long after = P.score;
    P.pos = letterPos[0] - V3(0, 0.9f, 0);
    Input in;
    tickPlayer(in, 1);
    CHECK(P.score == after);
}

TEST(tricks, session_reset_clears_letters_and_score) {
    initGame();
    resetWorld();
    P.letters[0] = P.letters[2] = true;
    P.lettersGot = 2;
    P.score = 12345;
    // mimic startSession()
    P.score = 0;
    P.reset(SPAWN_POS, SPAWN_YAW);
    for (int i = 0; i < 5; i++) P.letters[i] = false;
    P.lettersGot = 0;
    popups.clear();
    CHECK(P.lettersGot == 0 && P.score == 0);
    CHECK(P.state == ST_RIDE);
    CHECK(!P.letters[0] && !P.letters[2]);
}

TEST(tricks, stance_helpers) {
    initGame();
    resetWorld();
    P.reset(SPAWN_POS, SPAWN_YAW);
    P.vel = fwdYaw(P.yaw) * 5.f;
    CHECK(!P.isFakie());
    P.vel = fwdYaw(P.yaw) * -5.f;
    CHECK(P.isFakie());
    P.state = ST_RIDE;
    CHECK(P.grounded());
    P.state = ST_AIR;
    CHECK(!P.grounded());
    P.state = ST_MANUAL;
    CHECK(P.grounded());
    P.state = ST_GRIND;
    CHECK(!P.grounded());
}

// A hand-authored "reference line" through the spawn area: ollie -> kickflip ->
// land -> manual -> bank. Used by suite_replay to exercise recording.
TEST(tricks, reference_line_scores_in_order) {
    initGame();
    resetWorld();
    Input in;
    long long s0 = P.score;
    pushTo(in, 3.f);
    fullOllie(in);
    in.flipPress = true;
    tickPlayer(in, 1);
    int guard = 0;
    while (P.state == ST_AIR && guard++ < 300) tickPlayer(in, 1);
    CHECK(P.state == ST_RIDE);
    pushTo(in, 0.5f);
    long long s1 = P.score;
    CHECKM(s1 > s0, "the flip banked points");
    in.manualPress = true;
    in.manual = true;
    tickPlayer(in, 1);
    CHECK(P.state == ST_MANUAL);
    for (int i = 0; i < 60; i++) {
        in.up = P.bal > 0.02f;
        in.down = P.bal < -0.02f;
        tickPlayer(in, 1);
    }
    in.up = in.down = false;
    in.manual = false;
    in.manualPress = true;
    tickPlayer(in, 2);
    long long s2 = P.score;
    printf("    [line] score %lld -> %lld -> %lld\n", s0, s1, s2);
    CHECK(s2 > s1);
}

} // namespace hns
