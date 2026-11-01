// Shared types for the hopsan game DLL. The game is a unity build:
// game.cpp includes this header and then every src/game/*.cpp module in
// dependency order, so everything lives in one translation unit.
//
// All game state lives in one POD struct (GameState) placed at the start
// of the host-provided arena. No pointers into the arena are stored, so
// the state survives DLL hot reloads as long as the layout is unchanged
// (kStateVersion + sizeof check in game_init).

#pragma once

#include "shared/engine_api.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hop {

constexpr const char* kTag = "game";

// Bump whenever GameState's layout changes so a hot reload re-initialises
// instead of reading garbage.
constexpr uint32_t kStateMagic   = 0x484F5053u; // 'HOPS'
constexpr uint32_t kStateVersion = 11;

// ---- Units ---------------------------------------------------------------
// World units are pixels of the virtual screen. One tile is kTile pixels.

constexpr int   kTile      = 16;
constexpr int   kArtScale  = 2;     // art pixels per world unit (32px tile art)
constexpr int   kViewW     = 640;   // virtual screen, in world pixels
constexpr int   kViewH     = 360;
constexpr float kStep      = 1.0f / 60.0f;
constexpr int   kRoomDefW  = 40;    // a "one screen" room, in tiles
constexpr int   kRoomDefH  = 23;
constexpr int   kRoomMaxW  = 256;
constexpr int   kRoomMaxH  = 256;

constexpr const char* kWorldPath = "assets/levels/world.txt";

// ---- Tiles ---------------------------------------------------------------

enum Tile : uint8_t {
    T_EMPTY = 0,
    T_ROCK,
    T_BRICK,
    T_BG,          // decorative backdrop, not solid
    T_ONEWAY,      // jump-through platform (solid from above only)
    T_SPIKE_U,     // points up, sits on a floor
    T_SPIKE_D,     // points down, hangs from a ceiling
    T_SPIKE_L,     // points left, mounted on a wall to its right
    T_SPIKE_R,     // points right, mounted on a wall to its left
    T_CRUMBLE,     // falls away shortly after you touch it, then returns
    T_SPRING,      // launches you upward
    T_CRYSTAL,     // refills your dash in mid-air
    T_CHECKPOINT,  // respawn point
    T_WALKER,      // enemy: patrols left / right, turns at walls + ledges
    T_START,       // where a new game begins (one per world)
    // Decorations: not solid, drawn behind the player.
    T_DECOR_GRASS,
    T_DECOR_FERN,
    T_DECOR_SHROOMS,
    T_DECOR_RUBBLE,
    T_DECOR_VINES,   // hangs from the tile's top
    T_DECOR_CHAIN,   // hangs from the tile's top
    T_DECOR_TORCH,
    T_DECOR_PILLAR,  // two tiles tall, stands on the tile's floor
    T_STAR,          // collectible (plan piece), counted once per save
    // Lair hazards
    T_LASER,         // emitter, always on; fires away from the wall it's mounted on
    T_LASER_A,       // pulsing emitter, phase A
    T_LASER_B,       // pulsing emitter, phase B (on while A is off)
    T_WATER,         // swim: 8-way movement, float up, jump out at the surface
    T_SHARK,         // water tile with a shark that patrols (and chases a bit)
    T_CONVEYOR_L,    // solid, carries you left
    T_CONVEYOR_R,    // solid, carries you right
    T_DRONE_H,       // security drone, patrols horizontally
    T_DRONE_V,       // security drone, patrols vertically
    // Switches + reactions
    T_SWITCH,        // lever: touching it sets its flag (tile cond = flag id)
    T_LAVA,          // flows, ignites, turns to basalt on water, cools to basalt
    T_WOOD,          // solid crate / barricade, flammable
    T_FIRE,          // burning tile (runtime; burns out to nothing)
    T_BASALT,        // solid, made by lava meeting water or cooling
    T_CAULDRON,      // solid pot of lava; push or dash into it to tip it
    T_CAULDRON_TIPPED,
    T_CABLE,         // live wire: lethal, electrifies water it touches, flammable
    T_FUSE,          // wick: burns fast; when it burns out it sets its flag (cond)
    T_ICE,           // solid, slippery, unclimbable; heat thaws it into (flowing) water
    T_COUNT
};

struct TileInfo {
    char        ch;     // save-file character
    const char* name;
    uint32_t    color;  // editor minimap / palette colour
};

constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

constexpr TileInfo kTileInfo[T_COUNT] = {
    { '.', "empty",      rgba(0, 0, 0, 0)        },
    { '#', "rock",       rgba(92, 84, 124)       },
    { 'B', "brick",      rgba(140, 82, 66)       },
    { 'b', "backdrop",   rgba(48, 46, 70)        },
    { '=', "platform",   rgba(170, 124, 78)      },
    { '^', "spikes up",  rgba(214, 222, 236)     },
    { 'v', "spikes down",rgba(214, 222, 236)     },
    { '<', "spikes left",rgba(214, 222, 236)     },
    { '>', "spikes right",rgba(214, 222, 236)    },
    { '%', "crumble",    rgba(196, 156, 104)     },
    { 'S', "spring",     rgba(236, 200, 70)      },
    { '*', "crystal",    rgba(96, 236, 150)      },
    { 'C', "checkpoint", rgba(90, 220, 230)      },
    { 'E', "walker",     rgba(170, 80, 190)      },
    { '@', "start",      rgba(255, 255, 255)     },
    { 'g', "grass",      rgba(90, 160, 80, 160)  },
    { 'f', "fern",       rgba(60, 130, 70, 160)  },
    { 'm', "mushrooms",  rgba(190, 120, 90, 160) },
    { 'r', "rubble",     rgba(80, 86, 110, 160)  },
    { '~', "vines",      rgba(80, 150, 70, 160)  },
    { '|', "chain",      rgba(120, 120, 130, 160)},
    { 't', "torch",      rgba(255, 170, 60, 200) },
    { 'p', "pillar",     rgba(70, 84, 104, 160)  },
    { '$', "plan piece", rgba(240, 240, 250)     },
    { 'L', "laser",      rgba(230, 40, 60)       },
    { 'j', "laser A",    rgba(230, 90, 60)       },
    { 'J', "laser B",    rgba(230, 60, 120)      },
    { 'w', "water",      rgba(40, 110, 200, 140) },
    { 'K', "shark",      rgba(110, 130, 150)     },
    { '{', "conveyor L", rgba(80, 80, 96)        },
    { '}', "conveyor R", rgba(80, 80, 96)        },
    { 'D', "drone H",    rgba(200, 60, 60)       },
    { 'd', "drone V",    rgba(200, 60, 60)       },
    { '/', "switch",     rgba(230, 200, 60)      },
    { '&', "lava",       rgba(255, 110, 30)      },
    { 'W', "wood crate", rgba(150, 100, 56)      },
    { '!', "fire",       rgba(255, 170, 40)      },
    { 'o', "basalt",     rgba(58, 52, 64)        },
    { 'U', "cauldron",   rgba(40, 40, 46)        },
    { 'u', "tipped cauldron", rgba(40, 40, 46)   },
    { 'z', "live cable", rgba(250, 230, 90)      },
    { 'i', "fuse",       rgba(170, 130, 80)      },
    { 'I', "ice",        rgba(170, 220, 245)     },
};

inline bool tile_is_spike(uint8_t t) { return t >= T_SPIKE_U && t <= T_SPIKE_R; }
inline bool tile_is_decor(uint8_t t) { return t >= T_DECOR_GRASS && t <= T_DECOR_PILLAR; }
inline bool tile_is_laser(uint8_t t) { return t == T_LASER || t == T_LASER_A || t == T_LASER_B; }
inline bool tile_is_water(uint8_t t) { return t == T_WATER || t == T_SHARK; }
inline int  conveyor_dir(uint8_t t)  { return t == T_CONVEYOR_L ? -1 : (t == T_CONVEYOR_R ? 1 : 0); }
inline bool tile_is_flammable(uint8_t t) {
    return t == T_WOOD || t == T_FUSE || t == T_CABLE || t == T_ONEWAY || t == T_DECOR_GRASS
        || t == T_DECOR_FERN || t == T_DECOR_SHROOMS || t == T_DECOR_VINES;
}

// ---- World storage -------------------------------------------------------
// Tiles live in one global sparse grid of 32x32 chunks so rooms can sit
// next to each other and the player can cross between them seamlessly.
// Rooms are rectangles over that grid: they bound the camera, define the
// respawn scope, and are the unit the save file is written in.

constexpr int kChunkShift = 5;
constexpr int kChunk      = 1 << kChunkShift;
constexpr int kMaxChunks  = 4096;
constexpr int kChunkHash  = 8192;   // power of two, > 2 * kMaxChunks

// Golf-hole clock tags: a tile can exist only "from hole N" and/or only
// "until hole N" (gone once hole N starts). 0 = no limit. Holes are 1..18.
constexpr int kHoles = 18;

struct Chunk {
    int32_t cx, cy;
    uint8_t t[kChunk * kChunk];
    uint8_t from[kChunk * kChunk];
    uint8_t until[kChunk * kChunk];
    // World-flag condition: 1..31 = present while that flag is set,
    // 129..159 (bit 7) = present while it is NOT set. For switches and
    // fuses it's instead the flag they set (low 5 bits).
    uint8_t cond[kChunk * kChunk];
};

constexpr int     kMaxFlags   = 31;
constexpr uint8_t kCondNot    = 0x80;
inline int  cond_flag(uint8_t c) { return c & 0x1F; }
inline bool cond_is_target(uint8_t t) { return t == T_SWITCH || t == T_FUSE; }

struct ChunkMap {
    int32_t count;
    int32_t hash[kChunkHash];       // chunk index + 1; 0 = empty slot
    Chunk   chunks[kMaxChunks];
};

constexpr int kMaxRooms = 512;

struct Room {
    int32_t x, y, w, h;   // in tiles
    bool    used;
    uint8_t dark;         // 0 = lit, 255 = always dark, N = dark until flag N is set
    char    name[30];
};

struct World {
    int32_t  gate_hole;      // > 0: tile_get hides tiles not present at this hole
    bool     gate_use_flags; // tile_get applies flag conditions (play mode)
    uint32_t gate_flags;     // bit N = flag N set
    ChunkMap map;
    int32_t  room_count;   // high-water mark; slots may be unused
    Room     rooms[kMaxRooms];
};

// ---- Runtime entities ----------------------------------------------------
// Built from the tiles of the current room whenever you enter it or
// respawn, so crumbles / crystals / enemies reset like in Celeste.

enum EntKind : uint8_t {
    E_CRUMBLE,
    E_SPRING,
    E_CRYSTAL,
    E_CHECKPOINT,
    E_WALKER,
    E_STAR,      // state 0 = available, 1 = picked up (pending), 2 = collected earlier
    E_LASER,     // dir 0..3 = fires right/left/down/up; x = beam length; state = lethal now
    E_SHARK,     // x, y = top-left of 24x10 hitbox; dir = facing
    E_DRONE_H,   // x, y = top-left of 12x10 hitbox; dir = travel direction
    E_DRONE_V,
};

struct Entity {
    uint8_t kind;
    uint8_t state;
    int8_t  dir;
    int32_t tx, ty;   // source tile
    float   x, y;     // walker position (top-left of hitbox)
    float   timer;
};

constexpr int kMaxEntities  = 1024;
constexpr int kMaxParticles = 768;

struct Particle {
    float    x, y, vx, vy;
    float    life, max_life;
    float    w, h;
    float    gravity;
    float    drag;
    uint32_t color;
};

// ---- Player --------------------------------------------------------------

constexpr int kPlayerW = 10;
constexpr int kPlayerH = 18;

enum PlayerState : uint8_t {
    PS_NORMAL,
    PS_CLIMB,
    PS_DASH,
};

struct Player {
    int32_t x, y;           // top-left of hitbox, whole pixels
    float   rx, ry;         // sub-pixel remainders
    float   vx, vy;
    uint8_t state;
    int8_t  facing;         // -1 / +1
    bool    on_ground;
    bool    was_on_ground;

    float   coyote;
    float   jump_buffer;
    float   dash_buffer;
    float   var_jump_timer;
    float   var_jump_speed;
    bool    var_jump_auto;  // sustain the jump without holding the button
    float   force_move_timer;
    int8_t  force_move_x;
    float   drop_through;   // ignore one-way platforms for a moment

    int32_t dashes;
    float   dash_timer;
    float   dash_cooldown;
    float   dash_dx, dash_dy;
    float   trail_timer;

    float   stamina;
    float   squash_x, squash_y;
    float   run_anim;
    float   slide_dust;
    float   carry_vx;       // velocity from a conveyor belt; kept (and fading) in the air
};

// ---- Input ---------------------------------------------------------------

struct Actions {
    int  mx, my;            // digital move axis, -1 / 0 / +1 (y down)
    bool jump_held;
    bool grab_held;
    bool jump_latched;      // pressed since the last sim step
    bool dash_latched;
    bool confirm_latched;   // Enter / Escape / any action button: for cards
};

// ---- Play mode -----------------------------------------------------------

struct StarPos { int32_t x, y; };   // star tile position
constexpr int kMaxStars        = 2048;
constexpr int kMaxPendingStars = 64;

enum WallStyle : uint8_t {
    WALL_CELESTE = 0,   // kick away from the wall; climbing needs grab + stamina
    WALL_HK      = 1,   // Hollow Knight: short kick, hold toward the wall to climb it
};

enum RespawnMode : uint8_t {
    RESPAWN_ROOM_ENTRY = 0,   // Celeste: where you entered the room (or checkpoint)
    RESPAWN_CHECKPOINT = 1,   // Hollow Knight: last checkpoint touched
};

struct Play {
    Player  p;
    int32_t room;             // current room index, -1 = none

    // Respawn bookkeeping.
    int32_t spawn_x, spawn_y; // player top-left
    int32_t spawn_room;
    bool    spawn_wants_ground;   // entered mid-air: move spawn to first safe landing
    bool    has_checkpoint;
    int32_t cp_tx, cp_ty;
    uint8_t respawn_mode;
    uint8_t wall_style;

    // Progress: stars picked up in the current room are provisional until
    // you leave the room or touch a checkpoint ("commit"); dying rolls
    // them back.
    int32_t  star_count;         // committed stars
    StarPos  stars[kMaxStars];
    int32_t  pending_count;      // stars picked up in this room, not yet committed
    StarPos  pending[kMaxPendingStars];
    int32_t  star_total;         // stars in the world (cached)

    // Golf-hole clock. `hole` is what the lair currently shows; it catches
    // up with `hole_target` (set when pieces commit) on the next room entry
    // or respawn, so tiles never change under your feet mid-room.
    int32_t  hole;
    int32_t  hole_target;
    bool     finale_seen;

    // Full-screen interstitial card (new hole / finale). The game is
    // paused while one is up. card_t counts up from the card's start;
    // card_exit_t >= 0 once it's been dismissed and is fading out.
    uint8_t  card_kind;          // CardKind
    uint8_t  card_next;          // queued card to show after this one
    int32_t  card_hole;
    float    card_t;
    float    card_exit_t;

    // World flags (switches, fuses). Permanent once set; saved in progress.
    uint32_t flags;

    // Camera "peek" at the room a flag change affected (pauses the sim).
    int32_t  peek_room;          // -1 = none
    float    peek_t;
    float    peek_from_x, peek_from_y;

    // Reactions: the current room's tiles are snapshotted when it's built
    // and restored on death / leaving, so lava, fire etc. always reset.
    bool     snap_valid;
    int32_t  snap_x, snap_y, snap_w, snap_h;
    uint8_t  snap_t[kRoomMaxW * kRoomMaxH];
    uint8_t  snap_from[kRoomMaxW * kRoomMaxH];
    uint8_t  snap_until[kRoomMaxW * kRoomMaxH];
    uint8_t  snap_cond[kRoomMaxW * kRoomMaxH];
    uint8_t  cell_timer[kRoomMaxW * kRoomMaxH];   // fire burn / lava cooling
    int8_t   cell_dir[kRoomMaxW * kRoomMaxH];     // lava flow direction
    uint8_t  cell_aux[kRoomMaxW * kRoomMaxH];     // fire: what's burning; water: electrified
    int32_t  react_tick;
    float    react_accum;
    struct Emitter { int32_t x, y; int8_t dir; int16_t left; } emitters[16];
    int32_t  emitter_count;
    float    push_timer;

    float   cam_x, cam_y;     // top-left of the view, world px
    bool    transitioning;
    float   trans_t;
    float   trans_from_x, trans_from_y;

    bool    dying;
    float   death_timer;
    float   fade;             // 0 = clear, 1 = black

    float   freeze;           // hit-stop
    float   shake;
    float   room_banner;      // seconds to show room name

    int32_t ent_count;
    Entity  ents[kMaxEntities];
    Particle parts[kMaxParticles];
    int32_t part_next;

    float   sim_accum;
    float   time;
    int32_t deaths;
    uint32_t rng;
};

// ---- Editor --------------------------------------------------------------

struct TileChange {
    int32_t x, y;
    uint8_t before, after;
    uint8_t before_from, before_until, after_from, after_until;
    uint8_t before_cond, after_cond;
};
struct RoomChange { int32_t idx; Room before, after; };
struct UndoOp     { int32_t tile_begin, tile_end, room_begin, room_end; };

constexpr int kMaxUndoOps     = 4096;
constexpr int kMaxTileChanges = 1 << 20;
constexpr int kMaxRoomChanges = 8192;

enum RoomDrag : uint8_t {
    RD_NONE,
    RD_CREATE,
    RD_MOVE,
    RD_RESIZE,
};

enum EdgeBits : uint8_t { EDGE_L = 1, EDGE_R = 2, EDGE_T = 4, EDGE_B = 8 };

struct Editor {
    float   cam_x, cam_y;     // world px at the screen's top-left
    float   zoom;             // screen px per world px
    int32_t brush;            // palette index
    uint8_t spike_dir;        // 0 = auto, else forced T_SPIKE_*
    bool    room_mode;
    bool    grid;
    bool    help;

    bool    painting;
    uint8_t paint_value;
    int32_t last_tx, last_ty;

    bool    rect_drag;
    bool    rect_erase;
    int32_t rect_tx, rect_ty;

    bool    panning;
    float   pan_mx, pan_my;

    uint8_t room_drag;
    int32_t drag_room;
    uint8_t drag_edges;
    int32_t drag_tx, drag_ty;
    Room    drag_orig;
    Room    drag_cur;
    bool    drag_valid;

    bool    dirty;
    char    status[96];
    float   status_timer;

    // Undo history.
    int32_t op_count;
    int32_t op_cursor;
    bool    op_open;
    UndoOp  ops[kMaxUndoOps];
    int32_t tile_used;
    int32_t room_used;
    TileChange tiles[kMaxTileChanges];
    RoomChange rooms[kMaxRoomChanges];

    uint8_t scratch[kRoomMaxW * kRoomMaxH];   // room-move copy buffer
    uint8_t scratch_from[kRoomMaxW * kRoomMaxH];
    uint8_t scratch_until[kRoomMaxW * kRoomMaxH];

    // Golf-hole tags painted with the brush, and the hole being previewed
    // (0 = show every tile regardless of tags).
    uint8_t hole_from;
    uint8_t hole_until;
    uint8_t preview_hole;
    uint8_t flag_brush;      // 0 = none, else flag 1..31 for the brush's condition / switch target
    bool    flag_not;        // brush condition is "present while flag NOT set"
    uint8_t scratch_cond[kRoomMaxW * kRoomMaxH];
};

// ---- Top level -----------------------------------------------------------

enum Mode : uint8_t { MODE_PLAY, MODE_EDIT };

constexpr int kMaxQuads = 1 << 18;

struct GameState {
    uint32_t        magic;
    uint32_t        version;
    uint64_t        size;
    EnginePlatform* platform;

    uint8_t  mode;
    uint64_t frame;
    AssetHandle atlas;   // assets/art/atlas.png, 0 = procedural fallback art
    float    real_time;

    // Latest frame's raw input (render reads window size / mouse from it).
    FrameInput in;
    Actions    act;

    World  world;
    Play   play;
    Editor ed;

    // Per-frame draw output.
    int32_t quad_count;
    bool    quad_overflow;
    Quad2D  quads[kMaxQuads];
};

// ---- Small helpers -------------------------------------------------------

inline int   imin(int a, int b) { return a < b ? a : b; }
inline int   imax(int a, int b) { return a > b ? a : b; }
inline float fminf_(float a, float b) { return a < b ? a : b; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float signf(float v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); }
inline float approach(float v, float target, float amount) {
    return v < target ? fminf_(v + amount, target) : fmaxf_(v - amount, target);
}
// Floor division for negative tile / chunk coordinates.
inline int floordiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

inline uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline uint32_t hash2(int x, int y) { return hash_u32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u); }

inline uint32_t with_alpha(uint32_t c, float a) {
    int ai = (int)(clampf(a, 0.0f, 1.0f) * (float)(c >> 24));
    return (c & 0x00FFFFFFu) | ((uint32_t)ai << 24);
}
inline uint32_t shade(uint32_t c, float k) {
    int r = (int)clampf((float)(c & 0xFF) * k, 0, 255);
    int g = (int)clampf((float)((c >> 8) & 0xFF) * k, 0, 255);
    int b = (int)clampf((float)((c >> 16) & 0xFF) * k, 0, 255);
    return rgba(r, g, b, (int)(c >> 24));
}

} // namespace hop
