// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
namespace hns {
TEST(math, wrap_pi_stays_in_range) {
    Rng r(7);
    for (int i = 0; i < 20000; i++) {
        float a = r.range(-1000.f, 1000.f);
        float w = wrapPi(a);
        CHECK(w > -PI - 1e-4f && w < PI + 1e-4f);
        // wrapping must not change the angle
        CHECK(NEAR(std::cos(w), std::cos(a), 1e-3f));
        CHECK(NEAR(std::sin(w), std::sin(a), 1e-3f));
        if (worstFailures() > 3) break;              // do not spam 20k failures
    }
}

TEST(math, damp_is_framerate_independent) {
    const float rate = 4.f, dt = 1.f / 60.f;
    float cur = 100.f;
    for (int i = 0; i < 60; i++) cur = damp(cur, 0.f, rate, dt);
    CHECK(NEAR(cur, 100.f * std::exp(-rate * 60 * dt), 1e-2));   // exact closed form
    float big = damp(100.f, 0.f, rate, 1.f);
    CHECK(NEAR(big, 100.f * std::exp(-rate), 1e-3));             // one big step == many small
    CHECK(NEAR(damp(1.f, 5.f, 0.f, 0.016f), 1.f, 1e-6));         // rate 0 = frozen
    V3 dv = damp3(V3(1, 2, 3), V3(0, 0, 0), rate, dt);
    CHECK(dv.x < 1 && dv.y < 2 && dv.z < 3);
}

TEST(math, matrix_conventions) {
    // mRotY(+90) maps +Z onto +X -- the convention fwdYaw() relies on
    V3 z(0, 0, 1);
    V3 r = xDir(mRotY(PI / 2), z);
    CHECK(NEAR(r.x, 1, 1e-5) && NEAR(r.z, 0, 1e-5));
    CHECK(NEAR(fwdYaw(0).z, 1, 1e-6));
    CHECK(NEAR(fwdYaw(PI / 2).x, 1, 1e-6));
    // translation is applied to points, not to directions
    M4 t = mTranslate(V3(5, 6, 7));
    V3 p = xPoint(t, z);
    CHECK(NEAR(p.x, 5, 1e-5) && NEAR(p.y, 6, 1e-5) && NEAR(p.z, 8, 1e-5));
    V3 d = xDir(t, z);
    CHECK(NEAR(d.z, 1, 1e-5) && NEAR(len(d), 1, 1e-5));
    // rotation about Y leaves its own axis alone
    V3 y = xDir(mRotY(1.23f), V3(0, 1, 0));
    CHECK(NEAR(y.y, 1, 1e-5));
    CHECK(NEAR(len(xDir(mRotAxis(norm(V3(1, 2, 3)), 0.7f), V3(1, 0, 0))), 1, 1e-4));
}

TEST(math, matrix_inverse) {
    Rng r(99);
    for (int i = 0; i < 200; i++) {
        M4 m = mTranslate(V3(r.range(-50, 50), r.range(-10, 40), r.range(-50, 50))) *
               mRotY(r.range(-PI, PI)) * mRotX(r.range(-1.f, 1.f)) *
               mScale(V3(r.range(0.2f, 3.f), r.range(0.2f, 3.f), r.range(0.2f, 3.f)));
        M4 id = m * mInverse(m);
        for (int k = 0; k < 16; k++) {
            float want = (k % 5 == 0) ? 1.f : 0.f;
            CHECK(NEAR(id.m[k], want, 1e-3));
            if (worstFailures() > 3) return;
        }
    }
}

TEST(math, yaw_roundtrip) {
    for (float y = -3.1f; y <= 3.1f; y += 0.017f) {
        CHECK(NEAR(wrapPi(yawOf(fwdYaw(y)) - y), 0, 1e-4));
        if (worstFailures() > 3) return;
    }
    // perpendicular directions differ by 90 degrees
    CHECK(NEAR(std::fabs(wrapPi(yawOf(V3(1, 0, 0)) - yawOf(V3(0, 0, 1)))), PI / 2, 1e-5));
}

TEST(math, rng_is_deterministic) {
    Rng a(1234), b(1234), c(4321);
    bool same = true, differ = false;
    for (int i = 0; i < 1000; i++) {
        uint32_t x = a.next(), y = b.next(), z = c.next();
        same = same && (x == y);
        differ = differ || (x != z);
    }
    CHECK(same);
    CHECK(differ);
    Rng d(5);
    float mn = 2, mx = -1;
    for (int i = 0; i < 100000; i++) { float f = d.f(); mn = std::min(mn, f); mx = std::max(mx, f); }
    CHECK(mn >= 0.f && mx < 1.f);
    CHECK(mx > 0.99f && mn < 0.01f);                 // full range is actually used
    Rng e(5);
    for (int i = 0; i < 20000; i++) {
        int v = e.irange(3, 7);
        CHECK(v >= 3 && v <= 7);
        if (worstFailures() > 3) break;
    }
    CHECK(e.chance(0.f) == false);
    CHECK(e.chance(1.f) == true);
}
TEST(math, projection_and_screen_space) {
    M4 proj = mPerspective(60.f * PI / 180.f, 16.f / 9.f, 0.1f, 1000.f);
    M4 view = mLookAt(V3(0, 2, 10), V3(0, 2, 0), V3(0, 1, 0));
    M4 vp = proj * view;
    hud.begin(1920, 1080);
    float sx = 0, sy = 0;
    CHECK(projectToScreen(vp, V3(0, 2, 0), sx, sy));         // dead centre
    CHECK(NEAR(sx, 960, 5) && NEAR(sy, 540, 5));
    CHECK(projectToScreen(vp, V3(3, 2, 0), sx, sy));         // to the right
    CHECK(sx > 1000);
    CHECK(!projectToScreen(vp, V3(0, 2, 20), sx, sy));       // behind the camera
}

} // namespace hns
