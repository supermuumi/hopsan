# engine skeleton — agent notes

## What this is

A minimal cross-platform game engine skeleton, extracted from
[ikimaa](https://github.com/supermuumi/ikimaa). The host owns the window,
input, dev console, asset cache, renderer, and a hot-reloadable game DLL.
The game DLL (`game.cpp`) is a 50-line stub that draws "hello world".
Drop your own game in `game.cpp`; everything else is reusable plumbing.

## Target platforms

Primary platforms are **Windows** and **macOS** (Apple Silicon). Linux is
a "should just work" target but is not a priority. Compile-check only if
it's free.

## Layout

- `main.cpp` — host: window, input, frame loop, ImGui, hot reload, draw.
- `game.cpp` — game DLL. Exports `game_init`, `game_tick`, `game_render`.
- `src/shared/engine_api.h` — host ↔ DLL ABI. POD only.
- `src/engine/` — engine modules compiled into the host:
  - `log` / `console` — dev console + file log.
  - `game_loader` — DLL hot reload (poll mtime, dlopen / LoadLibrary).
  - `asset_cache` — texture + mesh loader keyed by path.
  - `renderer` — SDL_GPU tile / cube / mesh / shadow passes.
  - `ui_renderer` — native screen-space rects + text via stb_truetype.
  - `hud` — project HudBar world coords to screen + queue panels / text.
- `external/imgui` — vendored ImGui (with SDL3 + SDL_GPU backends).
- `ext/sdl3` — Windows SDL3 import lib (macOS / Linux use system / brew).
- `assets/shaders/` — GLSL + Metal sources; compiled into `compiled/`.
- `assets/fonts/` — ProggyClean (HUD font 0), Nova Round (font 1).

## Building

- macOS / Linux: `./build.sh` (needs `brew install sdl3 shaderc` on Mac).
- Windows: `build.bat` from an x64 Native Tools prompt for VS 2022.
- Targets: `host`, `game`, `shaders`, `all`, `clean`.

The engine binary lives in `build/<platform>/`; the build script
symlinks `assets/` into that directory so cwd-relative asset paths work.

## DLL hot reload

`./build.sh game` while the engine is running re-links the game DLL;
the host notices the mtime bump within ~250 ms and reloads. Game state
in the `kGameArenaBytes` arena survives the reload, so you can iterate
on game logic without losing your place.

## Branch naming

Use plain slug branch names tied to the task — e.g. `add-audio`,
`tilemap-editor`, `physics-2d`. No random alphanumeric suffixes.
