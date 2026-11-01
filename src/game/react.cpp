// World flags, switches, the camera "peek", dark rooms, and elemental
// reactions (lava, fire, basalt, cauldrons, live cables, fuses).
//
// Reactions run as a small cellular simulation over the current room at a
// fixed 10 Hz, deterministically, so a retry plays out exactly the same.
// The room's tiles are snapshotted when it's built and restored on death
// and when you leave, so everything resets like the rest of the room.
//
// Included after hazards.cpp, before play.cpp; play.cpp functions used
// here are forward-declared.

namespace hop {

void die(GameState& s);
void build_entities(GameState& s, int room);
bool collide_tiles(GameState& s, int x, int y, int w, int h);
bool boxes_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
Particle& spawn_particle(Play& g, float x, float y, float vx, float vy,
                         float life, float size, uint32_t color, float gravity, float drag);
void burst(Play& g, float x, float y, int n, float speed, float life, uint32_t color, float size);
float frand_range(Play& g, float lo, float hi);

constexpr float kReactStep    = 0.1f;   // 10 Hz
constexpr int   kLavaCoolTicks = 30;    // a resting poured lava cell turns to basalt (3 s)
constexpr int   kThawTicks     = 3;     // per ice cell; the thaw spreads cell to cell
constexpr uint8_t kAuxPoured   = 1;     // cell_aux on lava: poured (cools); placed lava doesn't
constexpr int   kCauldronPour  = 30;    // lava cells poured by a tipped cauldron
constexpr float kPushToTip     = 0.25f; // seconds of pushing to tip a cauldron
constexpr float kPeekTime      = 2.4f;

// ---- Flags ---------------------------------------------------------------

bool flag_is_set(const Play& g, int f) { return f > 0 && ((g.flags >> f) & 1u); }

bool room_is_dark(const GameState& s, int ri) {
    if (ri < 0) return false;
    uint8_t d = s.world.rooms[ri].dark;
    if (d == 0) return false;
    if (d == 255) return true;
    return !flag_is_set(s.play, d);
}

// Number of tiles in room ri whose presence depends on flag f.
int room_flag_refs(const GameState& s, int ri, int f) {
    const Room& r = s.world.rooms[ri];
    int n = (r.dark == f) ? 10 : 0;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) {
            uint8_t c = tile_cond(s.world, x, y);
            if (c && cond_flag(c) == f && !cond_is_target(tile_raw(s.world, x, y))) ++n;
        }
    return n;
}

void progress_save(const GameState& s);

void flag_set(GameState& s, int f) {
    Play& g = s.play;
    if (f <= 0 || f > kMaxFlags || flag_is_set(g, f)) return;
    g.flags |= 1u << f;
    s.world.gate_flags = g.flags;
    progress_save(s);
    logf(LOG_LEVEL_INFO, "flag %s set", flag_label(s.world, f));
    // Things in this room may have appeared / vanished.
    if (g.room >= 0 && room_flag_refs(s, g.room, f) > 0) build_entities(s, g.room);
    // Show the room that changed the most (if it's elsewhere).
    int best = -1, best_n = 0;
    for (int i = 0; i < s.world.room_count; ++i) {
        if (!s.world.rooms[i].used || i == g.room) continue;
        int n = room_flag_refs(s, i, f);
        if (n > best_n) { best_n = n; best = i; }
    }
    if (best >= 0) {
        g.peek_room = best;
        g.peek_t = 0;
        g.peek_from_x = g.cam_x;
        g.peek_from_y = g.cam_y;
    }
}

// ---- Peek ----------------------------------------------------------------

void peek_camera(const GameState& s, float* cx, float* cy) {
    const Play& g = s.play;
    const Room& r = s.world.rooms[g.peek_room];
    float rw = (float)(r.w * kTile), rh = (float)(r.h * kTile);
    float tx = r.x * kTile + (rw - kViewW) * 0.5f, ty = r.y * kTile + (rh - kViewH) * 0.5f;
    float t = g.peek_t, u;
    if (t < 0.6f) u = t / 0.6f;
    else if (t < kPeekTime - 0.6f) u = 1;
    else u = 1.0f - (t - (kPeekTime - 0.6f)) / 0.6f;
    u = clampf(u, 0, 1);
    u = u * u * (3 - 2 * u);
    *cx = g.peek_from_x + (tx - g.peek_from_x) * u;
    *cy = g.peek_from_y + (ty - g.peek_from_y) * u;
}

// Returns true while a peek is running (the sim is paused).
bool peek_tick(GameState& s, float dt) {
    Play& g = s.play;
    if (g.peek_room < 0) return false;
    g.peek_t += dt;
    if (g.peek_t >= kPeekTime) g.peek_room = -1;
    return true;
}

// ---- Room snapshot -------------------------------------------------------

void react_snapshot(GameState& s, int ri) {
    Play& g = s.play;
    g.snap_valid = false;
    g.emitter_count = 0;
    g.react_tick = 0;
    g.react_accum = 0;
    g.push_timer = 0;
    if (ri < 0) return;
    const Room& r = s.world.rooms[ri];
    g.snap_x = r.x; g.snap_y = r.y; g.snap_w = r.w; g.snap_h = r.h;
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            int i = y * r.w + x;
            g.snap_t[i] = tile_raw(s.world, r.x + x, r.y + y);
            tile_tags(s.world, r.x + x, r.y + y, &g.snap_from[i], &g.snap_until[i]);
            g.snap_cond[i] = tile_cond(s.world, r.x + x, r.y + y);
            g.cell_timer[i] = 0;
            g.cell_dir[i] = 0;
            g.cell_aux[i] = 0;
        }
    g.snap_valid = true;
}

// Put the snapshotted room back exactly as it was built.
void react_restore(GameState& s) {
    Play& g = s.play;
    if (!g.snap_valid) return;
    for (int y = 0; y < g.snap_h; ++y)
        for (int x = 0; x < g.snap_w; ++x) {
            int i = y * g.snap_w + x;
            int wx = g.snap_x + x, wy = g.snap_y + y;
            uint8_t f, u;
            tile_tags(s.world, wx, wy, &f, &u);
            if (tile_raw(s.world, wx, wy) == g.snap_t[i] && f == g.snap_from[i] && u == g.snap_until[i]
                && tile_cond(s.world, wx, wy) == g.snap_cond[i]) continue;   // unchanged (tags included)
            tile_set_full(s.world, g.snap_x + x, g.snap_y + y, g.snap_t[i], g.snap_from[i], g.snap_until[i], g.snap_cond[i]);
        }
    g.snap_valid = false;
    g.emitter_count = 0;
}

// ---- Simulation ----------------------------------------------------------

int burn_ticks(uint8_t t) {
    switch (t) {
        case T_WOOD:   return 14;
        case T_FUSE:   return 3;
        case T_CABLE:  return 6;
        case T_ONEWAY: return 10;
        default:       return 5;   // plants
    }
}

struct Cells {
    GameState& s;
    int x0, y0, w, h;
    bool in(int x, int y) const { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }
    int idx(int x, int y) const { return (y - y0) * w + (x - x0); }
    // The sim sees the world as the player does: tiles hidden by their hole /
    // flag tags are empty space (and may be overwritten; the room snapshot
    // restores them).
    uint8_t raw(int x, int y) const { return in(x, y) ? tile_get(s.world, x, y) : (uint8_t)T_ROCK; }
    void put(int x, int y, uint8_t t, uint8_t cond = 0) { tile_set_full(s.world, x, y, t, 0, 0, cond); }
};

bool liquid_can_enter(uint8_t t) { return t == T_EMPTY || tile_is_decor(t); }
// Water also flows into fire (putting it out).
bool water_can_enter(uint8_t t) { return liquid_can_enter(t) || t == T_FIRE; }

void start_thaw(GameState& s, Cells& c, int x, int y) {
    if (!c.in(x, y) || c.raw(x, y) != T_ICE) return;
    uint8_t& tm = s.play.cell_timer[c.idx(x, y)];
    if (tm == 0) tm = 1;
}

// One flow pass for a liquid. Each cell falls if it can; otherwise keeps
// running in its flow direction; otherwise moves sideways if it's under
// pressure (more of the same liquid above it) or can pour over an edge;
// otherwise it rests. Resting water never moves again until something
// changes around it, so designed pools stay perfectly still.
void flow_pass(GameState& s, Cells& c, uint8_t kind, uint8_t* moved) {
    Play& g = s.play;
    auto can_enter = [&](int x, int y) {
        if (!c.in(x, y)) return false;
        uint8_t t = c.raw(x, y);
        return kind == T_WATER ? water_can_enter(t) : liquid_can_enter(t);
    };
    bool ltr = (g.react_tick & 1) != 0;
    for (int y = c.y0 + c.h - 1; y >= c.y0; --y) {
        for (int k = 0; k < c.w; ++k) {
            int x = ltr ? c.x0 + k : c.x0 + c.w - 1 - k;
            int i = c.idx(x, y);
            if (moved[i] || c.raw(x, y) != kind) continue;
            int8_t dir = g.cell_dir[i];
            int nx = x, ny = y;
            bool pressured = kind == T_WATER && c.in(x, y - 1) && c.raw(x, y - 1) == T_WATER;
            if (can_enter(x, y + 1)) ny = y + 1;
            else if (dir && can_enter(x + dir, y)) nx = x + dir;
            else {
                dir = 0;
                int first = ltr ? -1 : 1;
                for (int d = first, n = 0; n < 2 && nx == x; ++n, d = -d)
                    if (can_enter(x + d, y) && (pressured || can_enter(x + d, y + 1))) { nx = x + d; dir = (int8_t)d; }
                // Water levels out: a surface cell sets off along the surface if
                // there's a drop somewhere just ahead. (Always downhill, so it
                // can't oscillate.)
                if (kind == T_WATER && nx == x) {
                    for (int d = first, n = 0; n < 2 && nx == x; ++n, d = -d) {
                        for (int k = 1; k <= 6; ++k) {
                            if (!can_enter(x + k * d, y)) break;
                            if (can_enter(x + k * d, y + 1)) { if (k > 1) { nx = x + d; dir = (int8_t)d; } break; }
                            if (!(c.in(x + k * d, y + 1) && c.raw(x + k * d, y + 1) == T_WATER)) break;
                        }
                    }
                }
            }
            if (nx == x && ny == y) {
                g.cell_dir[i] = 0;
                if (kind == T_LAVA && g.cell_aux[i] == kAuxPoured && ++g.cell_timer[i] >= kLavaCoolTicks)
                    c.put(x, y, T_BASALT);
                continue;
            }
            uint8_t aux = kind == T_LAVA ? g.cell_aux[i] : 0;
            c.put(x, y, T_EMPTY);
            c.put(nx, ny, kind);
            int j = c.idx(nx, ny);
            g.cell_dir[j] = dir; g.cell_timer[j] = 0; g.cell_aux[j] = aux; moved[j] = 1;
            g.cell_dir[i] = 0; g.cell_timer[i] = 0; g.cell_aux[i] = 0;
        }
    }
}

// A tile tagged "present while flag N is NOT set" was burnt / melted away.
// Once the last tile of that group in the room is gone, set flag N: the
// group then stays gone for good, and anything tagged "present while flag N
// is set" appears permanently (e.g. a bridge). That's how a room keeps the
// result of a reaction after it resets.
void group_destroyed(GameState& s, Cells& c, uint8_t cond) {
    for (int y = c.y0; y < c.y0 + c.h; ++y)
        for (int x = c.x0; x < c.x0 + c.w; ++x)
            if (tile_cond(s.world, x, y) == cond && tile_get(s.world, x, y) != T_EMPTY) return;
    flag_set(s, cond_flag(cond));
}

void ignite(Cells& c, int x, int y) {
    if (!c.in(x, y)) return;
    uint8_t t = c.raw(x, y);
    if (!tile_is_flammable(t)) return;
    Play& g = c.s.play;
    int i = c.idx(x, y);
    g.cell_aux[i] = t;
    // Fuses remember the flag they set (+N). Anything tagged "present while
    // flag N is NOT set" remembers -N, so destroying it can set flag N.
    uint8_t cond = tile_cond(c.s.world, x, y);
    int8_t mark = 0;
    if (t == T_FUSE) mark = (int8_t)cond_flag(cond);
    else if (cond & kCondNot) mark = (int8_t)-cond_flag(cond);
    g.cell_dir[i] = mark;
    g.cell_timer[i] = (uint8_t)burn_ticks(t);
    c.put(x, y, T_FIRE);
}

void react_tick(GameState& s) {
    Play& g = s.play;
    if (!g.snap_valid || g.room < 0) return;
    Cells c{ s, g.snap_x, g.snap_y, g.snap_w, g.snap_h };
    const int n = c.w * c.h;
    static uint8_t moved[kRoomMaxW * kRoomMaxH];
    std::memset(moved, 0, (size_t)n);
    ++g.react_tick;

    // Cauldrons pouring.
    for (int k = 0; k < g.emitter_count; ++k) {
        Play::Emitter& em = g.emitters[k];
        int x = em.x + em.dir, y = em.y;
        if (c.in(x, y) && liquid_can_enter(c.raw(x, y))) {
            c.put(x, y, T_LAVA);
            int i = c.idx(x, y);
            g.cell_dir[i] = em.dir; g.cell_timer[i] = 0; g.cell_aux[i] = kAuxPoured; moved[i] = 1;
            --em.left;
        }
        if (em.left <= 0) g.emitters[k--] = g.emitters[--g.emitter_count];
    }

    // Liquids. Water moves twice per tick (it's runnier than lava).
    flow_pass(s, c, T_LAVA, moved);
    for (int pass = 0; pass < 2; ++pass) {
        std::memset(moved, 0, (size_t)n);
        flow_pass(s, c, T_WATER, moved);
    }

    // Reactions.
    const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int y = c.y0; y < c.y0 + c.h; ++y) {
        for (int x = c.x0; x < c.x0 + c.w; ++x) {
            uint8_t t = c.raw(x, y);
            int i = c.idx(x, y);
            if (t == T_LAVA) {
                bool quenched = false;
                for (auto& d : nb) if (tile_wet(s.world, x + d[0], y + d[1]) && c.in(x + d[0], y + d[1])) quenched = true;
                // Ice: the lava hardens and the ice starts to thaw.
                for (auto& d : nb)
                    if (c.in(x + d[0], y + d[1]) && c.raw(x + d[0], y + d[1]) == T_ICE) { quenched = true; start_thaw(s, c, x + d[0], y + d[1]); }
                if (quenched) {
                    c.put(x, y, T_BASALT);
                    for (int k = 0; k < 3; ++k)
                        spawn_particle(g, x * kTile + 8.0f, y * kTile + 4.0f, frand_range(g, -20, 20), frand_range(g, -60, -30),
                                       0.6f, 3, rgba(220, 220, 230, 160), -20, 2);
                    continue;
                }
                for (auto& d : nb) ignite(c, x + d[0], y + d[1]);
            } else if (t == T_FIRE) {
                bool doused = false;
                for (auto& d : nb) if (tile_wet(s.world, x + d[0], y + d[1]) && c.in(x + d[0], y + d[1])) doused = true;
                for (auto& d : nb) start_thaw(s, c, x + d[0], y + d[1]);
                uint8_t burn = (uint8_t)burn_ticks(g.cell_aux[i]);
                if (!doused && g.cell_timer[i] + 2 <= burn)
                    for (auto& d : nb) ignite(c, x + d[0], y + d[1]);
                if (doused || g.cell_timer[i] == 0 || --g.cell_timer[i] == 0) {
                    bool fuse = g.cell_aux[i] == T_FUSE;
                    int f = g.cell_dir[i];
                    c.put(x, y, T_EMPTY);
                    g.cell_aux[i] = 0; g.cell_dir[i] = 0;
                    spawn_particle(g, x * kTile + 8.0f, y * kTile + 8.0f, frand_range(g, -10, 10), -30, 0.8f, 3,
                                   rgba(70, 70, 76, 180), -10, 1);
                    if (fuse && !doused) flag_set(s, f);
                    if (!fuse && f < 0) group_destroyed(s, c, (uint8_t)(kCondNot | -f));
                }
            } else if (t == T_ICE && g.cell_timer[i] > 0) {
                // Thawing: turns to water, and the thaw spreads to touching ice.
                if (++g.cell_timer[i] >= kThawTicks + 1) {
                    uint8_t cond = tile_cond(s.world, x, y);
                    c.put(x, y, T_WATER);
                    if (cond & kCondNot) group_destroyed(s, c, cond);
                    g.cell_timer[i] = 0; g.cell_dir[i] = 0; g.cell_aux[i] = 0;
                    for (auto& d : nb) start_thaw(s, c, x + d[0], y + d[1]);
                    spawn_particle(g, x * kTile + 8.0f, y * kTile + 8.0f, frand_range(g, -15, 15), frand_range(g, -40, -10),
                                   0.5f, 2, rgba(210, 240, 255, 200), 120, 1);
                }
            }
        }
    }

    // Electricity: water touching a live cable is electrified, and the
    // charge spreads through the whole connected body of water.
    static int queue[kRoomMaxW * kRoomMaxH];
    int qh = 0, qt = 0;
    for (int i = 0; i < n; ++i) g.cell_aux[i] = (g.cell_aux[i] == 2) ? 0 : g.cell_aux[i];
    for (int y = c.y0; y < c.y0 + c.h; ++y)
        for (int x = c.x0; x < c.x0 + c.w; ++x) {
            if (tile_get(s.world, x, y) != T_CABLE) continue;
            for (auto& d : nb) {
                int ax = x + d[0], ay = y + d[1];
                if (!c.in(ax, ay) || !tile_wet(s.world, ax, ay)) continue;
                int j = c.idx(ax, ay);
                if (g.cell_aux[j] == 2) continue;
                g.cell_aux[j] = 2; queue[qt++] = j;
            }
        }
    while (qh < qt) {
        int j = queue[qh++];
        int x = c.x0 + j % c.w, y = c.y0 + j / c.w;
        for (auto& d : nb) {
            int ax = x + d[0], ay = y + d[1];
            if (!c.in(ax, ay) || !tile_wet(s.world, ax, ay)) continue;
            int k = c.idx(ax, ay);
            if (g.cell_aux[k] == 2) continue;
            g.cell_aux[k] = 2; queue[qt++] = k;
        }
    }
}

bool cell_electrified(const GameState& s, int tx, int ty) {
    const Play& g = s.play;
    if (!g.snap_valid) return false;
    int x = tx - g.snap_x, y = ty - g.snap_y;
    if (x < 0 || y < 0 || x >= g.snap_w || y >= g.snap_h) return false;
    return g.cell_aux[y * g.snap_w + x] == 2 && tile_wet(s.world, tx, ty);
}

void tip_cauldron(GameState& s, int tx, int ty, int dir) {
    Play& g = s.play;
    tile_set_full(s.world, tx, ty, T_CAULDRON_TIPPED, 0, 0, 0);
    if (g.snap_valid && tx >= g.snap_x && ty >= g.snap_y && tx < g.snap_x + g.snap_w && ty < g.snap_y + g.snap_h)
        g.cell_dir[(ty - g.snap_y) * g.snap_w + (tx - g.snap_x)] = (int8_t)dir;   // facing, for drawing
    if (g.emitter_count < 16) g.emitters[g.emitter_count++] = Play::Emitter{ tx, ty, (int8_t)dir, (int16_t)kCauldronPour };
    g.shake = fmaxf_(g.shake, 0.25f);
    burst(g, tx * kTile + 8.0f + dir * 8.0f, ty * kTile + 6.0f, 10, 120, 0.5f, rgba(255, 150, 40), 3);
}

// Per-step player interactions with switches, cauldrons and reaction hazards.
void react_player(GameState& s, float dt) {
    Play& g = s.play;
    Player& p = g.p;
    if (g.dying) return;
    int tx0 = floordiv(p.x, kTile), tx1 = floordiv(p.x + kPlayerW - 1, kTile);
    int ty0 = floordiv(p.y, kTile), ty1 = floordiv(p.y + kPlayerH - 1, kTile);
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            uint8_t t = tile_get(s.world, tx, ty);
            int bx = tx * kTile, by = ty * kTile;
            if (t == T_LAVA && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, bx, by + 3, kTile, kTile - 3)) { die(s); return; }
            if (t == T_FIRE && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, bx + 2, by + 3, kTile - 4, kTile - 3)) { die(s); return; }
            if (t == T_CABLE && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, bx + 1, by + 5, kTile - 2, 6)) { die(s); return; }
            if (t == T_SWITCH) {
                int f = cond_flag(tile_cond(s.world, tx, ty));
                if (!flag_is_set(g, f)) {
                    burst(g, bx + 8.0f, by + 8.0f, 12, 100, 0.5f, rgba(255, 230, 90), 2);
                    flag_set(s, f);
                }
            }
        }
    if (cell_electrified(s, floordiv(p.x + kPlayerW / 2, kTile), floordiv(p.y + kPlayerH / 2, kTile))) { die(s); return; }

    // Cauldrons: push into one for a moment, or dash into it, to tip it.
    int pushing = 0;
    if (p.state == PS_DASH && p.dash_dx != 0) pushing = p.dash_dx > 0 ? 1 : -1;
    else if (p.on_ground && s.act.mx != 0) pushing = s.act.mx;
    bool tipped = false;
    if (pushing) {
        int fx = pushing > 0 ? p.x + kPlayerW : p.x - 1;
        for (int ty = ty0; ty <= ty1 && !tipped; ++ty) {
            int tx = floordiv(fx, kTile);
            if (tile_get(s.world, tx, ty) != T_CAULDRON) continue;
            g.push_timer += dt;
            if (p.state == PS_DASH || g.push_timer >= kPushToTip) { tip_cauldron(s, tx, ty, pushing); tipped = true; }
            else if (((int)(g.push_timer * 30)) & 1) g.shake = fmaxf_(g.shake, 0.05f);   // wobble
            break;
        }
    }
    if (!pushing) g.push_timer = 0;
    if (tipped) g.push_timer = 0;
}

void react_update(GameState& s, float dt) {
    Play& g = s.play;
    g.react_accum += dt;
    while (g.react_accum >= kReactStep) {
        g.react_accum -= kReactStep;
        react_tick(s);
    }
}

// ---- Drawing -------------------------------------------------------------

float react_time() { return g_state->mode == MODE_PLAY ? g_state->play.time : g_state->real_time; }

void draw_lava(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile), t = react_time();
    bool top = tile_get(g_state->world, tx, ty - 1) != T_LAVA;
    wrect(px, py, kTile, kTile, rgba(220, 70, 20));
    wrect(px, py + 10, kTile, 6, rgba(180, 40, 20));
    if (top) {
        for (int i = 0; i < 4; ++i) {
            float wv = std::floor(std::sin(t * 4.0f + (float)(tx * 4 + i) * 0.8f) * 1.0f + 0.5f);
            wrect(px + i * 4, py + 1 + wv, 4, 3, rgba(255, 190, 60));
        }
    }
    uint32_t h = hash2(tx, ty);
    float bub = std::fmod(t * 1.3f + (float)(h & 7), 3.0f);
    if (bub < 0.4f) wrect(px + (float)(h % 11) + 2, py + 6, 2, 2, rgba(255, 230, 120));
}

void draw_fire(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile), t = react_time();
    wrect(px + 1, py + 10, 14, 6, rgba(70, 34, 20));
    for (int i = 0; i < 4; ++i) {
        float fl = 7 + 5 * std::fabs(std::sin(t * 9.0f + (float)(tx * 3 + ty + i) * 1.7f));
        wrect(px + 1 + i * 3.5f, py + 16 - fl, 3, fl, rgba(255, 110, 30));
        wrect(px + 1.5f + i * 3.5f, py + 16 - fl * 0.6f, 2, fl * 0.6f, rgba(255, 210, 80));
    }
}

void draw_wood(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    wrect(px, py, kTile, kTile, rgba(110, 70, 38));
    wrect(px + 1, py + 1, 14, 14, rgba(156, 104, 58));
    wrect(px + 1, py + 5, 14, 1, rgba(120, 78, 42));
    wrect(px + 1, py + 10, 14, 1, rgba(120, 78, 42));
    (void)ty;
}

void draw_basalt(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    wrect(px, py, kTile, kTile, rgba(52, 46, 58));
    uint32_t h = hash2(tx, ty);
    wrect(px + (float)(h & 7) + 2, py + (float)((h >> 3) & 7) + 3, 5, 1, rgba(82, 72, 92));
    wrect(px + (float)((h >> 6) & 7) + 4, py + (float)((h >> 9) & 7) + 4, 1, 4, rgba(82, 72, 92));
    if (!is_solid_tile(tile_get(g_state->world, tx, ty - 1)) && tile_get(g_state->world, tx, ty - 1) != T_BASALT)
        wrect(px, py, kTile, 1, rgba(96, 86, 108));
}

void draw_cauldron(int tx, int ty, bool tipped, int dir) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile), t = react_time();
    uint32_t iron = rgba(36, 36, 44), rim = rgba(110, 110, 124);
    if (!tipped) {
        wrect(px + 1, py + 5, 14, 9, iron);
        wrect(px, py + 4, 16, 2, rim);
        wrect(px + 2, py + 14, 2, 2, iron);
        wrect(px + 12, py + 14, 2, 2, iron);
        float b = ((int)(t * 3) & 1) ? 1.0f : 0.0f;
        wrect(px + 2, py + 5, 12, 2, rgba(255, 120, 30));
        wrect(px + 4 + b * 5, py + 3, 2, 2, rgba(255, 200, 80));
    } else {
        // On its side, mouth toward the pour direction.
        bool r = dir >= 0;
        wrect(px + 1, py + 7, 12, 9, iron);
        wrect(r ? px + 13 : px, py + 6, 3, 10, rim);
        wrect(r ? px + 14 : px, py + 9, 2, 6, rgba(255, 110, 30));
    }
}

void draw_line_tile(int tx, int ty, uint8_t t, uint32_t outer, uint32_t inner, float thick) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    const World& w = g_state->world;
    bool l = tile_get(w, tx - 1, ty) == t, r = tile_get(w, tx + 1, ty) == t;
    bool u = tile_get(w, tx, ty - 1) == t, d = tile_get(w, tx, ty + 1) == t;
    bool horiz = l || r || !(u || d);
    float c = (kTile - thick) * 0.5f;
    if (horiz) {
        float x0 = l ? px : px + 2, x1 = r ? px + kTile : px + kTile - 2;
        wrect(x0, py + c, x1 - x0, thick, outer);
        wrect(x0, py + c + thick * 0.3f, x1 - x0, thick * 0.4f, inner);
    }
    if (u || d) {
        float y0 = u ? py : py + c, y1 = d ? py + kTile : py + c + thick;
        wrect(px + c, y0, thick, y1 - y0, outer);
        wrect(px + c + thick * 0.3f, y0, thick * 0.4f, y1 - y0, inner);
    }
}

void draw_switch(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    int f = cond_flag(tile_cond(g_state->world, tx, ty));
    bool on = flag_is_set(g_state->play, f);
    wrect(px + 3, py + 12, 10, 4, rgba(90, 92, 104));
    wrect(px + 3, py + 12, 10, 1, rgba(140, 142, 156));
    // lever stick, leaning left (off) or right (on)
    for (int i = 0; i < 6; ++i) {
        float x = on ? px + 8 + i * 0.8f : px + 7 - i * 0.8f;
        wrect(x, py + 11 - i, 1.5f, 1, rgba(170, 170, 180));
    }
    float kx = on ? px + 11.5f : px + 1.5f;
    wrect(kx, py + 3, 3, 3, on ? rgba(90, 230, 110) : rgba(230, 70, 70));
}

void draw_ice(int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    const World& w = g_state->world;
    wrect(px, py, kTile, kTile, rgba(150, 205, 235));
    wrect(px + 1, py + 1, 14, 14, rgba(178, 224, 246));
    // diagonal glints
    for (int i = 0; i < 4; ++i) wrect(px + 3 + i, py + 9 - i, 1, 1, rgba(240, 250, 255));
    wrect(px + 10, py + 4, 1, 1, rgba(240, 250, 255));
    if (tile_get(w, tx, ty - 1) != T_ICE) wrect(px, py, kTile, 1, rgba(235, 248, 255));
    // thawing: cracks + drips
    const Play& g = g_state->play;
    if (g.snap_valid && tx >= g.snap_x && ty >= g.snap_y && tx < g.snap_x + g.snap_w && ty < g.snap_y + g.snap_h) {
        int tm = g.cell_timer[(ty - g.snap_y) * g.snap_w + (tx - g.snap_x)];
        if (tm > 0) {
            wrect(px + 4, py + 2, 1, 6, rgba(90, 140, 180));
            wrect(px + 5, py + 7, 4, 1, rgba(90, 140, 180));
            if (tm > 1) { wrect(px + 9, py + 7, 1, 6, rgba(90, 140, 180)); wrect(px + 7, py + 14, 2, 2, rgba(120, 180, 230)); }
        }
    }
}

void draw_react_tile(uint8_t t, int tx, int ty) {
    float time = react_time();
    switch (t) {
        case T_LAVA:   draw_lava(tx, ty); break;
        case T_FIRE:   draw_fire(tx, ty); break;
        case T_WOOD:   draw_wood(tx, ty); break;
        case T_BASALT: draw_basalt(tx, ty); break;
        case T_CAULDRON: draw_cauldron(tx, ty, false, 0); break;
        case T_CAULDRON_TIPPED: {
            const Play& g = g_state->play;
            int dir = 1;
            if (g.snap_valid && tx >= g.snap_x && ty >= g.snap_y && tx < g.snap_x + g.snap_w && ty < g.snap_y + g.snap_h)
                dir = g.cell_dir[(ty - g.snap_y) * g.snap_w + (tx - g.snap_x)] < 0 ? -1 : 1;
            draw_cauldron(tx, ty, true, dir);
            break;
        }
        case T_CABLE: {
            bool spark = ((int)(time * 12 + tx * 3 + ty) % 5) == 0;
            draw_line_tile(tx, ty, T_CABLE, rgba(30, 30, 34), spark ? rgba(255, 255, 200) : rgba(250, 210, 50), 4);
            break;
        }
        case T_FUSE:   draw_line_tile(tx, ty, T_FUSE, rgba(110, 80, 50), rgba(200, 160, 100), 2); break;
        case T_SWITCH: draw_switch(tx, ty); break;
        case T_ICE:    draw_ice(tx, ty); break;
        default: break;
    }
}

// Sparks dancing over electrified water.
void draw_electric(GameState& s) {
    const Play& g = s.play;
    if (!g.snap_valid) return;
    float t = g.time;
    for (int y = 0; y < g.snap_h; ++y)
        for (int x = 0; x < g.snap_w; ++x) {
            if (g.cell_aux[y * g.snap_w + x] != 2) continue;
            int tx = g.snap_x + x, ty = g.snap_y + y;
            float px = (float)(tx * kTile), py = (float)(ty * kTile);
            wrect(px, py, kTile, kTile, rgba(255, 240, 120, 30));
            uint32_t h = hash2(tx + (int)(t * 12), ty);
            if ((h & 3) == 0) {
                float sx = px + (float)(h % 13), sy = py + (float)((h >> 4) % 13);
                wrect(sx, sy, 3, 1, rgba(255, 255, 190, 220));
                wrect(sx + 2, sy + 1, 1, 2, rgba(255, 255, 190, 220));
            }
        }
}

// Lava, fire and live cables glow through darkness: redraw them on top.
void draw_glow_tiles(GameState& s) {
    const Play& g = s.play;
    if (!g.snap_valid) return;
    for (int y = 0; y < g.snap_h; ++y)
        for (int x = 0; x < g.snap_w; ++x) {
            int tx = g.snap_x + x, ty = g.snap_y + y;
            uint8_t t = tile_get(s.world, tx, ty);
            if (t == T_LAVA || t == T_FIRE || t == T_CABLE || t == T_SWITCH) draw_react_tile(t, tx, ty);
        }
}

// Darkness with a soft pool of light around the player.
void draw_darkness(float pcx, float pcy, float vx0, float vy0, float vx1, float vy1) {
    const float r1 = 70.0f, r2 = 100.0f, band = 2.0f;
    const uint32_t a1 = rgba(4, 4, 10, 140), a2 = rgba(4, 4, 10, 205);
    for (float y = vy0; y < vy1; y += band) {
        float dy = y + band * 0.5f - pcy;
        for (int layer = 0; layer < 2; ++layer) {
            float r = layer == 0 ? r1 : r2;
            uint32_t c = layer == 0 ? a1 : a2;
            if (std::fabs(dy) >= r) { wrect(vx0, y, vx1 - vx0, band, c); continue; }
            float hw = std::sqrt(r * r - dy * dy);
            wrect(vx0, y, (pcx - hw) - vx0, band, c);
            wrect(pcx + hw, y, vx1 - (pcx + hw), band, c);
        }
    }
}

} // namespace hop
