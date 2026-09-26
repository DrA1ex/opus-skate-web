> Source: adapted from the headless harness in adeism/OSkate, branch `arena/01a0cbce-oskate`,
> commit `4bdcb144bb6f82379ee3ebeaddbd1657f65aa263`. Focused runtime regressions also
> incorporate cases from intra-secdsm/OpusSkate commit `62815a02a3102a968956ba793b79357a272fae52`.

# OpusSkate web headless harness

This harness runs the real `skate.cpp` gameplay code without a GPU, window, or audio
device. Its job is to catch behavioral regressions in physics, tricks, world simulation,
audio, input handling, and deterministic replay.

It intentionally does **not** treat the current level layout or CI machine speed as an API.
Map content changes often in this fork, so exact object counts, heuristic reachability
checks, and wall-clock microbenchmarks are not CI gates.

## Run it

```bash
bash tools/harness/check.sh
bash tools/harness/check.sh --filter physics
bash tools/harness/check.sh --repeat 3
bash tools/harness/check.sh --junit harness.xml
bash tools/harness/check.sh --asan
bash tools/harness/check.sh --tsan
```

The standalone runner also keeps useful development tools:

```bash
/tmp/opus-skate-harness --list
/tmp/opus-skate-harness --dump
/tmp/opus-skate-harness --monkey 30 --seed 7
/tmp/opus-skate-harness --record run.oskr --monkey 30
/tmp/opus-skate-harness --replay run.oskr
/tmp/opus-skate-harness --goto "FOUNTAIN GAP"
```

## What is covered

The default suite currently contains 88 tests:

- **math** — matrix conventions/inverse, angle wrapping, frame-rate-independent damping,
  projection, yaw conversion, deterministic RNG;
- **level** — collision grid agrees with brute force, gaps are structurally valid, letters
  and spawn are legal, world bounds contain playable objects, pedestrian spawns are legal,
  and a gap cannot be awarded twice;
- **physics** — acceleration/braking, ollie, coyote time, walls, river, ramps,
  quarter-pipes, stairs/slopes, out-of-world recovery, and tick-rate stability;
- **tricks** — flips, grabs, spins, grind/manual behavior, combo scoring, landing grace,
  S-K-A-T-E collection, and an integrated reference line;
- **world** — NPC behavior, traffic, pigeons, particles, HUD/dynamic-mesh safety, RNG
  isolation, and bounded runtime collections;
- **audio** — generated SFX sanity, voice limits, bounded/finite mixer output, live
  parameter updates, synchronized concurrent snapshots, and no-device behavior;
- **runtime** — regressions for transition `qpAir`, queued input edges, and screenshot I/O;
- **replay** — deterministic player/world state, record/replay round-trip, tamper detection,
  long-run determinism, fuzzing, and repeated bail/respawn safety.

## Deliberately not tested

These were present in the imported harness but were removed from CI because they caused
maintenance without protecting a stable contract:

- exact or broad counts of solids, rails, gaps, NPCs, or other current map content;
- "no more than N blocked rails" baselines;
- straight-line reachability of every gap/letter;
- random map sampling that encoded assumptions about the old level layout;
- exact music sequencer pattern counts;
- wall-clock performance budgets on GitHub runners;
- trivial helper tests already exercised by higher-level gameplay/replay tests.

Performance can still be profiled with the normal browser build and dedicated profiling
when optimization work is being done. Rendering itself remains covered by the existing
Emscripten/Playwright smoke test rather than this CPU-only harness.

## CI

`.github/workflows/headless.yml` runs the suite on Ubuntu and macOS. A separate sanitizer
job runs ASan/UBSan and ThreadSanitizer. Timing is informational only and never decides
whether sanitizer runs pass.

The harness includes the actual game source via:

```cpp
#define main oskate_main
#include "skate.cpp"
#undef main
```

so tests exercise production gameplay code rather than a reimplementation.
