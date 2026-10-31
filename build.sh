#!/usr/bin/env sh
# Build the engine host, game DLL, and shaders on macOS / Linux.
#
# Usage:
#   ./build.sh             # build everything (default)
#   ./build.sh all
#   ./build.sh host        # rebuild only the host binary
#   ./build.sh game        # rebuild only the game shared library
#   ./build.sh shaders     # rebuild only the SPIR-V / MSL shader blobs
#   ./build.sh clean       # remove the build cache
set -e
cd "$(dirname "$0")"

MODE="${1:-all}"
PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')"
BUILD="build/$PLATFORM"
SHADERS_OUT="assets/shaders/compiled"
mkdir -p "$BUILD" "$SHADERS_OUT"

CXX="${CXX:-clang++}"
CXXFLAGS="-std=c++20 -g -O0 -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti"
INCLUDES="-I. -Isrc -Iexternal -Iexternal/imgui -Iexternal/imgui/backends"

# Locate SDL3.
#  - macOS: Homebrew (`brew install sdl3`) via `brew --prefix sdl3`.
#  - Linux: pkg-config (or self-built).
#  - Override either by exporting SDL3_DIR=<prefix>.
SDL_CFLAGS=""
SDL_LIBS=""
if [ -n "$SDL3_DIR" ]; then
    SDL_CFLAGS="-I$SDL3_DIR/include"
    SDL_LIBS="-L$SDL3_DIR/lib -Wl,-rpath,$SDL3_DIR/lib -lSDL3"
elif [ "$PLATFORM" = "darwin" ]; then
    command -v brew >/dev/null 2>&1 || { echo "build.sh: need Homebrew on macOS (or set SDL3_DIR)." >&2; exit 1; }
    SDL_PREFIX="$(brew --prefix sdl3 2>/dev/null || true)"
    if [ -z "$SDL_PREFIX" ] || [ ! -d "$SDL_PREFIX/include/SDL3" ]; then
        echo "build.sh: SDL3 not found. Run: brew install sdl3" >&2
        exit 1
    fi
    SDL_CFLAGS="-I$SDL_PREFIX/include"
    SDL_LIBS="-L$SDL_PREFIX/lib -Wl,-rpath,$SDL_PREFIX/lib -lSDL3"
else
    SDL_CFLAGS="$(pkg-config --cflags sdl3)"
    SDL_LIBS="$(pkg-config --libs sdl3)"
fi

if [ "$PLATFORM" = "darwin" ]; then
    GAME_LIB="$BUILD/libgame.dylib"
    GAME_FLAGS="-dynamiclib -fPIC"
else
    GAME_LIB="$BUILD/libgame.so"
    GAME_FLAGS="-shared -fPIC"
fi

_stale() {
    out="$1"; shift
    [ ! -f "$out" ] && return 0
    for src in "$@"; do
        [ "$src" -nt "$out" ] && return 0
    done
    return 1
}

_run() {
    echo "+ $*"
    "$@"
}

build_game() {
    echo "==> game"
    _run $CXX $CXXFLAGS $GAME_FLAGS $INCLUDES -o "$GAME_LIB" game.cpp
}

build_host() {
    echo "==> host"
    # ui_renderer.cpp builds in its own TU so stb_truetype doesn't collide
    # with the imstb_truetype copy ImGui pulls in.
    if _stale "$BUILD/ui_renderer.o" src/engine/ui_renderer.cpp src/engine/ui_renderer.h; then
        _run $CXX $CXXFLAGS -Wno-unused-function $INCLUDES $SDL_CFLAGS -c -o "$BUILD/ui_renderer.o" src/engine/ui_renderer.cpp
    fi
    # ImGui in a sibling TU so editing main.cpp doesn't recompile 80K
    # lines of ImGui every time.
    if _stale "$BUILD/imgui.o"    src/build/imgui_unity.cpp external/imgui/imgui.cpp external/imgui/imgui_draw.cpp external/imgui/imgui_tables.cpp external/imgui/imgui_widgets.cpp external/imgui/imgui_demo.cpp external/imgui/backends/imgui_impl_sdl3.cpp external/imgui/backends/imgui_impl_sdlgpu3.cpp external/imgui/imgui.h external/imgui/imgui_internal.h; then
        _run $CXX $CXXFLAGS $INCLUDES $SDL_CFLAGS -c -o "$BUILD/imgui.o"    src/build/imgui_unity.cpp
    fi
    _run $CXX $CXXFLAGS $INCLUDES $SDL_CFLAGS -o "$BUILD/engine" main.cpp \
        "$BUILD/imgui.o" "$BUILD/ui_renderer.o" $SDL_LIBS
}

build_shaders() {
    echo "==> shaders"
    if command -v glslc >/dev/null 2>&1; then
        for src in assets/shaders/*.vert.glsl; do
            [ -f "$src" ] || continue
            out="$SHADERS_OUT/$(basename "${src%.glsl}").spv"
            glslc -fshader-stage=vert "$src" -o "$out"
            echo "    $src -> $out"
        done
        for src in assets/shaders/*.frag.glsl; do
            [ -f "$src" ] || continue
            out="$SHADERS_OUT/$(basename "${src%.glsl}").spv"
            glslc -fshader-stage=frag "$src" -o "$out"
            echo "    $src -> $out"
        done
    else
        if [ "$PLATFORM" = "darwin" ]; then
            echo "    glslc not found (brew install shaderc) -- skipping SPIR-V" >&2
        else
            echo "    glslc not found -- skipping SPIR-V output" >&2
            echo "      apt install glslc / dnf install shaderc" >&2
        fi
    fi

    for src in assets/shaders/*.metal; do
        [ -f "$src" ] || continue
        out="$SHADERS_OUT/$(basename "${src%.metal}").msl"
        cp "$src" "$out"
        echo "    $src -> $out"
    done
}

build_clean() {
    echo "==> clean"
    rm -rf "$BUILD"
    return 0
}

case "$MODE" in
    all)     build_shaders; build_game; build_host ;;
    game)    build_game ;;
    host)    build_host ;;
    shaders) build_shaders ;;
    clean)   build_clean; exit 0 ;;
    *)
        echo "usage: $0 [all|host|game|shaders|clean]" >&2
        exit 2 ;;
esac

# Symlink assets/ into the build dir so the engine, running from there,
# finds them via cwd-relative paths like "assets/shaders/compiled/...".
if [ ! -e "$BUILD/assets" ]; then
    ln -snf "../../assets" "$BUILD/assets"
fi

echo "OK -> $BUILD"
