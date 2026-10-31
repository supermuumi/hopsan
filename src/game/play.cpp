// Play mode: player physics, room transitions, entities, hazards, death and
// respawn, camera, and the play-mode renderer.
//
// The simulation runs at a fixed 60 Hz (kStep). Player position is whole
// pixels plus a sub-pixel remainder, and movement is resolved one pixel at
// a time against the tile grid -- the classic precise-platformer setup.

namespace hop {

// ---- Tuning (world px, seconds) ------------------------------------------

constexpr float kMaxRun          = 160.0f;
constexpr float kRunAccel        = 1800.0f;
constexpr float kRunReduce       = 700.0f;
constexpr float kAirMult         = 0.65f;
constexpr float kGravity         = 1800.0f;
constexpr float kMaxFall         = 300.0f;
constexpr float kFastFall        = 460.0f;
constexpr float kHalfGravThresh  = 80.0f;
constexpr float kJumpSpeed       = -215.0f;
constexpr float kJumpHBoost      = 70.0f;
constexpr float kVarJumpTime     = 0.2f;
constexpr float kCoyoteTime      = 0.1f;
constexpr float kJumpBufferTime  = 0.1f;
constexpr float kWallJumpH       = 220.0f;
constexpr float kWallJumpForce   = 0.16f;
constexpr int   kWallJumpCheck   = 3;
constexpr float kWallSlideMax    = 70.0f;
constexpr float kDashSpeed       = 460.0f;
constexpr float kEndDashSpeed    = 300.0f;
constexpr float kDashTime        = 0.15f;
constexpr float kDashCooldown    = 0.2f;
constexpr float kDashBufferTime  = 0.08f;
constexpr float kSuperJumpH      = 340.0f;
constexpr float kClimbUp         = -90.0f;
constexpr float kClimbDown       = 160.0f;
constexpr float kStaminaMax      = 110.0f;
constexpr float kClimbUpCost     = 100.0f / 2.2f;   // per second
constexpr float kClimbStillCost  = 100.0f / 10.0f;  // per second
constexpr float kClimbJumpCost   = 110.0f / 4.0f;
constexpr float kClimbHopY       = -230.0f;
constexpr float kClimbHopX       = 100.0f;
constexpr float kStaminaWarn     = 20.0f;
constexpr float kSpringSpeed     = -500.0f;
constexpr float kCrumbleShake    = 0.45f;
constexpr float kCrumbleRespawn  = 2.0f;
constexpr float kCrystalRespawn  = 2.5f;
constexpr float kWalkerSpeed     = 32.0f;
constexpr float kTransitionTime  = 0.35f;
constexpr float kDeathTime       = 0.6f;

constexpr uint32_t kHairDash   = rgba(230, 72, 92);
constexpr uint32_t kHairNoDash = rgba(90, 160, 255);

// ---- Random + particles --------------------------------------------------

float frand(Play& g) {       // 0..1
    g.rng = g.rng * 1664525u + 1013904223u;
    return (float)(g.rng >> 8) / 16777216.0f;
}
float frand_range(Play& g, float lo, float hi) { return lo + (hi - lo) * frand(g); }

Particle& spawn_particle(Play& g, float x, float y, float vx, float vy,
                         float life, float size, uint32_t color, float gravity = 0, float drag = 0) {
    Particle& p = g.parts[g.part_next];
    g.part_next = (g.part_next + 1) % kMaxParticles;
    p.x = x; p.y = y; p.vx = vx; p.vy = vy;
    p.life = life; p.max_life = life;
    p.w = size; p.h = size;
    p.color = color; p.gravity = gravity; p.drag = drag;
    return p;
}

void burst(Play& g, float x, float y, int n, float speed, float life, uint32_t color, float size = 2) {
    for (int i = 0; i < n; ++i) {
        float a = (float)i / (float)n * 6.2831853f + frand(g) * 0.4f;
        float sp = speed * (0.6f + 0.4f * frand(g));
        spawn_particle(g, x, y, std::cos(a) * sp, std::sin(a) * sp, life * (0.7f + 0.3f * frand(g)),
                       size, color, 0, 4.0f);
    }
}

void dust(Play& g, float x, float y, int n, float dir_x = 0) {
    for (int i = 0; i < n; ++i) {
        spawn_particle(g, x + frand_range(g, -4, 4), y - frand_range(g, 0, 2),
                       dir_x * 40 + frand_range(g, -30, 30), frand_range(g, -30, -5),
                       frand_range(g, 0.25f, 0.45f), 2, rgba(230, 226, 240, 200), 0, 3.0f);
    }
}

void update_particles(Play& g, float dt) {
    for (int i = 0; i < kMaxParticles; ++i) {
        Particle& p = g.parts[i];
        if (p.life <= 0) continue;
        p.life -= dt;
        p.vy += p.gravity * dt;
        float k = 1.0f / (1.0f + p.drag * dt);
        p.vx *= k; p.vy *= k;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
    }
}

// ---- Collision -----------------------------------------------------------

Entity* entity_at_tile(Play& g, uint8_t kind, int tx, int ty) {
    for (int i = 0; i < g.ent_count; ++i) {
        Entity& e = g.ents[i];
        if (e.kind == kind && e.tx == tx && e.ty == ty) return &e;
    }
    return nullptr;
}

bool solid_at(GameState& s, int tx, int ty) {
    uint8_t t = tile_get(s.world, tx, ty);
    if (t == T_ROCK || t == T_BRICK) return true;
    if (t == T_CRUMBLE) {
        Entity* e = entity_at_tile(s.play, E_CRUMBLE, tx, ty);
        return !e || e->state != 2;
    }
    return false;
}

bool collide_tiles(GameState& s, int x, int y, int w, int h) {
    int tx0 = floordiv(x, kTile), tx1 = floordiv(x + w - 1, kTile);
    int ty0 = floordiv(y, kTile), ty1 = floordiv(y + h - 1, kTile);
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx)
            if (solid_at(s, tx, ty)) return true;
    return false;
}

// Room edges act as walls / ceiling where no other room continues the
// world. The bottom edge is open: falling out of the world kills you.
bool bounds_block(GameState& s, int x, int y, int w, int h) {
    Play& g = s.play;
    if (g.room < 0) return false;
    const Room& r = s.world.rooms[g.room];
    int rx0 = r.x * kTile, ry0 = r.y * kTile;
    int rx1 = rx0 + r.w * kTile, ry1 = ry0 + r.h * kTile;
    if (x >= rx0 && y >= ry0 && x + w <= rx1 && y + h <= ry1) return false;
    const int px[3] = { x, x + w / 2, x + w - 1 };
    const int py[3] = { y, y + h / 2, y + h - 1 };
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            int qx = px[i], qy = py[j];
            if (qx >= rx0 && qx < rx1 && qy >= ry0 && qy < ry1) continue;
            if (qy >= ry1) continue;
            if (room_at_px(s.world, qx, qy) < 0) return true;
        }
    }
    return false;
}

bool collide(GameState& s, int x, int y, int w = kPlayerW, int h = kPlayerH) {
    return collide_tiles(s, x, y, w, h) || bounds_block(s, x, y, w, h);
}

// Is the bottom pixel row of the box exactly the top row of a platform?
bool oneway_at(GameState& s, int x, int y, int w = kPlayerW, int h = kPlayerH) {
    if (s.play.p.drop_through > 0) return false;
    int by = y + h - 1;
    if (by - floordiv(by, kTile) * kTile != 0) return false;
    int ty = floordiv(by, kTile);
    for (int tx = floordiv(x, kTile); tx <= floordiv(x + w - 1, kTile); ++tx)
        if (tile_get(s.world, tx, ty) == T_ONEWAY) return true;
    return false;
}

bool boxes_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// ---- Rooms, entities, respawn --------------------------------------------

void build_entities(GameState& s, int room) {
    Play& g = s.play;
    g.ent_count = 0;
    if (room < 0) return;
    const Room& r = s.world.rooms[room];
    for (int ty = r.y; ty < r.y + r.h; ++ty) {
        for (int tx = r.x; tx < r.x + r.w; ++tx) {
            uint8_t t = tile_get(s.world, tx, ty);
            int kind = -1;
            switch (t) {
                case T_CRUMBLE:    kind = E_CRUMBLE; break;
                case T_SPRING:     kind = E_SPRING; break;
                case T_CRYSTAL:    kind = E_CRYSTAL; break;
                case T_CHECKPOINT: kind = E_CHECKPOINT; break;
                case T_WALKER:     kind = E_WALKER; break;
                default: break;
            }
            if (kind < 0) continue;
            if (g.ent_count >= kMaxEntities) return;
            Entity& e = g.ents[g.ent_count++];
            e = Entity{};
            e.kind = (uint8_t)kind;
            e.tx = tx; e.ty = ty;
            e.x = (float)(tx * kTile + 1);
            e.y = (float)(ty * kTile + 4);
            e.dir = (hash2(tx, ty) & 1) ? 1 : -1;
        }
    }
}

void snap_camera(GameState& s);

void place_player(GameState& s, int x, int y, int room) {
    Play& g = s.play;
    Player& p = g.p;
    int8_t facing = p.facing ? p.facing : 1;
    p = Player{};
    p.x = x; p.y = y;
    p.facing = facing;
    p.dashes = 1;
    p.stamina = kStaminaMax;
    p.squash_x = p.squash_y = 1;
    p.on_ground = true;
    g.room = room;
    build_entities(s, room);
    snap_camera(s);
}

// Player top-left for standing on the floor of tile (tx, ty).
void tile_spawn_pos(int tx, int ty, int* x, int* y) {
    *x = tx * kTile + (kTile - kPlayerW) / 2;
    *y = (ty + 1) * kTile - kPlayerH;
}

// Start a fresh run at the world's start tile (or the first room).
void play_start(GameState& s) {
    Play& g = s.play;
    int tx = 0, ty = 0, x = 0, y = 0, room = -1;
    if (find_start(s.world, &tx, &ty)) {
        tile_spawn_pos(tx, ty, &x, &y);
        room = room_at_tile(s.world, tx, ty);
    } else {
        for (int i = 0; i < s.world.room_count; ++i) {
            if (!s.world.rooms[i].used) continue;
            const Room& r = s.world.rooms[i];
            room = i;
            x = (r.x + r.w / 2) * kTile;
            y = (r.y + r.h / 2) * kTile;
            break;
        }
    }
    g.spawn_x = x; g.spawn_y = y; g.spawn_room = room;
    g.spawn_wants_ground = false;
    g.has_checkpoint = false;
    g.dying = false; g.fade = 0; g.transitioning = false;
    g.room_banner = 2.0f;
    place_player(s, x, y, room);
}

// Respawn where the current respawn rule says.
void respawn(GameState& s) {
    Play& g = s.play;
    int room = g.spawn_room;
    if (room < 0 || room >= s.world.room_count || !s.world.rooms[room].used)
        room = room_at_px(s.world, g.spawn_x + kPlayerW / 2, g.spawn_y + kPlayerH / 2);
    if (room < 0) { play_start(s); return; }
    place_player(s, g.spawn_x, g.spawn_y, room);
}

void die(GameState& s) {
    Play& g = s.play;
    if (g.dying) return;
    g.dying = true;
    g.death_timer = 0;
    g.deaths++;
    g.shake = 0.3f;
    float cx = (float)g.p.x + kPlayerW * 0.5f, cy = (float)g.p.y + kPlayerH * 0.5f;
    uint32_t hair = g.p.dashes > 0 ? kHairDash : kHairNoDash;
    burst(g, cx, cy, 18, 220, 0.6f, hair, 3);
    burst(g, cx, cy, 10, 120, 0.5f, rgba(255, 255, 255), 2);
}

// ---- Camera --------------------------------------------------------------

void camera_target(GameState& s, int room, float* out_x, float* out_y) {
    Play& g = s.play;
    float tx = (float)g.p.x + kPlayerW * 0.5f - kViewW * 0.5f;
    float ty = (float)g.p.y + kPlayerH * 0.5f - kViewH * 0.5f - 8.0f;
    if (room >= 0) {
        const Room& r = s.world.rooms[room];
        float rx0 = (float)(r.x * kTile), ry0 = (float)(r.y * kTile);
        float rw = (float)(r.w * kTile), rh = (float)(r.h * kTile);
        tx = rw <= kViewW ? rx0 + (rw - kViewW) * 0.5f : clampf(tx, rx0, rx0 + rw - kViewW);
        ty = rh <= kViewH ? ry0 + (rh - kViewH) * 0.5f : clampf(ty, ry0, ry0 + rh - kViewH);
    }
    *out_x = tx; *out_y = ty;
}

void snap_camera(GameState& s) {
    camera_target(s, s.play.room, &s.play.cam_x, &s.play.cam_y);
}

void update_camera(GameState& s, float dt) {
    Play& g = s.play;
    float tx, ty;
    camera_target(s, g.room, &tx, &ty);
    if (g.transitioning) {
        float t = clampf(g.trans_t, 0, 1);
        t = t * t * (3 - 2 * t);
        g.cam_x = g.trans_from_x + (tx - g.trans_from_x) * t;
        g.cam_y = g.trans_from_y + (ty - g.trans_from_y) * t;
        return;
    }
    float k = 1.0f - std::exp(-dt * 12.0f);
    g.cam_x += (tx - g.cam_x) * k;
    g.cam_y += (ty - g.cam_y) * k;
}

void enter_room(GameState& s, int room) {
    Play& g = s.play;
    Player& p = g.p;
    const Room& old = s.world.rooms[g.room];
    const Room& r = s.world.rooms[room];
    bool going_up = (p.y + kPlayerH / 2) < old.y * kTile;

    g.trans_from_x = g.cam_x;
    g.trans_from_y = g.cam_y;
    g.transitioning = true;
    g.trans_t = 0;
    g.room = room;

    // Push the player fully inside the new room so it can't bounce back.
    int rx0 = r.x * kTile, ry0 = r.y * kTile;
    int rx1 = rx0 + r.w * kTile, ry1 = ry0 + r.h * kTile;
    p.x = clampi(p.x, rx0, rx1 - kPlayerW);
    p.y = clampi(p.y, ry0, ry1 - kPlayerH);
    if (going_up) {
        // Help the player up onto whatever ledge is waiting: a full jump
        // that doesn't need the button held.
        if (p.state != PS_CLIMB) {
            p.state = PS_NORMAL;
            p.vy = fminf_(p.vy, kJumpSpeed);
            p.var_jump_timer = kVarJumpTime;
            p.var_jump_speed = p.vy;
            p.var_jump_auto = true;
        }
    }

    build_entities(s, room);

    bool update_spawn = g.respawn_mode == RESPAWN_ROOM_ENTRY || !g.has_checkpoint;
    if (update_spawn) {
        g.spawn_x = p.x; g.spawn_y = p.y; g.spawn_room = room;
        g.spawn_wants_ground = !p.on_ground || going_up;
        if (g.respawn_mode == RESPAWN_ROOM_ENTRY) g.has_checkpoint = false;
    }
    g.room_banner = 1.5f;
}

// ---- Player --------------------------------------------------------------

void squash(Player& p, float sx, float sy) { p.squash_x = sx; p.squash_y = sy; }

void do_jump(GameState& s, int mx) {
    Play& g = s.play;
    Player& p = g.p;
    p.jump_buffer = 0;
    p.coyote = 0;
    p.var_jump_timer = kVarJumpTime;
    p.vx += (float)mx * kJumpHBoost;
    p.vy = kJumpSpeed;
    p.var_jump_speed = p.vy;
    p.var_jump_auto = false;
    p.state = PS_NORMAL;
    squash(p, 0.7f, 1.3f);
    dust(g, (float)p.x + kPlayerW * 0.5f, (float)(p.y + kPlayerH), 4);
}

void do_wall_jump(GameState& s, int dir) {
    Play& g = s.play;
    Player& p = g.p;
    p.jump_buffer = 0;
    p.coyote = 0;
    p.var_jump_timer = kVarJumpTime;
    p.force_move_timer = kWallJumpForce;
    p.force_move_x = (int8_t)dir;
    p.vx = (float)dir * kWallJumpH;
    p.vy = kJumpSpeed;
    p.var_jump_speed = p.vy;
    p.var_jump_auto = false;
    p.facing = (int8_t)dir;
    p.state = PS_NORMAL;
    squash(p, 0.7f, 1.3f);
    float wx = dir > 0 ? (float)p.x : (float)(p.x + kPlayerW);
    dust(g, wx, (float)(p.y + kPlayerH * 0.7f), 4, (float)dir);
}

void start_dash(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;
    const Actions& a = s.act;
    float dx = (float)a.mx, dy = (float)a.my;
    if (dx == 0 && dy == 0) dx = (float)p.facing;
    if (dx != 0 && dy != 0) { dx *= 0.70710678f; dy *= 0.70710678f; }
    p.dashes--;
    p.state = PS_DASH;
    p.dash_timer = kDashTime;
    p.dash_cooldown = kDashCooldown;
    p.dash_buffer = 0;
    p.dash_dx = dx; p.dash_dy = dy;
    p.vx = dx * kDashSpeed;
    p.vy = dy * kDashSpeed;
    p.var_jump_timer = 0;
    p.trail_timer = 0;
    if (dx != 0) p.facing = dx > 0 ? 1 : -1;
    g.freeze = 0.05f;
    g.shake = fmaxf_(g.shake, 0.12f);
    burst(g, (float)p.x + kPlayerW * 0.5f, (float)p.y + kPlayerH * 0.5f, 8, 90, 0.3f, rgba(255, 255, 255, 220));
}

void move_x(GameState& s, float amount) {
    Player& p = s.play.p;
    p.rx += amount;
    int m = (int)std::lround(p.rx);
    if (m == 0) return;
    p.rx -= (float)m;
    int sgn = m > 0 ? 1 : -1;
    while (m != 0) {
        if (!collide(s, p.x + sgn, p.y)) { p.x += sgn; m -= sgn; continue; }
        // Dashing sideways into a ledge corner: pop up over it.
        bool corrected = false;
        if (p.state == PS_DASH && p.dash_dy <= 0) {
            for (int k = 1; k <= 4 && !corrected; ++k) {
                if (!collide(s, p.x + sgn, p.y - k)) {
                    p.y -= k; p.x += sgn; m -= sgn; corrected = true;
                }
            }
        }
        if (corrected) continue;
        p.vx = 0;
        p.rx = 0;
        break;
    }
}

void move_y(GameState& s, float amount) {
    Player& p = s.play.p;
    p.ry += amount;
    int m = (int)std::lround(p.ry);
    if (m == 0) return;
    p.ry -= (float)m;
    int sgn = m > 0 ? 1 : -1;
    while (m != 0) {
        int ny = p.y + sgn;
        bool hit = collide(s, p.x, ny) || (sgn > 0 && oneway_at(s, p.x, ny));
        if (!hit) { p.y = ny; m -= sgn; continue; }
        if (sgn < 0) {
            // Bumped a ceiling corner by a few pixels: slide around it.
            bool corrected = false;
            int pref = p.vx < 0 ? -1 : 1;
            for (int k = 1; k <= 4 && !corrected; ++k) {
                for (int d = 0; d < 2 && !corrected; ++d) {
                    int dir = d == 0 ? pref : -pref;
                    if (p.vx != 0 && dir != pref) continue;   // don't correct backwards
                    if (!collide(s, p.x + dir * k, ny)) {
                        p.x += dir * k; p.y = ny; m -= sgn; corrected = true;
                    }
                }
            }
            if (corrected) continue;
            p.vy = 0;
            p.var_jump_timer = 0;
            if (p.state == PS_DASH) p.dash_timer = fminf_(p.dash_timer, 0.0f);
        } else {
            p.vy = 0;
        }
        p.ry = 0;
        break;
    }
}

bool standing_on_oneway_only(GameState& s) {
    Player& p = s.play.p;
    return !collide(s, p.x, p.y + 1) && oneway_at(s, p.x, p.y + 1);
}

void trigger_crumbles_in_box(GameState& s, int x, int y, int w, int h) {
    Play& g = s.play;
    int tx0 = floordiv(x, kTile), tx1 = floordiv(x + w - 1, kTile);
    int ty0 = floordiv(y, kTile), ty1 = floordiv(y + h - 1, kTile);
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            Entity* e = entity_at_tile(g, E_CRUMBLE, tx, ty);
            if (e && e->state == 0) { e->state = 1; e->timer = kCrumbleShake; }
        }
}

void player_update(GameState& s, float dt) {
    Play& g = s.play;
    Player& p = g.p;
    const Actions& a = s.act;
    int mx = a.mx, my = a.my;

    p.was_on_ground = p.on_ground;
    p.on_ground = p.vy >= 0 && (collide(s, p.x, p.y + 1) || oneway_at(s, p.x, p.y + 1));

    p.jump_buffer      -= dt;
    p.dash_buffer      -= dt;
    p.dash_cooldown    -= dt;
    p.force_move_timer -= dt;
    p.drop_through     -= dt;
    p.var_jump_timer   -= dt;
    p.squash_x = approach(p.squash_x, 1.0f, dt * 4.0f);
    p.squash_y = approach(p.squash_y, 1.0f, dt * 4.0f);

    if (p.on_ground) {
        if (!p.was_on_ground && p.state != PS_CLIMB) {
            squash(p, 1.3f, 0.75f);
            dust(g, (float)p.x + kPlayerW * 0.5f, (float)(p.y + kPlayerH), 5);
        }
        p.coyote = kCoyoteTime;
        p.stamina = kStaminaMax;
        if (p.dash_cooldown <= 0 && p.state != PS_DASH) p.dashes = 1;
    } else {
        p.coyote -= dt;
    }

    if (p.force_move_timer > 0) mx = p.force_move_x;
    if (mx != 0 && p.state != PS_CLIMB) p.facing = (int8_t)mx;

    // Dash can start from any state.
    if (p.dash_buffer > 0 && p.dashes > 0 && p.dash_cooldown <= 0) start_dash(s);

    switch (p.state) {
    case PS_DASH: {
        p.dash_timer -= dt;
        p.vx = p.dash_dx * kDashSpeed;
        p.vy = p.dash_dy * kDashSpeed;
        p.trail_timer -= dt;
        if (p.trail_timer <= 0) {
            p.trail_timer = 0.035f;
            Particle& gp = spawn_particle(g, (float)p.x, (float)p.y, 0, 0, 0.25f, 0,
                                          with_alpha(kHairNoDash, 0.55f));
            gp.w = kPlayerW; gp.h = kPlayerH;
        }
        // Jumping out of a grounded dash keeps the dash speed: a long jump.
        if (p.jump_buffer > 0 && p.coyote > 0 && p.dash_dy >= 0) {
            float keep = p.dash_dx != 0 ? signf(p.dash_dx) * kSuperJumpH : 0.0f;
            p.state = PS_NORMAL;
            do_jump(s, 0);
            p.vx = keep;
            break;
        }
        if (p.dash_timer <= 0) {
            p.state = PS_NORMAL;
            p.vx = p.dash_dx * kEndDashSpeed;
            p.vy = p.dash_dy * kEndDashSpeed;
            if (p.vy < 0) p.vy *= 0.75f;
        }
        break;
    }

    case PS_CLIMB: {
        bool wall = collide_tiles(s, p.x + p.facing, p.y, kPlayerW, kPlayerH);
        if (!a.grab_held || p.stamina <= 0 || !wall) {
            p.state = PS_NORMAL;
            break;
        }
        if (p.jump_buffer > 0) {
            if (mx == -p.facing) {
                do_wall_jump(s, -p.facing);
            } else {
                p.stamina -= kClimbJumpCost;
                do_jump(s, 0);
            }
            break;
        }
        p.vx = 0;
        if (my < 0) {
            // Out of wall beside our head? We've reached the top: hop over.
            if (!collide_tiles(s, p.x + p.facing, p.y, kPlayerW, 6)) {
                p.state = PS_NORMAL;
                p.vy = kClimbHopY;
                p.vx = (float)p.facing * kClimbHopX;
                p.force_move_timer = 0.2f;
                p.force_move_x = p.facing;
                break;
            }
            p.vy = kClimbUp;
            p.stamina -= kClimbUpCost * dt;
        } else if (my > 0) {
            p.vy = kClimbDown;
        } else {
            p.vy = 0;
            p.stamina -= kClimbStillCost * dt;
        }
        break;
    }

    default: {
        // Grab a wall?
        if (a.grab_held && p.stamina > 0 && !(p.vy < 0 && p.var_jump_timer > 0)
            && collide_tiles(s, p.x + p.facing, p.y, kPlayerW, kPlayerH)) {
            p.state = PS_CLIMB;
            p.vx = 0;
            p.vy = 0;
            break;
        }

        float mult = p.on_ground ? 1.0f : kAirMult;
        float target = (float)mx * kMaxRun;
        if (std::fabs(p.vx) > kMaxRun && signf(p.vx) == (float)mx)
            p.vx = approach(p.vx, target, kRunReduce * mult * dt);
        else
            p.vx = approach(p.vx, target, kRunAccel * mult * dt);

        float max_fall = my > 0 ? kFastFall : kMaxFall;
        if (!p.on_ground && p.vy > 0 && mx != 0
            && collide_tiles(s, p.x + mx, p.y, kPlayerW, kPlayerH)) {
            max_fall = kWallSlideMax;
            p.slide_dust -= dt;
            if (p.slide_dust <= 0) {
                p.slide_dust = 0.08f;
                dust(g, mx > 0 ? (float)(p.x + kPlayerW) : (float)p.x, (float)(p.y + 4), 1);
            }
        }
        float grav = kGravity * ((std::fabs(p.vy) < kHalfGravThresh && a.jump_held) ? 0.5f : 1.0f);
        if (!p.on_ground || p.vy < 0) p.vy = approach(p.vy, max_fall, grav * dt);
        if (p.vy > max_fall) p.vy = approach(p.vy, max_fall, kGravity * dt);

        if (p.var_jump_timer > 0) {
            if (a.jump_held || p.var_jump_auto) p.vy = fminf_(p.vy, p.var_jump_speed);
            else p.var_jump_timer = 0;
        } else {
            p.var_jump_auto = false;
        }

        if (p.jump_buffer > 0) {
            if (p.on_ground && my > 0 && standing_on_oneway_only(s)) {
                p.drop_through = 0.2f;
                p.jump_buffer = 0;
                p.on_ground = false;
                p.coyote = 0;
            } else if (p.coyote > 0) {
                do_jump(s, mx);
            } else if (collide_tiles(s, p.x + kWallJumpCheck, p.y, kPlayerW, kPlayerH)) {
                do_wall_jump(s, -1);
            } else if (collide_tiles(s, p.x - kWallJumpCheck, p.y, kPlayerW, kPlayerH)) {
                do_wall_jump(s, +1);
            }
        }
        break;
    }
    }

    move_x(s, p.vx * dt);
    move_y(s, p.vy * dt);

    // Touching crumble blocks starts them falling.
    if (collide(s, p.x, p.y + 1) && p.vy >= 0) trigger_crumbles_in_box(s, p.x, p.y + kPlayerH, kPlayerW, 1);
    if (p.state == PS_CLIMB) trigger_crumbles_in_box(s, p.facing > 0 ? p.x + kPlayerW : p.x - 1, p.y, 1, kPlayerH);

    // Running animation phase.
    if (p.on_ground && std::fabs(p.vx) > 10) p.run_anim += dt * std::fabs(p.vx) / 14.0f;
    else p.run_anim = 0;
}

// ---- Entities + hazards --------------------------------------------------

bool walker_ground_at(GameState& s, int tx, int ty) {
    return solid_at(s, tx, ty) || tile_get(s.world, tx, ty) == T_ONEWAY;
}

void update_entities(GameState& s, float dt) {
    Play& g = s.play;
    Player& p = g.p;
    for (int i = 0; i < g.ent_count; ++i) {
        Entity& e = g.ents[i];
        switch (e.kind) {
        case E_CRUMBLE:
            if (e.state == 1) {
                e.timer -= dt;
                if (e.timer <= 0) {
                    e.state = 2;
                    e.timer = kCrumbleRespawn;
                    for (int k = 0; k < 6; ++k)
                        spawn_particle(g, (float)(e.tx * kTile) + frand(g) * 16, (float)(e.ty * kTile) + frand(g) * 16,
                                       frand_range(g, -30, 30), frand_range(g, -40, 0), 0.6f, 3,
                                       rgba(196, 156, 104), 600);
                }
            } else if (e.state == 2) {
                e.timer -= dt;
                if (e.timer <= 0 && !boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, e.tx * kTile, e.ty * kTile, kTile, kTile)) {
                    e.state = 0;
                }
            }
            break;
        case E_SPRING:
            e.timer = fmaxf_(0, e.timer - dt);
            break;
        case E_CRYSTAL:
            if (e.state == 1) {
                e.timer -= dt;
                if (e.timer <= 0) { e.state = 0; burst(g, e.tx * kTile + 8.0f, e.ty * kTile + 8.0f, 6, 40, 0.3f, rgba(150, 255, 190)); }
            }
            break;
        case E_WALKER: {
            int foot_y = (int)e.y + 12;
            int ftx0 = floordiv((int)e.x, kTile), ftx1 = floordiv((int)e.x + 13, kTile);
            int fty = floordiv(foot_y, kTile);
            bool grounded = (foot_y - fty * kTile == 0)
                && (walker_ground_at(s, ftx0, fty) || walker_ground_at(s, ftx1, fty));
            if (!grounded) {
                e.timer += dt;   // reuse as fall speed accumulator
                float vy = fminf_(e.timer * kGravity, kMaxFall);
                float ny = e.y + vy * dt;
                int nfoot = (int)ny + 12;
                int nty = floordiv(nfoot, kTile);
                if (nty > fty || (nfoot - nty * kTile) == 0) {
                    if (walker_ground_at(s, ftx0, nty) || walker_ground_at(s, ftx1, nty)) {
                        ny = (float)(nty * kTile - 12);
                        e.timer = 0;
                    }
                }
                e.y = ny;
                // Fell out of the room: gone.
                const Room& r = s.world.rooms[g.room];
                if (e.y > (r.y + r.h) * kTile + 32) e.kind = 0xFF;
                break;
            }
            e.timer = 0;
            float nx = e.x + (float)e.dir * kWalkerSpeed * dt;
            int front = e.dir > 0 ? (int)nx + 14 : (int)nx - 1;
            int ftx = floordiv(front, kTile);
            int body_ty = floordiv((int)e.y + 6, kTile);
            const Room& r = s.world.rooms[g.room];
            bool wall  = solid_at(s, ftx, body_ty) || tile_is_spike(tile_get(s.world, ftx, body_ty));
            bool ledge = !walker_ground_at(s, ftx, fty);
            bool edge  = ftx < r.x || ftx >= r.x + r.w;
            if (wall || ledge || edge) e.dir = (int8_t)-e.dir;
            else e.x = nx;
            break;
        }
        default: break;
        }
    }
}

void check_interactions(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;

    // Spikes: only lethal when moving into them, like Celeste.
    int tx0 = floordiv(p.x, kTile), tx1 = floordiv(p.x + kPlayerW - 1, kTile);
    int ty0 = floordiv(p.y, kTile), ty1 = floordiv(p.y + kPlayerH - 1, kTile);
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            uint8_t t = tile_get(s.world, tx, ty);
            if (!tile_is_spike(t)) continue;
            int bx = tx * kTile, by = ty * kTile, bw = kTile, bh = kTile;
            bool lethal = false;
            switch (t) {
                case T_SPIKE_U: by += 10; bh = 6; lethal = p.vy >= 0; break;
                case T_SPIKE_D: bh = 6;           lethal = p.vy <= 0; break;
                case T_SPIKE_L: bx += 10; bw = 6; lethal = p.vx >= 0; break;
                case T_SPIKE_R: bw = 6;           lethal = p.vx <= 0; break;
            }
            if (lethal && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, bx, by, bw, bh)) { die(s); return; }
        }
    }

    for (int i = 0; i < g.ent_count; ++i) {
        Entity& e = g.ents[i];
        int ex = e.tx * kTile, ey = e.ty * kTile;
        switch (e.kind) {
        case E_SPRING:
            if (p.vy >= 0 && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, ex + 1, ey + 8, 14, 8)) {
                p.state = PS_NORMAL;
                p.vy = kSpringSpeed;
                p.var_jump_timer = 0;
                p.dashes = 1;
                p.stamina = kStaminaMax;
                p.jump_buffer = 0;
                p.on_ground = false;
                p.coyote = 0;
                squash(p, 0.6f, 1.4f);
                e.timer = 0.25f;
                burst(g, ex + 8.0f, ey + 8.0f, 6, 80, 0.3f, rgba(255, 236, 140));
            }
            break;
        case E_CRYSTAL:
            if (e.state == 0 && p.dashes < 1
                && boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, ex + 3, ey + 3, 10, 10)) {
                p.dashes = 1;
                p.stamina = kStaminaMax;
                e.state = 1;
                e.timer = kCrystalRespawn;
                g.freeze = 0.05f;
                burst(g, ex + 8.0f, ey + 8.0f, 12, 120, 0.4f, rgba(150, 255, 190));
            }
            break;
        case E_CHECKPOINT:
            if (boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, ex, ey, kTile, kTile)) {
                bool already = g.has_checkpoint && g.cp_tx == e.tx && g.cp_ty == e.ty;
                if (!already) {
                    g.has_checkpoint = true;
                    g.cp_tx = e.tx; g.cp_ty = e.ty;
                    int sx, sy;
                    tile_spawn_pos(e.tx, e.ty, &sx, &sy);
                    g.spawn_x = sx; g.spawn_y = sy; g.spawn_room = g.room;
                    g.spawn_wants_ground = false;
                    burst(g, ex + 12.0f, ey + 4.0f, 14, 100, 0.5f, rgba(90, 220, 230));
                }
            }
            break;
        case E_WALKER:
            if (boxes_overlap(p.x, p.y, kPlayerW, kPlayerH, (int)e.x + 2, (int)e.y + 2, 10, 9)) {
                die(s);
                return;
            }
            break;
        default: break;
        }
    }
}

// Entered a room mid-air: once we land somewhere safe, respawn there.
void update_safe_spawn(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;
    if (!g.spawn_wants_ground || !p.on_ground || p.state == PS_DASH) return;
    if (g.spawn_room != g.room) return;
    int ty = floordiv(p.y + kPlayerH, kTile);
    for (int tx = floordiv(p.x, kTile); tx <= floordiv(p.x + kPlayerW - 1, kTile); ++tx) {
        uint8_t t = tile_get(s.world, tx, ty);
        if (t == T_CRUMBLE || t == T_SPRING) return;
    }
    // Not right next to spikes either.
    for (int y = floordiv(p.y, kTile) - 1; y <= ty; ++y)
        for (int x = floordiv(p.x, kTile) - 1; x <= floordiv(p.x + kPlayerW - 1, kTile) + 1; ++x)
            if (tile_is_spike(tile_get(s.world, x, y))) return;
    g.spawn_x = p.x; g.spawn_y = p.y;
    g.spawn_wants_ground = false;
}

void check_room_bounds(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;
    if (g.room < 0) return;
    const Room& r = s.world.rooms[g.room];
    int cx = p.x + kPlayerW / 2, cy = p.y + kPlayerH / 2;
    int rx0 = r.x * kTile, ry0 = r.y * kTile;
    int rx1 = rx0 + r.w * kTile, ry1 = ry0 + r.h * kTile;
    if (cx >= rx0 && cx < rx1 && cy >= ry0 && cy < ry1) return;
    int nr = room_at_px(s.world, cx, cy);
    if (nr >= 0 && nr != g.room) { enter_room(s, nr); return; }
    if (cy >= ry1) die(s);
}

// ---- Fixed step ----------------------------------------------------------

void sim_step(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;
    const float dt = kStep;

    // Latched presses become buffers so a press is never lost.
    if (s.act.jump_latched) { p.jump_buffer = kJumpBufferTime; s.act.jump_latched = false; }
    if (s.act.dash_latched) { p.dash_buffer = kDashBufferTime; s.act.dash_latched = false; }

    g.time += dt;
    g.shake = fmaxf_(0, g.shake - dt);
    g.room_banner = fmaxf_(0, g.room_banner - dt);
    update_particles(g, dt);

    if (g.freeze > 0) { g.freeze -= dt; return; }

    if (g.dying) {
        g.death_timer += dt;
        g.fade = clampf((g.death_timer - 0.3f) / 0.25f, 0, 1);
        if (g.death_timer >= kDeathTime) {
            g.dying = false;
            respawn(s);
        }
        return;
    }
    g.fade = approach(g.fade, 0, dt / 0.3f);

    if (g.room < 0) return;

    if (g.transitioning) {
        g.trans_t += dt / kTransitionTime;
        update_camera(s, dt);
        if (g.trans_t >= 1) g.transitioning = false;
        return;
    }

    update_entities(s, dt);
    player_update(s, dt);
    check_interactions(s);
    if (!g.dying) {
        update_safe_spawn(s);
        check_room_bounds(s);
    }
    update_camera(s, dt);
}

// ---- Input mapping -------------------------------------------------------

constexpr int SC_A = 4, SC_C = 6, SC_D = 7, SC_J = 13, SC_K = 14, SC_L = 15, SC_S = 22,
              SC_W = 26, SC_X = 27, SC_Z = 29, SC_SPACE = 44, SC_RIGHT = 79, SC_LEFT = 80,
              SC_DOWN = 81, SC_UP = 82, SC_LSHIFT = 225, SC_LCTRL = 224, SC_LALT = 226,
              SC_LGUI = 227, SC_RSHIFT = 229, SC_RCTRL = 228, SC_RGUI = 231;

// SDL_GamepadButton values.
constexpr int PAD_SOUTH = 0, PAD_EAST = 1, PAD_WEST = 2, PAD_NORTH = 3,
              PAD_LSHOULDER = 9, PAD_RSHOULDER = 10,
              PAD_UP = 11, PAD_DOWN = 12, PAD_LEFT = 13, PAD_RIGHT = 14;

void gather_actions(GameState& s, const FrameInput& in) {
    Actions& a = s.act;
    const bool* k = in.keys_held;
    const bool* kp = in.keys_pressed;
    int mx = 0, my = 0;
    if (k[SC_LEFT] || k[SC_A]) mx -= 1;
    if (k[SC_RIGHT] || k[SC_D]) mx += 1;
    if (k[SC_UP] || k[SC_W]) my -= 1;
    if (k[SC_DOWN] || k[SC_S]) my += 1;
    bool jump = k[SC_SPACE] || k[SC_C] || k[SC_K];
    bool jump_p = kp[SC_SPACE] || kp[SC_C] || kp[SC_K];
    bool dash_p = kp[SC_X] || kp[SC_J] || kp[SC_LSHIFT];
    bool grab = k[SC_Z] || k[SC_L] || k[SC_RSHIFT];

    if (in.pad_connected) {
        const float dz = 0.4f;
        float lx = in.pad_axes[0], ly = in.pad_axes[1];
        if (in.pad_held[PAD_LEFT] || lx < -dz) mx = -1;
        if (in.pad_held[PAD_RIGHT] || lx > dz) mx = 1;
        if (in.pad_held[PAD_UP] || ly < -dz) my = -1;
        if (in.pad_held[PAD_DOWN] || ly > dz) my = 1;
        jump   |= in.pad_held[PAD_SOUTH] || in.pad_held[PAD_NORTH];
        jump_p |= in.pad_pressed[PAD_SOUTH] || in.pad_pressed[PAD_NORTH];
        dash_p |= in.pad_pressed[PAD_WEST] || in.pad_pressed[PAD_EAST];
        grab   |= in.pad_held[PAD_LSHOULDER] || in.pad_held[PAD_RSHOULDER]
               || in.pad_axes[4] > 0.5f || in.pad_axes[5] > 0.5f;
    }
    a.mx = mx; a.my = my;
    a.jump_held = jump;
    a.grab_held = grab;
    if (jump_p) a.jump_latched = true;
    if (dash_p) a.dash_latched = true;
}

void play_tick(GameState& s, float dt) {
    Play& g = s.play;
    g.sim_accum += fminf_(dt, 0.1f);
    while (g.sim_accum >= kStep) {
        sim_step(s);
        g.sim_accum -= kStep;
    }
}

// ---- Rendering -----------------------------------------------------------

void draw_player(GameState& s) {
    Play& g = s.play;
    Player& p = g.p;
    if (g.dying) return;
    float cx = (float)p.x + kPlayerW * 0.5f;
    float by = (float)(p.y + kPlayerH);
    float sx = p.squash_x, sy = p.squash_y;
    auto pr = [&](float u, float v, float w, float h, uint32_t c) {
        wrect(cx + u * sx, by + v * sy, w * sx, h * sy, c);
    };
    uint32_t hair = p.dashes > 0 ? kHairDash : kHairNoDash;
    if (p.state == PS_CLIMB && p.stamina < kStaminaWarn && ((int)(g.time * 10) & 1)) hair = rgba(255, 255, 255);
    uint32_t cloak = rgba(64, 74, 128), legs = rgba(36, 36, 58), skin = rgba(244, 214, 186);
    float f = (float)p.facing;

    int step = p.run_anim > 0 ? ((int)p.run_anim & 1) : 0;
    pr(-4, -5 + (step ? 1.0f : 0.0f), 3, 5 - (step ? 1.0f : 0.0f), legs);
    pr(1, -5 + (step ? 0.0f : 1.0f), 3, 5 - (step ? 0.0f : 1.0f), legs);
    pr(-5, -11, 10, 7, cloak);
    pr(-5, -6, 10, 1, shade(cloak, 0.75f));
    pr(-4, -16, 8, 6, skin);
    pr(-5, -18, 10, 3, hair);
    // hair tail streams behind
    float tail_u = f > 0 ? -7.0f : 3.0f;
    float lift = p.vy < -40 ? 2.0f : (p.vy > 40 ? -1.0f : 0.0f);
    pr(tail_u, -17 + lift, 4, 4, hair);
    pr(f > 0 ? -8.0f : 5.0f, -15 + lift * 1.5f, 3, 3, hair);
    // eye
    pr(f > 0 ? 1.0f : -2.0f, -14, 1, 2, rgba(30, 24, 40));
    if (p.state == PS_CLIMB) pr(f > 0 ? 4.0f : -6.0f, -11, 2, 3, skin);   // hand on wall
}

void draw_particles(Play& g) {
    for (int i = 0; i < kMaxParticles; ++i) {
        const Particle& p = g.parts[i];
        if (p.life <= 0) continue;
        float a = p.life / p.max_life;
        if (p.w >= kPlayerW) {   // dash afterimage
            wrect(p.x, p.y, p.w, p.h, with_alpha(p.color, a));
            continue;
        }
        wrect(p.x - p.w * 0.5f, p.y - p.h * 0.5f, p.w, p.h, with_alpha(p.color, fminf_(1, a * 1.5f)));
    }
}

void draw_entities(GameState& s) {
    Play& g = s.play;
    for (int i = 0; i < g.ent_count; ++i) {
        const Entity& e = g.ents[i];
        float px = (float)(e.tx * kTile), py = (float)(e.ty * kTile);
        switch (e.kind) {
        case E_CRUMBLE:
            if (e.state == 0) draw_crumble(px, py, 1);
            else if (e.state == 1) {
                float j = ((int)(g.time * 40) & 1) ? 1.0f : -1.0f;
                draw_crumble(px + j, py, 1);
            } else draw_crumble(px, py, 0.15f);
            break;
        case E_SPRING:     draw_spring(px, py, e.timer > 0.15f ? 1.0f : 0.0f); break;
        case E_CRYSTAL:    draw_crystal(px, py, std::floor(std::sin(g.time * 3 + (float)e.tx) * 1.5f + 0.5f), e.state == 0); break;
        case E_CHECKPOINT: draw_checkpoint(px, py, g.has_checkpoint && g.cp_tx == e.tx && g.cp_ty == e.ty, g.time); break;
        case E_WALKER:     draw_walker(e.x, e.y, e.dir, g.time); break;
        default: break;
        }
    }
}

// Fill everything in the view that's outside room `ri` with the void colour.
void draw_void_outside(const World& w, int ri, uint32_t color) {
    const Room& r = w.rooms[ri];
    float x0 = (float)(r.x * kTile), y0 = (float)(r.y * kTile);
    float x1 = x0 + (float)(r.w * kTile), y1 = y0 + (float)(r.h * kTile);
    float big = 1e6f;
    wrect(-big, -big, 2 * big, y0 + big, color);
    wrect(-big, y1, 2 * big, big, color);
    wrect(-big, y0, x0 + big, y1 - y0, color);
    wrect(x1, y0, big, y1 - y0, color);
}

void render_play(GameState& s) {
    Play& g = s.play;
    const int W = s.in.window_w, H = s.in.window_h;
    int scale = (int)fmaxf_(1.0f, std::floor(fminf_((float)W / kViewW, (float)H / kViewH)));
    int vw = kViewW * scale, vh = kViewH * scale;
    int ox = (W - vw) / 2, oy = (H - vh) / 2;

    float shx = 0, shy = 0;
    if (g.shake > 0) {
        shx = (frand(g) < 0.5f ? -1.0f : 1.0f) * (g.shake > 0.15f ? 2.0f : 1.0f);
        shy = (frand(g) < 0.5f ? -1.0f : 1.0f) * (g.shake > 0.15f ? 2.0f : 1.0f);
    }
    g_view.cam_x = std::floor(g.cam_x + 0.5f) + shx;
    g_view.cam_y = std::floor(g.cam_y + 0.5f) + shy;
    g_view.scale = (float)scale;
    g_view.ox = (float)ox; g_view.oy = (float)oy;
    g_view.clip_x0 = (float)ox; g_view.clip_y0 = (float)oy;
    g_view.clip_x1 = (float)(ox + vw); g_view.clip_y1 = (float)(oy + vh);
    g_view.snap = true;

    // Sky.
    srect((float)ox, (float)oy, (float)vw, (float)vh * 0.5f, rgba(30, 28, 54));
    srect((float)ox, (float)oy + vh * 0.5f, (float)vw, (float)vh * 0.5f + 1, rgba(40, 36, 70));
    draw_parallax(0.2f, rgba(48, 44, 84), 90, 80, 7);
    draw_parallax(0.45f, rgba(36, 34, 64), 40, 70, 13);

    if (g.room >= 0 && !g.transitioning) draw_void_outside(s.world, g.room, rgba(12, 10, 20));

    draw_tiles(s.world, true, true);    // backdrop
    draw_tiles(s.world, true, false);   // solids, platforms, spikes
    draw_entities(s);
    draw_particles(g);
    draw_player(s);

    if (g.fade > 0) srect((float)ox, (float)oy, (float)vw, (float)vh, rgba(0, 0, 0, (int)(g.fade * 255)));

    // Letterbox.
    uint32_t bar = rgba(0, 0, 0);
    srect(0, 0, (float)W, (float)oy, bar);
    srect(0, (float)(oy + vh), (float)W, (float)(H - oy - vh), bar);
    srect(0, (float)oy, (float)ox, (float)vh, bar);
    srect((float)(ox + vw), (float)oy, (float)(W - ox - vw), (float)vh, bar);

    // HUD.
    char buf[96];
    int mins = (int)(g.time / 60), secs = (int)g.time % 60;
    std::snprintf(buf, sizeof(buf), "deaths %d   %d:%02d", g.deaths, mins, secs);
    text_sh((float)ox + 10 * g_ui, (float)oy + 18 * g_ui, rgba(230, 230, 240), font_small(), buf);
    if (g.room_banner > 0 && g.room >= 0) {
        float a = clampf(g.room_banner / 0.4f, 0, 1);
        const char* name = s.world.rooms[g.room].name;
        float tw = (float)std::strlen(name) * 11.0f * (float)g_ui;
        text_sh((float)ox + vw * 0.5f - tw * 0.5f, (float)oy + 44 * g_ui, with_alpha(rgba(255, 255, 255), a),
                font_title(), name);
    }
    text_sh((float)ox + 10 * g_ui, (float)(oy + vh) - 10 * g_ui, rgba(200, 200, 215, 160), font_small(),
            "Tab: editor   F1: console");
}

} // namespace hop
