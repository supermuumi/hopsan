# Engine skeleton

Cross-platform game engine skeleton extracted from
[ikimaa](https://github.com/supermuumi/ikimaa). Provides the window, input,
dev console, asset cache, renderer (SDL_GPU + ImGui), native HUD renderer,
and a hot-reloadable game DLL. The game itself is a 50-line `game.cpp`
stub that draws "hello world".

## Quick start

**macOS** (Apple Silicon):
```sh
brew install sdl3 shaderc
./build.sh
./build/darwin/engine
```

**Linux**:
```sh
sudo apt install libsdl3-dev glslc   # or your distro's equivalent
./build.sh
./build/linux/engine
```

**Windows**: open an *x64 Native Tools Command Prompt for VS 2022*, then:
```cmd
build.bat
build\win\engine.exe
```
SDL3 headers + import lib are expected under `ext/sdl3` (or set
`SDL3_DIR` to wherever you put SDL3-devel-VC).

## What you get

- SDL3 + SDL_GPU host with a Vulkan / Metal renderer
- Hot-reloadable game DLL — recompile `game.cpp` and the host picks
  it up live, preserving game state across reloads
- Dev console (F1 to toggle)
- Native HUD renderer: world-space bars, screen-space panels, screen-space text
- ImGui (with SDL3 + SDL_GPU backends, multi-viewport + docking enabled)
- Asset cache for textures and glTF meshes
- Fullscreen toggle (Alt+Enter / Cmd+Ctrl+F)
- Shader pipeline: GLSL → SPIR-V (Vulkan) and Metal source (macOS)
- Shadow mapping, directional sunlight, instanced tile / cube / mesh draws

## Where to add things

- Per-frame logic and rendering: `game.cpp`
- New host services: extend `EnginePlatform` in `src/shared/engine_api.h`
  and wire them up in `main.cpp`
- New shaders: add `.glsl` / `.metal` pairs under `assets/shaders/`,
  rebuild with `./build.sh shaders`
