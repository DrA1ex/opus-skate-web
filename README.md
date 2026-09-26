# OpusSkate Web

Browser/WebAssembly port of [OminousIndustries/OpusSkate](https://github.com/OminousIndustries/OpusSkate).

The goal is to preserve the original game logic, physics, level generation, visuals, audio and controls as closely as possible while making the game playable directly in a modern browser.

## Play

Once GitHub Pages is enabled for this repository, the current `main` build is published automatically at:

**https://dra1ex.github.io/opus-skate-web/**

Every push to `main` builds the Emscripten/WebAssembly version and deploys the resulting static files to GitHub Pages. Pull requests build and validate the output but do not deploy it.

For the first deployment only, enable Pages in:

`Settings → Pages → Build and deployment → Source → GitHub Actions`

After that, no local build or manual deployment is required.

## Local build

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

## Browser port

The original game is a single C++17 source file using SDL2 and OpenGL 3.3. This port keeps the gameplay code in C++ and compiles it to WebAssembly with Emscripten.

The browser-specific compatibility layer currently covers:

- SDL2 through Emscripten
- OpenGL 3.3 shaders converted to GLSL ES 3.00 / WebGL2
- browser-friendly fullscreen canvas
- WebGL-compatible clipping and framebuffer handling
- automatic GitHub Pages deployment
- shader and WebGL runtime diagnostics

The gameplay simulation still runs at the original fixed 120 Hz and the procedural world, audio, physics and controls stay in the original C++ implementation.

## Upstream

Original project: https://github.com/OminousIndustries/OpusSkate
