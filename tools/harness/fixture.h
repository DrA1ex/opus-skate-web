// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
// ============================================================================
//  fixture.h -- game-facing helpers for the harness (included *after* skate.cpp)
//
//  Everything here drives the real game code: the same buildLevel(), the same
//  120 Hz Player::update(), the same NPC/traffic/particle systems. No rendering,
//  no audio device, no window.
// ============================================================================
#pragma once

namespace hns {

// ------------------------------------------------------------- lifecycle ---
// Pedestrians only get their real position inside their update tick, so run one
// tick with a ghost player far away: tests can then read npcs[i].pos directly.
inline void primeEntities() {
    Player ghost;
    ghost.pos = V3(9999.f, 50.f, 9999.f);
    ghost.state = ST_BAIL;                 // parked far away: startles nothing
    ghost.vel = V3(0, 0, 0);
    long long seen = ghost.score;
    updateNpcs(1.f / 60.f, ghost, seen);
    updatePigeons(1.f / 60.f, ghost);
    updateTraffic(1.f / 60.f, ghost);
}

// The mixer and the voice allocator sit behind "is a device open?". The headless
// harness can flip that flag so those code paths still run.
inline void openFakeAudioDevice() { audioDev = 1; }
inline void closeFakeAudioDevice() { audioDev = 0; }

inline void initGame() {
    static bool done = false;
    if (done) return;
    initAudio(true);      // synthesise every SFX buffer (no device is opened)
    buildLevel();
    initNpcs();
    initPigeons();
    initTraffic();
    done = true;
}

// Put the whole world back into its start-of-run state so tests are isolated:
// entities, timers, one-shots, the player and every RNG stream the game owns.
inline void resetWorld() {
    npcs.clear();
    pigeons.clear();
    cars.clear();
    preparePaths();
    initNpcs();
    initPigeons();
    initTraffic();
    primeEntities();
    tlTimer = 0.f;
    parts.clear();
    popups.clear();
    bubbles.clear();
    for (auto& e : emitters) e.acc = 0.f;
    for (auto& v : voices) { v.id = -1; v.pos = 0; }
    P = Player();
    P.reset(SPAWN_POS, SPAWN_YAW);
    aud = AudioParams();
    publishAudio(aud, true);
    LS = LiveState();
    // Every RNG stream the game owns, back to its start-of-process state, so a
    // run is reproducible no matter what ran before it in this process.
    prng = Rng(2024);            // particles
    npcRng = Rng(4242);          // pedestrian chatter
    pigeonRng = Rng(99);         // pigeon idle / startle jitter
}

// Rebuild the level from scratch. buildLevel() appends to the mesh builders and
// the collision world, so a rebuild has to clear them first -- this is the same
// "load a level" entry point a level editor would need.
inline void rebuildLevel() {
    SM.clear();
    WM.clear();
    world.solids.clear();
    world.rails.clear();
    world.gaps.clear();
    world.pools.clear();
    world.grid.clear();
    emitters.clear();
    tlights.clear();
    lamps.clear();
    npcPaths.clear();
    pigeonSpots.clear();
    letterPos.clear();
    buildLevel();
    npcs.clear();
    pigeons.clear();
    cars.clear();
    preparePaths();
    initNpcs();
    initPigeons();
    initTraffic();
    primeEntities();
}

// ------------------------------------------------------------ stepping -----
// Physics only: exactly what the game does per fixed tick.
inline void tickPlayer(Input& in, int frames, float dt = 1.f / 120.f) {
    for (int i = 0; i < frames; i++) {
        P.prevPos = P.pos;
        P.update(in, dt);
        in.olliePress = in.flipPress = in.grabPress = in.grindPress = in.manualPress = false;
    }
}
inline void tickPlayer(Input& in, float seconds) { tickPlayer(in, (int)(seconds * 120.f + 0.5f)); }

// Full frame like main(): fixed-step physics with the accumulator + world
// systems at frame rate. `world` toggles NPC/pigeon/traffic/particles.
struct Sim {
    float acc = 0;
    long frame = 0;
    long long lastBankSeen = 0;
    bool worldSystems = true;
    void tick(const Input& in, float frameDt = 1.f / 60.f) {
        Input step = in;
        const float fixed = 1.f / 120.f;
        acc += frameDt;
        int n = 0;
        while (acc >= fixed && n < 12) {
            P.prevPos = P.pos;
            P.update(step, fixed);
            step.olliePress = step.flipPress = step.grabPress = step.grindPress = step.manualPress = false;
            acc -= fixed;
            n++;
        }
        if (n == 12) acc = 0;
        if (worldSystems) {
            updateNpcs(frameDt, P, lastBankSeen);
            updatePigeons(frameDt, P);
            updateTraffic(frameDt, P);
            updateParticles(frameDt, P.pos);
        } else {
            updateParticles(frameDt, P.pos);
        }
        frame++;
    }
    void run(const Input& in, float seconds, float frameDt = 1.f / 60.f) {
        int frames = (int)(seconds / frameDt + 0.5f);
        for (int i = 0; i < frames; i++) tick(in, frameDt);
    }
};

// ---------------------------------------------------------------- hashing --
// State hash of everything the player physics can observe directly.
inline uint32_t playerHash() {
    uint32_t h = 2166136261u;
    h = fnv1a(h, &P.pos, sizeof(V3));
    h = fnv1a(h, &P.vel, sizeof(V3));
    h = fnv1a(h, &P.yaw, 4);
    h = fnv1a(h, &P.state, 4);
    h = fnv1a(h, &P.score, 8);
    h = fnv1a(h, &P.combo.base, 4);
    h = fnv1a(h, &P.combo.mult, 4);
    h = fnv1a(h, &P.letters, sizeof(P.letters));
    h = fnv1a(h, &P.bal, 4);
    h = fnv1a(h, &P.rail, 4);
    return h;
}
inline uint32_t worldHash() {
    uint32_t h = playerHash();
    for (auto& n : npcs) h = fnv1a(h, &n.pos, sizeof(V3));
    for (auto& p : pigeons) h = fnv1a(h, &p.pos, sizeof(V3));
    for (auto& c : cars) h = fnv1a(h, &c.x, 4);
    h = fnv1a(h, &tlTimer, 4);
    return h;
}

// ------------------------------------------------------------- monkey fuzz --
// Deterministic input generator, independent from the game's own RNG so a
// recorded run can be replayed exactly.
struct MonkeyRng {
    uint32_t s = 1u;
    explicit MonkeyRng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.f; }
    bool chance(float p) { return f() < p; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};
inline Input monkeyInput(MonkeyRng& r, const Player& pl) {
    Input x;
    bool air = pl.state == ST_AIR;
    x.up = r.chance(0.75f);
    x.down = r.chance(0.05f);
    x.left = r.chance(0.12f);
    x.right = r.chance(0.12f);
    x.ollie = !air && r.chance(0.30f);
    x.olliePress = x.ollie && r.chance(0.08f);
    x.flipPress = air && r.chance(0.06f);
    x.grabPress = air && r.chance(0.03f);
    x.grab = x.grabPress || r.chance(0.05f);
    x.grind = air && r.chance(0.20f);
    x.grindPress = x.grind && r.chance(0.30f);
    x.manualPress = !air && r.chance(0.04f);
    x.manual = x.manualPress;
    return x;
}

// -------------------------------------------------------------- replays ----
// Minimal deterministic input-replay format (prototype for the replay/ghost
// feature recommended in REVIEW.md 6.5). Little-endian, one uint16 per tick.
struct Replay {
    static constexpr uint32_t MAGIC = 0x524B534Fu;   // "OSKR"
    uint32_t ticks = 0;
    uint32_t seed = 0;
    uint32_t hash = 0;
    bool worldSystems = true;
    std::vector<uint16_t> input;

    static uint16_t pack(const Input& in) {
        uint16_t b = 0;
        if (in.up) b |= 1 << 0;
        if (in.down) b |= 1 << 1;
        if (in.left) b |= 1 << 2;
        if (in.right) b |= 1 << 3;
        if (in.ollie) b |= 1 << 4;
        if (in.grab) b |= 1 << 5;
        if (in.grind) b |= 1 << 6;
        if (in.manual) b |= 1 << 7;
        if (in.olliePress) b |= 1 << 8;
        if (in.flipPress) b |= 1 << 9;
        if (in.grabPress) b |= 1 << 10;
        if (in.grindPress) b |= 1 << 11;
        if (in.manualPress) b |= 1 << 12;
        return b;
    }
    static Input unpack(uint16_t b) {
        Input in;
        in.up = b & (1 << 0); in.down = b & (1 << 1); in.left = b & (1 << 2); in.right = b & (1 << 3);
        in.ollie = b & (1 << 4); in.grab = b & (1 << 5); in.grind = b & (1 << 6); in.manual = b & (1 << 7);
        in.olliePress = b & (1 << 8); in.flipPress = b & (1 << 9); in.grabPress = b & (1 << 10);
        in.grindPress = b & (1 << 11); in.manualPress = b & (1 << 12);
        return in;
    }
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;
        uint32_t v = 1, flags = worldSystems ? 1u : 0u;
        fwrite(&MAGIC, 4, 1, f);
        fwrite(&v, 4, 1, f);
        fwrite(&ticks, 4, 1, f);
        fwrite(&seed, 4, 1, f);
        fwrite(&hash, 4, 1, f);
        fwrite(&flags, 4, 1, f);
        if (ticks) fwrite(input.data(), 2, ticks, f);
        fclose(f);
        return true;
    }
    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        uint32_t magic = 0, v = 0, flags = 0;
        bool ok = fread(&magic, 4, 1, f) == 1 && magic == MAGIC;
        ok = ok && fread(&v, 4, 1, f) == 1 && v == 1;
        ok = ok && fread(&ticks, 4, 1, f) == 1;
        ok = ok && fread(&seed, 4, 1, f) == 1;
        ok = ok && fread(&hash, 4, 1, f) == 1;
        ok = ok && fread(&flags, 4, 1, f) == 1;
        worldSystems = flags & 1u;
        input.resize(ticks);
        if (ok && ticks) ok = fread(input.data(), 2, ticks, f) == ticks;
        fclose(f);
        return ok;
    }
};

// Run `seconds` of random input. When `rec` is given the exact inputs and the
// resulting state hash are stored, so the same run can be replayed later.
struct MonkeyStats {
    long long bails = 0;
    int maxCombo = 0;
    double frames = 0;
    bool finite = true;
    long stuckFrames = 0;        // pressing forward but not moving
    float maxSpeed = 0;
    long long score = 0;
};
inline MonkeyStats runMonkey(int seconds, uint32_t seed, Replay* rec = nullptr, bool worldSystems = true,
                             double* outMs = nullptr) {
    MonkeyStats st;
    MonkeyRng r(seed);
    Sim sim;
    sim.worldSystems = worldSystems;
    double t0 = nowMs();
    int prevState = P.state;
    V3 lastPos = P.pos;
    for (int f = 0; f < seconds * 60; f++) {
        Input in = monkeyInput(r, P);
        if (rec) rec->input.push_back(Replay::pack(in));
        sim.tick(in);
        if (P.state == ST_BAIL && prevState != ST_BAIL) st.bails++;
        prevState = P.state;
        st.maxCombo = std::max(st.maxCombo, P.combo.mult);
        st.frames++;
        st.maxSpeed = std::max(st.maxSpeed, len(P.vel));
        if (!(std::isfinite(P.pos.x) && std::isfinite(P.pos.y) && std::isfinite(P.pos.z))) st.finite = false;
        if (in.up && lenXZ(P.pos - lastPos) < 0.002f) st.stuckFrames++;
        lastPos = P.pos;
    }
    st.score = P.score;
    if (rec) {
        rec->ticks = (uint32_t)rec->input.size();
        rec->seed = seed;
        rec->worldSystems = worldSystems;
        rec->hash = worldSystems ? worldHash() : playerHash();
    }
    if (outMs) *outMs = nowMs() - t0;
    return st;
}
// Replay a recording and return the resulting hash.
inline uint32_t playReplay(const Replay& rep) {
    Sim sim;
    sim.worldSystems = rep.worldSystems;
    for (uint32_t i = 0; i < rep.ticks; i++) sim.tick(Replay::unpack(rep.input[i]));
    return rep.worldSystems ? worldHash() : playerHash();
}

// ------------------------------------------------------------- level tools --
inline int findGap(const std::string& name) {
    for (int i = 0; i < (int)world.gaps.size(); i++)
        if (world.gaps[i].name == name) return i;
    return -1;
}
inline bool gotoGap(const std::string& name) {
    int i = findGap(name);
    if (i < 0) return false;
    const Gap& g = world.gaps[i];
    V3 p(g.cx, 0, g.cz + g.hz * 2.f + 2.f);
    p.y = world.ground(p.x, p.z, 4.f).h + 0.05f;
    P.reset(p, yawOf(V3(g.cx - p.x, 0, g.cz - p.z)));
    return true;
}
// world.pointBlocked() carries a 0.2 m skin so a probe standing on a slab reads
// as "inside" it. Probe at chest height when asking "is this body inside a wall?"
inline bool blockedAbove(V3 feet, float height = 0.9f) {
    return world.pointBlocked(feet + V3(0, height, 0), false);
}

// Highest walkable surface within `radius` of (x,z) above yRef (for spawn helpers).
inline float groundNear(float x, float z, float radius, float yRef = 6.f) {
    float best = world.ground(x, z, yRef).h;
    for (int i = 0; i < 16; i++) {
        float a = TAU * i / 16.f;
        best = std::max(best, world.ground(x + std::cos(a) * radius, z + std::sin(a) * radius, yRef).h);
    }
    return best;
}

} // namespace hns
