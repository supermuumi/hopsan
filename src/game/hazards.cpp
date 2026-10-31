// Lair hazards: lasers (constant + two pulsing phases), water + sharks,
// conveyor belts, security drones. Placeholder art is drawn procedurally.
//
// Included before play.cpp; the few play.cpp functions used here are
// forward-declared below.

namespace hop {

bool solid_at(GameState& s, int tx, int ty);
bool boxes_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);

constexpr float kLaserPeriod   = 3.0f;   // pulsing lasers: full cycle
constexpr float kLaserOnTime   = 1.4f;   // ... of which this long lethal
constexpr float kLaserWarnTime = 0.45f;  // flicker before switching on
constexpr int   kLaserMaxLen   = 64;     // tiles
constexpr float kSharkSpeed    = 70.0f;
constexpr float kSharkChase    = 40.0f;  // vertical tracking speed
constexpr float kDroneSpeed    = 60.0f;
constexpr float kConveyorSpeed = 70.0f;
constexpr float kSwimSpeed     = 110.0f;
constexpr float kSwimAccel     = 700.0f;
constexpr float kSwimRise      = 25.0f;  // gentle float toward the surface

// ---- Water ---------------------------------------------------------------

// Water, or an open tile (plan piece, crystal, ...) placed inside a pool,
// i.e. with water on both sides.
bool tile_wet(const World& w, int tx, int ty) {
    uint8_t t = tile_get(w, tx, ty);
    if (tile_is_water(t)) return true;
    if (t == T_EMPTY || is_solid_tile(t) || t == T_ONEWAY || conveyor_dir(t) || tile_is_laser(t)) return false;
    return tile_is_water(tile_get(w, tx - 1, ty)) && tile_is_water(tile_get(w, tx + 1, ty));
}

bool water_at_px(const World& w, int px, int py) {
    return tile_wet(w, floordiv(px, kTile), floordiv(py, kTile));
}

// ---- Lasers --------------------------------------------------------------

// Emitters point away from the solid tile they're mounted on.
int laser_dir(GameState& s, int tx, int ty) {
    if (solid_at(s, tx - 1, ty)) return 0;   // mounted left, fires right
    if (solid_at(s, tx + 1, ty)) return 1;
    if (solid_at(s, tx, ty - 1)) return 2;   // mounted on ceiling, fires down
    if (solid_at(s, tx, ty + 1)) return 3;
    return 0;
}

void laser_step(int dir, int* dx, int* dy) {
    *dx = dir == 0 ? 1 : (dir == 1 ? -1 : 0);
    *dy = dir == 2 ? 1 : (dir == 3 ? -1 : 0);
}

// Beam length in world units, stopping at the first solid tile.
float laser_length(GameState& s, int tx, int ty, int dir) {
    int dx, dy;
    laser_step(dir, &dx, &dy);
    int n = 0;
    for (; n < kLaserMaxLen; ++n)
        if (solid_at(s, tx + dx * (n + 1), ty + dy * (n + 1))) break;
    return (float)(n * kTile);
}

// Beam rect (world). Thickness 4, centred on the emitter.
void laser_rect(const Entity& e, float* x, float* y, float* w, float* h, float thick = 4) {
    float px = (float)(e.tx * kTile), py = (float)(e.ty * kTile), L = e.x;
    float c = (kTile - thick) * 0.5f;
    switch (e.dir) {
        case 0: *x = px + kTile; *y = py + c; *w = L; *h = thick; break;
        case 1: *x = px - L;     *y = py + c; *w = L; *h = thick; break;
        case 2: *x = px + c; *y = py + kTile; *w = thick; *h = L; break;
        default:*x = px + c; *y = py - L;     *w = thick; *h = L; break;
    }
}

// Pulsing lasers cycle up to 35% faster as the golf round goes on.
float laser_period() { return kLaserPeriod * (1.0f - 0.35f * hecticness(g_state->play)); }

// 0 = off, 1 = warning flicker, 2 = lethal.
int laser_phase(const Entity& e, float t) {
    if (e.timer < 0) return 2;  // constant laser
    float period = laser_period();
    float on = kLaserOnTime * (period / kLaserPeriod);
    float u = std::fmod(t + e.timer * period * 0.5f, period);
    if (u < on) return 2;
    if (u > period - kLaserWarnTime) return 1;
    return 0;
}

// ---- Entity hooks (called from play.cpp) ---------------------------------

// Returns the entity kind for a hazard tile, or -1.
int hazard_kind(uint8_t t) {
    switch (t) {
        case T_LASER: case T_LASER_A: case T_LASER_B: return E_LASER;
        case T_SHARK:   return E_SHARK;
        case T_DRONE_H: return E_DRONE_H;
        case T_DRONE_V: return E_DRONE_V;
        default: return -1;
    }
}

void hazard_init(GameState& s, Entity& e, uint8_t t) {
    switch (e.kind) {
        case E_LASER:
            e.dir = (int8_t)laser_dir(s, e.tx, e.ty);
            e.timer = t == T_LASER ? -1.0f : (t == T_LASER_A ? 0.0f : 1.0f);   // phase
            e.x = laser_length(s, e.tx, e.ty, e.dir);
            break;
        case E_SHARK:
            e.x = (float)(e.tx * kTile - 4);
            e.y = (float)(e.ty * kTile + 3);
            break;
        case E_DRONE_H: case E_DRONE_V:
            e.x = (float)(e.tx * kTile + 2);
            e.y = (float)(e.ty * kTile + 3);
            break;
        default: break;
    }
}

bool drone_blocked(GameState& s, const Room& r, float x, float y) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int pts[4][2] = { { x0, y0 }, { x0 + 11, y0 }, { x0, y0 + 9 }, { x0 + 11, y0 + 9 } };
    for (auto& p : pts) {
        int tx = floordiv(p[0], kTile), ty = floordiv(p[1], kTile);
        if (tx < r.x || tx >= r.x + r.w || ty < r.y || ty >= r.y + r.h) return true;
        if (solid_at(s, tx, ty) || tile_is_spike(tile_get(s.world, tx, ty))) return true;
    }
    return false;
}

bool shark_fits(GameState& s, float x, float y) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const int pts[4][2] = { { x0, y0 }, { x0 + 23, y0 }, { x0, y0 + 9 }, { x0 + 23, y0 + 9 } };
    for (auto& p : pts) {
        int tx = floordiv(p[0], kTile), ty = floordiv(p[1], kTile);
        if (!tile_is_water(tile_get(s.world, tx, ty)) || solid_at(s, tx, ty)) return false;
    }
    return true;
}

void hazard_update(GameState& s, Entity& e, float dt) {
    Play& g = s.play;
    const Room& r = s.world.rooms[g.room];
    switch (e.kind) {
    case E_LASER:
        e.x = laser_length(s, e.tx, e.ty, e.dir);   // crumbles can open / close the path
        e.state = (uint8_t)laser_phase(e, g.time);
        break;
    case E_SHARK: {
        float hk = 1.0f + 0.4f * hecticness(g);
        float nx = e.x + (float)e.dir * kSharkSpeed * hk * dt;
        if (shark_fits(s, nx, e.y)) e.x = nx;
        else e.dir = (int8_t)-e.dir;
        // Drift toward the player if they're swimming nearby.
        const Player& p = g.p;
        float pcx = (float)p.x + kPlayerW * 0.5f, pcy = (float)p.y + kPlayerH * 0.5f;
        if (water_at_px(s.world, (int)pcx, (int)pcy) && std::fabs(pcx - (e.x + 12)) < 7 * kTile) {
            float ty = pcy - 5;
            float ny = e.y + signf(ty - e.y) * fminf_(kSharkChase * hk * dt, std::fabs(ty - e.y));
            if (shark_fits(s, e.x, ny)) e.y = ny;
        }
        break;
    }
    case E_DRONE_H: {
        float nx = e.x + (float)e.dir * kDroneSpeed * (1.0f + 0.5f * hecticness(g)) * dt;
        if (drone_blocked(s, r, nx, e.y)) e.dir = (int8_t)-e.dir;
        else e.x = nx;
        break;
    }
    case E_DRONE_V: {
        float ny = e.y + (float)e.dir * kDroneSpeed * (1.0f + 0.5f * hecticness(g)) * dt;
        if (drone_blocked(s, r, e.x, ny)) e.dir = (int8_t)-e.dir;
        else e.y = ny;
        break;
    }
    default: break;
    }
}

// Does this hazard kill the player right now?
bool hazard_touch(GameState& s, const Entity& e) {
    const Player& p = s.play.p;
    switch (e.kind) {
    case E_LASER: {
        if (e.state != 2 || e.x <= 0) return false;
        float x, y, w, h;
        laser_rect(e, &x, &y, &w, &h);
        return boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, (int)x, (int)y, (int)w, (int)h);
    }
    case E_SHARK:
        return boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, (int)e.x + 2, (int)e.y + 2, 20, 7);
    case E_DRONE_H: case E_DRONE_V:
        return boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, (int)e.x + 1, (int)e.y + 1, 10, 8);
    default:
        return false;
    }
}

// ---- Drawing -------------------------------------------------------------

void draw_laser_emitter(float px, float py, int dir) {
    uint32_t metal = rgba(70, 74, 88), dark = rgba(36, 38, 48), lens = rgba(255, 70, 80);
    // Housing hugging the wall, lens on the firing side.
    switch (dir) {
        case 0: wrect(px, py + 3, 10, 10, metal); wrect(px, py + 3, 3, 10, dark); wrect(px + 10, py + 5, 3, 6, lens); break;
        case 1: wrect(px + 6, py + 3, 10, 10, metal); wrect(px + 13, py + 3, 3, 10, dark); wrect(px + 3, py + 5, 3, 6, lens); break;
        case 2: wrect(px + 3, py, 10, 10, metal); wrect(px + 3, py, 10, 3, dark); wrect(px + 5, py + 10, 6, 3, lens); break;
        default:wrect(px + 3, py + 6, 10, 10, metal); wrect(px + 3, py + 13, 10, 3, dark); wrect(px + 5, py + 3, 6, 3, lens); break;
    }
}

void draw_laser_beam(const Entity& e, float t) {
    if (e.x <= 0) return;
    float x, y, w, h;
    if (e.state == 2) {
        laser_rect(e, &x, &y, &w, &h, 6);
        wrect(x, y, w, h, rgba(255, 40, 60, 90));          // glow
        laser_rect(e, &x, &y, &w, &h, 3);
        wrect(x, y, w, h, rgba(255, 70, 90));
        laser_rect(e, &x, &y, &w, &h, 1);
        wrect(x, y, w, h, rgba(255, 230, 230));            // hot core
    } else if (e.state == 1 && ((int)(t * 20) & 1)) {
        laser_rect(e, &x, &y, &w, &h, 1);
        wrect(x, y, w, h, rgba(255, 80, 90, 150));          // warning flicker
    }
}

void draw_shark(float x, float y, int dir, float t) {
    uint32_t body = rgba(110, 130, 150), belly = rgba(190, 200, 210), dark = rgba(70, 86, 104);
    bool r = dir > 0;
    float tailx = r ? x : x + 19;
    wrect(x + 2, y + 2, 20, 7, body);
    wrect(x + 3, y + 7, 18, 2, belly);
    wrect(x + 9, y - 1, 4, 3, dark);                       // dorsal fin
    float wag = ((int)(t * 6) & 1) ? 1.0f : 0.0f;
    wrect(tailx, y + 1 + wag, 3, 3, dark);                 // tail fluke
    wrect(tailx, y + 6 - wag, 3, 3, dark);
    wrect(r ? x + 18 : x + 4, y + 4, 2, 2, rgba(10, 10, 20)); // eye
    wrect(r ? x + 19 : x + 2, y + 7, 3, 1, rgba(255, 255, 255)); // teeth
}

void draw_drone(float x, float y, float t) {
    wrect(x, y + 2, 12, 7, rgba(60, 64, 76));
    wrect(x + 1, y + 3, 10, 5, rgba(96, 102, 118));
    bool blink = ((int)(t * 4) & 1) != 0;
    wrect(x + 4, y + 4, 4, 3, blink ? rgba(255, 60, 70) : rgba(150, 30, 40));
    float rw = ((int)(t * 30) & 1) ? 12.0f : 6.0f;           // rotor blur
    wrect(x + 6 - rw * 0.5f, y, rw, 1, rgba(200, 205, 220, 160));
    wrect(x + 5, y + 1, 2, 1, rgba(60, 64, 76));
}

void draw_conveyor(int tx, int ty, int dir, float t) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    wrect(px, py, kTile, kTile, rgba(52, 54, 66));
    wrect(px, py, kTile, 5, rgba(30, 30, 38));
    // Moving chevrons on the belt.
    float off = std::fmod(t * kConveyorSpeed, 8.0f);
    if (off < 0) off += 8;
    for (int i = -1; i < 3; ++i) {
        float cx = px + (float)i * 8 + (dir > 0 ? off : 8 - off);
        if (cx < px - 2 || cx > px + kTile - 2) continue;
        wrect(cx, py + 1, 2, 3, rgba(240, 190, 60));
    }
    // rollers
    wrect(px + 2, py + 8, 4, 4, rgba(90, 92, 106));
    wrect(px + 10, py + 8, 4, 4, rgba(90, 92, 106));
}

void hazard_draw(GameState& s, const Entity& e) {
    float t = s.play.time;
    float px = (float)(e.tx * kTile), py = (float)(e.ty * kTile);
    switch (e.kind) {
        case E_LASER:   draw_laser_beam(e, t); draw_laser_emitter(px, py, e.dir); break;
        case E_SHARK:   draw_shark(e.x, e.y, e.dir, t); break;
        case E_DRONE_H: case E_DRONE_V: draw_drone(e.x, e.y, t); break;
        default: break;
    }
}

// Water is drawn as a translucent layer in front of everything, with a
// lighter animated surface line.
void draw_water_front(const World& w, float t) {
    float wx0 = g_view.cam_x + (g_view.clip_x0 - g_view.ox) / g_view.scale;
    float wy0 = g_view.cam_y + (g_view.clip_y0 - g_view.oy) / g_view.scale;
    float wx1 = g_view.cam_x + (g_view.clip_x1 - g_view.ox) / g_view.scale;
    float wy1 = g_view.cam_y + (g_view.clip_y1 - g_view.oy) / g_view.scale;
    int tx0 = floordiv((int)wx0, kTile) - 1, ty0 = floordiv((int)wy0, kTile) - 1;
    int tx1 = floordiv((int)wx1, kTile) + 1, ty1 = floordiv((int)wy1, kTile) + 1;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (!tile_wet(w, tx, ty)) continue;
            float px = (float)(tx * kTile), py = (float)(ty * kTile);
            bool surface = !tile_wet(w, tx, ty - 1);
            if (surface) {
                float wave = std::floor(std::sin(t * 3.0f + (float)tx * 0.9f) * 1.0f + 0.5f);
                wrect(px, py + 3 + wave, kTile, kTile - 3 - wave, rgba(40, 110, 200, 110));
                wrect(px, py + 2 + wave, kTile, 1, rgba(150, 210, 255, 210));
            } else {
                wrect(px, py, kTile, kTile, rgba(40, 110, 200, 110));
            }
        }
    }
}

// Editor preview of hazards that are entities in play.
void draw_hazard_static(GameState& s, uint8_t t, int tx, int ty) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    switch (t) {
        case T_LASER: case T_LASER_A: case T_LASER_B: {
            Entity e{};
            e.kind = E_LASER; e.tx = tx; e.ty = ty;
            hazard_init(s, e, t);
            e.state = t == T_LASER ? 2 : 1;
            draw_laser_beam(e, 0.05f);   // even frame: warning flicker visible
            draw_laser_emitter(px, py, e.dir);
            // Pulse phase marker in the housing.
            if (t != T_LASER) wrect(px + 7, py + 7, 2, 2, t == T_LASER_A ? rgba(255, 220, 80) : rgba(120, 200, 255));
            break;
        }
        case T_SHARK:   draw_shark(px - 4, py + 3, 1, 0); break;
        case T_DRONE_H: draw_drone(px + 2, py + 3, 0); wrect(px, py + 14, kTile, 1, rgba(255, 255, 255, 60)); break;
        case T_DRONE_V: draw_drone(px + 2, py + 3, 0); wrect(px + 14, py, 1, kTile, rgba(255, 255, 255, 60)); break;
        default: break;
    }
}

} // namespace hop
