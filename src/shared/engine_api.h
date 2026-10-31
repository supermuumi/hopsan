// Stable interface between the host (engine binary) and the game DLL.
// Everything the game DLL imports from the host, and everything the
// host calls in the game DLL, lives here.
//
// Layout rules:
// - POD only. No std::* types, no virtuals, no STL containers.
// - Extend at the end; don't reorder fields once shipped.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
    #define GAME_EXPORT extern "C" __declspec(dllexport)
#else
    #define GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

enum LogLevel : int {
    LOG_LEVEL_INFO  = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_ERROR = 2,
};

enum AssetKindAPI : int {
    ASSET_KIND_TEXTURE = 1,
    ASSET_KIND_MESH    = 2,
};

typedef void     (*ConsoleCommandFn)(int argc, const char** argv, void* user);
typedef uint32_t AssetHandle;       // 0 == invalid

// Host-provided services. Game DLL receives this on init and keeps the pointer.
struct EnginePlatform {
    void (*log)(LogLevel level, const char* category, const char* msg);

    // Register a dev console command. `help` shows in the console help list.
    // The host injects the game-state pointer as the `user` arg passed to
    // the callback on invocation, so the game doesn't need to track it.
    void (*console_register)(const char* name, ConsoleCommandFn fn, const char* help);

    // Load (or fetch cached) an asset by path. Returns 0 on failure.
    // Paths are resolved relative to the engine binary's working directory.
    AssetHandle (*asset_load)(AssetKindAPI kind, const char* path);
};

// Per-frame input snapshot. Edge-triggered + held state.
struct FrameInput {
    float    dt;                  // seconds since last frame
    uint64_t frame;               // monotonically increasing frame index
    bool     keys_held[512];      // indexed by SDL_Scancode
    bool     keys_pressed[512];   // edge-triggered: this frame only
    bool     quit_requested;      // window close / Cmd+Q etc.

    float    mouse_x;
    float    mouse_y;
    bool     mouse_down[3];       // L, M, R held this frame
    bool     mouse_pressed[3];    // edge-triggered
    float    mouse_wheel_y;       // accumulated this frame
    int      window_w;            // pixels
    int      window_h;

    // First connected gamepad (SDL_GamepadButton / SDL_GamepadAxis order).
    // Axes are normalised to -1..1 (triggers 0..1). All zero when no pad.
    bool     pad_connected;
    bool     pad_held[32];        // indexed by SDL_GamepadButton
    bool     pad_pressed[32];     // edge-triggered
    float    pad_axes[6];         // LX, LY, RX, RY, LT, RT
};

// Tile geometry shapes the renderer understands. A TileLayer is one
// instanced draw of N tile instances sharing texture + shape.
//   QUAD  -- flat floor quad at the tile's z (the default).
//   CUBE  -- built-in unit box extruded to `height` world units.
//   MESH  -- external gltf passed via `mesh`, instanced.
//   TILED -- autotile mesh variant; the layer's `variant` picks which
//            of a family of meshes (e.g. straight/corner/T/cross/end)
//            this draw renders, and per-instance rot_radians composes
//            with the layer's rot_z.
enum TileShape : uint8_t {
    SHAPE_QUAD  = 0,
    SHAPE_CUBE  = 1,
    SHAPE_MESH  = 2,
    SHAPE_TILED = 3,
};

// One instanced draw of tile quads sharing a texture. Each instance is
// 5 floats: (x, y, z, brightness, rot_radians).
struct TileLayer {
    AssetHandle  texture;
    int          count;
    const float* positions;       // (x, y, z, brightness, rot_radians) instances, count entries
    int          shape;           // TileShape
    float        height;          // extrusion for CUBE (world units)
    AssetHandle  mesh;            // gltf for MESH; per-variant mesh for TILED
    float        rot_x;
    float        rot_y;
    float        rot_z;
    float        off_x;
    float        off_y;
    float        off_z;
    int          variant;         // TILED variant index (informational)
};

struct MeshDraw {
    AssetHandle mesh;
    AssetHandle texture;
    float       model_matrix[16]; // column-major
};

// One world-space HUD bar. The host projects world_x/world_y through
// (view * proj) and draws a small bar at the resulting screen point.
struct HudBar {
    float    world_x;
    float    world_y;
    float    frac;     // 0..1; foreground portion
    uint32_t color;    // IM_COL32-packed ABGR (low byte R)
};

// Screen-space rectangle in pixel coordinates. (x, y) is top-left.
struct HudPanel {
    float    x;
    float    y;
    float    w;
    float    h;
    uint32_t bg_color;     // 0 = no fill
    uint32_t border_color; // 0 = no border
};

// Screen-space text run. ASCII only (32..126); UTF-8 unsupported.
struct HudText {
    float    x;
    float    y;      // baseline (not top-left)
    uint32_t color;
    char     text[96];
    int      font;   // index into the host's UI font list (0 = ProggyClean 13px)
};

// Screen-space solid rectangle in window pixels, drawn by the UI renderer
// before HUD panels + text. (x0, y0) top-left, (x1, y1) bottom-right.
// Used by 2D games that do their own world -> screen transform.
struct Quad2D {
    float    x0, y0, x1, y1;
    uint32_t color;    // IM_COL32-packed ABGR (low byte R)
};

constexpr int kMaxHudBars   = 32;
constexpr int kMaxHudPanels = 64;
constexpr int kMaxHudTexts  = 256;

// Host UI font slots (see main.cpp). The game picks one per HudText.
enum HudFont : int {
    HUD_FONT_SMALL     = 0,  // ProggyClean 13px
    HUD_FONT_TITLE     = 1,  // Nova Round 22px
    HUD_FONT_SMALL_2X  = 2,  // ProggyClean 26px (hi-dpi)
    HUD_FONT_TITLE_2X  = 3,  // Nova Round 44px (hi-dpi)
};

// What the game wants drawn this frame.
struct RenderCommands {
    float clear_color[4];         // RGBA, 0..1
    float view_matrix[16];        // column-major
    float proj_matrix[16];        // column-major
    float tint_rgb[3];            // colour-grading tint; multiplied into tile + mesh frag

    // Directional sunlight. out = texture * tint * (ambient + sun_color * max(N.L, 0)).
    // sun_dir is the unit vector pointing TOWARD the sun, in world space.
    float sun_dir[3];
    float sun_color[3];
    float ambient[3];

    // Shadow framing. The renderer fits an orthographic light frustum
    // around this world-space sphere to render the shadow map.
    // radius <= 0 disables shadows for the frame.
    float shadow_center[3];
    float shadow_radius;

    int       tile_layer_count;
    TileLayer tile_layers[128];

    int       mesh_draw_count;
    MeshDraw  mesh_draws[64];

    int       hud_bar_count;
    HudBar    hud_bars[kMaxHudBars];

    int       hud_panel_count;
    HudPanel  hud_panels[kMaxHudPanels];

    int       hud_text_count;
    HudText   hud_texts[kMaxHudTexts];

    // Screen-space 2D quads. Points into game memory; must stay valid
    // until the next game_tick. Drawn in order, before HUD panels/text.
    const Quad2D* quads;
    int           quad_count;
};

// ---- Game DLL exports ---------------------------------------------------
//
// Resolved by the host via dlsym / GetProcAddress after the DLL loads.
// Every running session calls these in order: init once, then tick + render
// each frame.

typedef void (*GameInitFn)  (void* memory, size_t size, EnginePlatform* platform);
typedef void (*GameTickFn)  (void* memory, const FrameInput* input);
typedef void (*GameRenderFn)(void* memory, RenderCommands* out);
