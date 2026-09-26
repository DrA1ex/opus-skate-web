# OpusSkate Web

Browser/WebAssembly port of [OminousIndustries/OpusSkate](https://github.com/OminousIndustries/OpusSkate).

The goal is to preserve the original game logic, physics, level generation, visuals, audio and controls as closely as possible while making the game playable directly in a modern browser.

## Build

Requires the Emscripten SDK.

```bash
./build-web.sh
python3 -m http.server 8000 -d dist
```

Then open http://localhost:8000.

## Controls

- W / Up — push
- S / Down — brake
- A / D or Left / Right — steer, spin, balance
- Space — ollie
- J / Z + direction — flip tricks
- K / X + direction — grab tricks
- L / C + direction — grind / slide
- I / Shift (+W) — manual / nose manual
- R — reset
- H — help
- M — music
- V — camera
- T — 2-minute session
- Esc — pause
- F11 — fullscreen

## Upstream

The original game is a single C++17 source file using SDL2 and OpenGL 3.3. This port keeps the gameplay code in C++ and compiles it to WebAssembly with Emscripten. The browser-specific changes are kept intentionally small.
