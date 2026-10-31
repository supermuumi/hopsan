#pragma once

#include "shared/engine_api.h"

namespace engine {

// Wraps a hot-reloadable game library. Copies the library to a "_loaded"
// sibling before opening so the source file remains writable (essential
// on Windows where loaded DLLs are locked).
struct GameLoader {
    // Either the source library is missing or we haven't loaded successfully
    // yet. Functions are null and should not be called.
    bool         loaded = false;

    // Entry points resolved from the live copy.
    GameInitFn   init   = nullptr;
    GameTickFn   tick   = nullptr;
    GameRenderFn render = nullptr;

    // Opaque internal state.
    void*        impl = nullptr;
};

// `source_path` is the path to game.dll / libgame.dylib / libgame.so that the
// build system produces. The loader makes its own copy next to it.
bool game_loader_init(GameLoader* gl, const char* source_path);
void game_loader_shutdown(GameLoader* gl);

// Polls mtime of the source library. If newer (and stable for one tick), copies
// it to the live path, unloads the previous handle, loads the new one, and
// re-resolves entry points. Returns true if a reload happened this call.
bool game_loader_poll(GameLoader* gl);

// Unconditionally reload from the source path (or load if not yet loaded).
// Returns true on success.
bool game_loader_force_reload(GameLoader* gl);

// Resolve an additional exported symbol from the currently loaded library.
// Returns nullptr if not loaded or the symbol is missing. Result is invalidated
// by the next reload.
void* game_loader_resolve(GameLoader* gl, const char* symbol);

} // namespace engine
