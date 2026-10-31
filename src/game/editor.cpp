// In-game level editor. Tab toggles between playing and editing.
//
// Tile mode (default)
//   LMB paint, RMB erase, Shift+drag fills / erases a rectangle,
//   Alt+click picks the tile under the cursor.
//   1-9, 0 pick a brush; Q / E cycle brushes; T cycles spike direction.
// Room mode (R)
//   Drag on empty space to create a room, drag a room to move it (its
//   tiles come along), drag an edge or corner to resize, Delete / X
//   removes the room under the cursor. N makes a one-screen room at the
//   cursor in either mode.
// Everywhere
//   WASD / arrows pan (Shift = faster), middle-drag pans, wheel zooms,
//   F frames the room under the cursor, G toggles the grid, H toggles help,
//   P plays from the cursor, Ctrl+Z / Ctrl+Y undo / redo, Ctrl+S saves.

namespace hop {

constexpr uint8_t kPalette[] = {
    T_ROCK, T_BRICK, T_BG, T_ONEWAY, T_SPIKE_U, T_CRUMBLE,
    T_SPRING, T_CRYSTAL, T_STAR, T_CHECKPOINT, T_WALKER, T_START,
    T_LASER, T_LASER_A, T_LASER_B, T_WATER, T_SHARK, T_CONVEYOR_L, T_CONVEYOR_R,
    T_DRONE_H, T_DRONE_V,
    T_DECOR_GRASS, T_DECOR_FERN, T_DECOR_SHROOMS, T_DECOR_RUBBLE,
    T_DECOR_VINES, T_DECOR_CHAIN, T_DECOR_TORCH, T_DECOR_PILLAR,
};
constexpr int kPaletteN = (int)(sizeof(kPalette) / sizeof(kPalette[0]));

constexpr int SC_E = 8, SC_F = 9, SC_G = 10, SC_H = 11, SC_N = 17, SC_P = 19, SC_Q = 20,
              SC_R = 21, SC_T = 23, SC_Y = 28, SC_1 = 30, SC_0 = 39, SC_ESCAPE = 41,
              SC_BACKSPACE = 42, SC_TAB = 43, SC_F2 = 59, SC_DELETE = 76;

void set_status(Editor& e, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(e.status, sizeof(e.status), fmt, ap);
    va_end(ap);
    e.status_timer = 3.0f;
}

// ---- Undo ----------------------------------------------------------------

void undo_clear(Editor& e) {
    e.op_count = e.op_cursor = 0;
    e.tile_used = e.room_used = 0;
    e.op_open = false;
}

void undo_begin(Editor& e) {
    if (e.op_open) return;
    e.op_count  = e.op_cursor;   // drop the redo tail
    e.tile_used = e.op_cursor > 0 ? e.ops[e.op_cursor - 1].tile_end : 0;
    e.room_used = e.op_cursor > 0 ? e.ops[e.op_cursor - 1].room_end : 0;
    if (e.op_count >= kMaxUndoOps || e.tile_used > kMaxTileChanges - (kRoomMaxW * kRoomMaxH * 2)
        || e.room_used > kMaxRoomChanges - 16) {
        undo_clear(e);
        set_status(e, "undo history full -- cleared");
    }
    UndoOp& op = e.ops[e.op_count];
    op.tile_begin = op.tile_end = e.tile_used;
    op.room_begin = op.room_end = e.room_used;
    e.op_open = true;
}

void undo_end(Editor& e) {
    if (!e.op_open) return;
    e.op_open = false;
    UndoOp& op = e.ops[e.op_count];
    op.tile_end = e.tile_used;
    op.room_end = e.room_used;
    if (op.tile_end == op.tile_begin && op.room_end == op.room_begin) return;
    e.op_count++;
    e.op_cursor = e.op_count;
    e.dirty = true;
}

void ed_set_tile(GameState& s, int tx, int ty, uint8_t t) {
    Editor& e = s.ed;
    uint8_t before = tile_get(s.world, tx, ty);
    if (before == t) return;
    if (e.op_open && e.tile_used < kMaxTileChanges)
        e.tiles[e.tile_used++] = TileChange{ tx, ty, before, t };
    tile_set(s.world, tx, ty, t);
    e.dirty = true;
}

void ed_set_room(GameState& s, int idx, const Room& after) {
    Editor& e = s.ed;
    if (idx >= s.world.room_count) s.world.room_count = idx + 1;
    if (e.op_open && e.room_used < kMaxRoomChanges)
        e.rooms[e.room_used++] = RoomChange{ idx, s.world.rooms[idx], after };
    s.world.rooms[idx] = after;
    e.dirty = true;
}

void undo(GameState& s) {
    Editor& e = s.ed;
    if (e.op_open || e.op_cursor == 0) { set_status(e, "nothing to undo"); return; }
    const UndoOp& op = e.ops[--e.op_cursor];
    for (int i = op.room_end - 1; i >= op.room_begin; --i) s.world.rooms[e.rooms[i].idx] = e.rooms[i].before;
    for (int i = op.tile_end - 1; i >= op.tile_begin; --i) {
        const TileChange& c = e.tiles[i];
        tile_set(s.world, c.x, c.y, c.before);
    }
    e.dirty = true;
    set_status(e, "undo (%d left)", e.op_cursor);
}

void redo(GameState& s) {
    Editor& e = s.ed;
    if (e.op_open || e.op_cursor >= e.op_count) { set_status(e, "nothing to redo"); return; }
    const UndoOp& op = e.ops[e.op_cursor++];
    for (int i = op.tile_begin; i < op.tile_end; ++i) {
        const TileChange& c = e.tiles[i];
        tile_set(s.world, c.x, c.y, c.after);
    }
    for (int i = op.room_begin; i < op.room_end; ++i) s.world.rooms[e.rooms[i].idx] = e.rooms[i].after;
    e.dirty = true;
    set_status(e, "redo");
}

// ---- Helpers -------------------------------------------------------------

float ed_scale_base(const GameState& s) {
    return fmaxf_(1.0f, std::floor(fminf_((float)s.in.window_w / kViewW, (float)s.in.window_h / kViewH)));
}

void screen_to_world(const Editor& e, float sx, float sy, float* wx, float* wy) {
    *wx = e.cam_x + sx / e.zoom;
    *wy = e.cam_y + sy / e.zoom;
}

void mouse_tile(const GameState& s, int* tx, int* ty) {
    float wx, wy;
    screen_to_world(s.ed, s.in.mouse_x, s.in.mouse_y, &wx, &wy);
    *tx = floordiv((int)std::floor(wx), kTile);
    *ty = floordiv((int)std::floor(wy), kTile);
}

// Shrinks to fit the window when the palette grows.
float palette_item_size() {
    float pad = 6.0f * (float)g_ui;
    float fit = ((float)g_state->in.window_w - 20.0f * g_ui) / (float)kPaletteN - pad;
    return fminf_(40.0f * (float)g_ui, fmaxf_(16.0f, fit));
}
float palette_bar_h()     { return palette_item_size() + 16.0f * (float)g_ui; }

bool mouse_over_palette(const GameState& s) {
    return s.in.mouse_y >= (float)s.in.window_h - palette_bar_h();
}

int palette_item_at(const GameState& s, float mx, float my) {
    float sz = palette_item_size(), pad = 6.0f * (float)g_ui;
    float total = kPaletteN * (sz + pad) - pad;
    float x0 = ((float)s.in.window_w - total) * 0.5f;
    float y0 = (float)s.in.window_h - palette_bar_h() + 8.0f * (float)g_ui;
    if (my < y0 || my > y0 + sz) return -1;
    for (int i = 0; i < kPaletteN; ++i) {
        float x = x0 + (float)i * (sz + pad);
        if (mx >= x && mx < x + sz) return i;
    }
    return -1;
}

uint8_t spike_for(GameState& s, int tx, int ty) {
    if (s.ed.spike_dir) return s.ed.spike_dir;
    const World& w = s.world;
    if (is_solid_tile(tile_get(w, tx, ty + 1))) return T_SPIKE_U;
    if (is_solid_tile(tile_get(w, tx, ty - 1))) return T_SPIKE_D;
    if (is_solid_tile(tile_get(w, tx + 1, ty))) return T_SPIKE_L;
    if (is_solid_tile(tile_get(w, tx - 1, ty))) return T_SPIKE_R;
    return T_SPIKE_U;
}

// Write the current brush (or erase) at one tile, respecting rooms.
void paint_tile(GameState& s, int tx, int ty, uint8_t value) {
    if (room_at_tile(s.world, tx, ty) < 0) {
        set_status(s.ed, "can't paint outside a room -- press R for room mode");
        return;
    }
    if (value == T_SPIKE_U) value = spike_for(s, tx, ty);
    if (value == T_START) {
        // Only one start tile per world.
        for (int i = 0; i < s.world.room_count; ++i) {
            const Room& r = s.world.rooms[i];
            if (!r.used) continue;
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x)
                    if ((x != tx || y != ty) && tile_get(s.world, x, y) == T_START) ed_set_tile(s, x, y, T_EMPTY);
        }
    }
    ed_set_tile(s, tx, ty, value);
}

void paint_line(GameState& s, int x0, int y0, int x1, int y1, uint8_t value) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; ++guard) {
        paint_tile(s, x0, y0, value);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int edges_under_mouse(const GameState& s, int* out_room) {
    const Editor& e = s.ed;
    float tol = 6.0f * (float)g_ui;
    float mx = s.in.mouse_x, my = s.in.mouse_y;
    for (int i = 0; i < s.world.room_count; ++i) {
        const Room& r = s.world.rooms[i];
        if (!r.used) continue;
        float x0 = (r.x * kTile - e.cam_x) * e.zoom, y0 = (r.y * kTile - e.cam_y) * e.zoom;
        float x1 = ((r.x + r.w) * kTile - e.cam_x) * e.zoom, y1 = ((r.y + r.h) * kTile - e.cam_y) * e.zoom;
        if (mx < x0 - tol || mx > x1 + tol || my < y0 - tol || my > y1 + tol) continue;
        int bits = 0;
        if (std::fabs(mx - x0) < tol) bits |= EDGE_L;
        if (std::fabs(mx - x1) < tol) bits |= EDGE_R;
        if (std::fabs(my - y0) < tol) bits |= EDGE_T;
        if (std::fabs(my - y1) < tol) bits |= EDGE_B;
        if (bits) { *out_room = i; return bits; }
    }
    *out_room = -1;
    return 0;
}

void frame_room(GameState& s, int ri) {
    Editor& e = s.ed;
    const Room& r = s.world.rooms[ri];
    float W = (float)s.in.window_w, H = (float)s.in.window_h - palette_bar_h();
    float rw = (float)(r.w * kTile), rh = (float)(r.h * kTile);
    e.zoom = fminf_(W / (rw + 32), H / (rh + 32));
    e.cam_x = r.x * kTile + rw * 0.5f - W * 0.5f / e.zoom;
    e.cam_y = r.y * kTile + rh * 0.5f - H * 0.5f / e.zoom;
}

void frame_world(GameState& s) {
    Editor& e = s.ed;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int i = 0; i < s.world.room_count; ++i) {
        const Room& r = s.world.rooms[i];
        if (!r.used) continue;
        x0 = imin(x0, r.x); y0 = imin(y0, r.y);
        x1 = imax(x1, r.x + r.w); y1 = imax(y1, r.y + r.h);
    }
    if (x1 <= x0) return;
    float W = (float)s.in.window_w, H = (float)s.in.window_h - palette_bar_h();
    float rw = (float)((x1 - x0) * kTile), rh = (float)((y1 - y0) * kTile);
    e.zoom = fminf_(W / (rw + 64), H / (rh + 64));
    e.cam_x = x0 * kTile + rw * 0.5f - W * 0.5f / e.zoom;
    e.cam_y = y0 * kTile + rh * 0.5f - H * 0.5f / e.zoom;
}

void editor_enter(GameState& s) {
    Editor& e = s.ed;
    s.mode = MODE_EDIT;
    if (e.zoom <= 0) e.zoom = ed_scale_base(s);
    // Centre on the player.
    float px = (float)s.play.p.x + kPlayerW * 0.5f, py = (float)s.play.p.y + kPlayerH * 0.5f;
    e.cam_x = px - (float)s.in.window_w * 0.5f / e.zoom;
    e.cam_y = py - (float)s.in.window_h * 0.5f / e.zoom;
    e.painting = e.rect_drag = e.panning = false;
    e.room_drag = RD_NONE;
}

// Leave the editor and resume play. If `at_cursor`, the player starts at
// the mouse cursor instead of where they were.
void editor_exit(GameState& s, bool at_cursor) {
    Editor& e = s.ed;
    undo_end(e);
    Play& g = s.play;
    if (at_cursor) {
        float wx, wy;
        screen_to_world(e, s.in.mouse_x, s.in.mouse_y, &wx, &wy);
        int x = (int)wx - kPlayerW / 2, y = (int)wy - kPlayerH / 2;
        int room = room_at_px(s.world, (int)wx, (int)wy);
        if (room < 0) { set_status(e, "cursor isn't inside a room"); return; }
        g.spawn_x = x; g.spawn_y = y; g.spawn_room = room;
        g.has_checkpoint = false;
        g.spawn_wants_ground = true;
        g.dying = false; g.fade = 0; g.transitioning = false;
        progress_rollback(s);   // a playtest jump doesn't keep this room's pickups
        place_player(s, x, y, room);
    } else {
        int room = room_at_px(s.world, g.p.x + kPlayerW / 2, g.p.y + kPlayerH / 2);
        if (room < 0) { play_start(s); }
        else {
            g.room = room;
            g.transitioning = false;
            build_entities(s, room);
            snap_camera(s);
        }
    }
    g.star_total = count_world_stars(s.world);
    // Swallow presses that happened while editing.
    s.act.jump_latched = s.act.dash_latched = false;
    s.mode = MODE_PLAY;
}

// ---- Tick ----------------------------------------------------------------

void editor_room_drag_update(GameState& s, int mtx, int mty) {
    Editor& e = s.ed;
    Room c = e.drag_orig;
    switch (e.room_drag) {
    case RD_CREATE:
        c = Room{};
        c.x = imin(e.drag_tx, mtx); c.y = imin(e.drag_ty, mty);
        c.w = std::abs(mtx - e.drag_tx) + 1; c.h = std::abs(mty - e.drag_ty) + 1;
        c.used = true;
        break;
    case RD_MOVE:
        c.x += mtx - e.drag_tx;
        c.y += mty - e.drag_ty;
        break;
    case RD_RESIZE: {
        const Room& o = e.drag_orig;
        int x0 = o.x, y0 = o.y, x1 = o.x + o.w, y1 = o.y + o.h;   // exclusive
        if (e.drag_edges & EDGE_L) x0 = imin(mtx, x1 - 1);
        if (e.drag_edges & EDGE_R) x1 = imax(mtx + 1, x0 + 1);
        if (e.drag_edges & EDGE_T) y0 = imin(mty, y1 - 1);
        if (e.drag_edges & EDGE_B) y1 = imax(mty + 1, y0 + 1);
        c.x = x0; c.y = y0; c.w = x1 - x0; c.h = y1 - y0;
        break;
    }
    default: break;
    }
    e.drag_cur = c;
    int ignore = e.room_drag == RD_CREATE ? -1 : e.drag_room;
    e.drag_valid = c.w >= 2 && c.h >= 2 && c.w <= kRoomMaxW && c.h <= kRoomMaxH
                && !room_overlaps_any(s.world, c, ignore);
}

void editor_room_drag_finish(GameState& s) {
    Editor& e = s.ed;
    const Room& c = e.drag_cur;
    uint8_t kind = e.room_drag;
    e.room_drag = RD_NONE;
    if (!e.drag_valid) {
        set_status(e, c.w < 2 || c.h < 2 ? "room too small" : "rooms can't overlap");
        return;
    }
    if (kind != RD_CREATE && std::memcmp(&c, &e.drag_orig, sizeof(Room)) == 0) return;
    undo_begin(e);
    if (kind == RD_CREATE) {
        int slot = room_alloc_slot(s.world);
        if (slot < 0) { set_status(e, "too many rooms"); undo_end(e); return; }
        Room nr = c;
        nr.used = true;
        std::snprintf(nr.name, sizeof(nr.name), "room_%d", slot);
        ed_set_room(s, slot, nr);
        set_status(e, "created %s (%dx%d)", nr.name, nr.w, nr.h);
    } else if (kind == RD_MOVE) {
        const Room& o = e.drag_orig;
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                e.scratch[y * kRoomMaxW + x] = tile_get(s.world, o.x + x, o.y + y);
                ed_set_tile(s, o.x + x, o.y + y, T_EMPTY);
            }
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x)
                ed_set_tile(s, c.x + x, c.y + y, e.scratch[y * kRoomMaxW + x]);
        ed_set_room(s, e.drag_room, c);
        set_status(e, "moved %s", c.name);
    } else {
        ed_set_room(s, e.drag_room, c);
        set_status(e, "resized %s to %dx%d", c.name, c.w, c.h);
    }
    undo_end(e);
}

void editor_delete_room(GameState& s, int ri) {
    Editor& e = s.ed;
    Room r = s.world.rooms[ri];
    undo_begin(e);
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) ed_set_tile(s, x, y, T_EMPTY);
    Room gone = r;
    gone.used = false;
    ed_set_room(s, ri, gone);
    undo_end(e);
    set_status(e, "deleted %s (Ctrl+Z to undo)", r.name);
}

void editor_new_screen_room(GameState& s, int tx, int ty) {
    Editor& e = s.ed;
    Room r{};
    r.x = tx; r.y = ty; r.w = kRoomDefW; r.h = kRoomDefH; r.used = true;
    if (room_overlaps_any(s.world, r, -1)) { set_status(e, "no space for a %dx%d room here", r.w, r.h); return; }
    int slot = room_alloc_slot(s.world);
    if (slot < 0) { set_status(e, "too many rooms"); return; }
    std::snprintf(r.name, sizeof(r.name), "room_%d", slot);
    undo_begin(e);
    ed_set_room(s, slot, r);
    undo_end(e);
    set_status(e, "created %s", r.name);
}

void editor_tick(GameState& s, float dt) {
    Editor& e = s.ed;
    const FrameInput& in = s.in;
    const bool* k = in.keys_held;
    const bool* kp = in.keys_pressed;
    bool ctrl  = k[SC_LCTRL] || k[SC_RCTRL] || k[SC_LGUI] || k[SC_RGUI];
    bool shift = k[SC_LSHIFT] || k[SC_RSHIFT];
    bool alt   = k[SC_LALT] || k[230];

    e.status_timer = fmaxf_(0, e.status_timer - dt);
    if (e.zoom <= 0) e.zoom = ed_scale_base(s);

    int mtx, mty;
    mouse_tile(s, &mtx, &mty);

    // ---- keyboard
    if (ctrl) {
        if (kp[SC_Z]) { if (shift) redo(s); else undo(s); }
        if (kp[SC_Y]) redo(s);
        if (kp[SC_S]) {
            if (world_save(s.world, kWorldPath)) { e.dirty = false; set_status(e, "saved %s", kWorldPath); }
            else set_status(e, "SAVE FAILED -- see console (F1)");
        }
    } else {
        float pan = 700.0f * (shift ? 2.5f : 1.0f) / e.zoom * dt;
        if (k[SC_A] || k[SC_LEFT])  e.cam_x -= pan;
        if (k[SC_D] || k[SC_RIGHT]) e.cam_x += pan;
        if (k[SC_W] || k[SC_UP])    e.cam_y -= pan;
        if (k[SC_S] || k[SC_DOWN])  e.cam_y += pan;

        for (int i = 0; i < 10; ++i) {
            int sc = i == 9 ? SC_0 : SC_1 + i;
            if (kp[sc] && i < kPaletteN) e.brush = i;
        }
        if (kp[SC_Q]) e.brush = (e.brush + kPaletteN - 1) % kPaletteN;
        if (kp[SC_E]) e.brush = (e.brush + 1) % kPaletteN;
        if (kp[SC_T]) {
            static const uint8_t cycle[5] = { 0, T_SPIKE_U, T_SPIKE_R, T_SPIKE_D, T_SPIKE_L };
            int idx = 0;
            for (int i = 0; i < 5; ++i) if (cycle[i] == e.spike_dir) idx = i;
            e.spike_dir = cycle[(idx + 1) % 5];
            set_status(e, "spikes: %s", e.spike_dir ? kTileInfo[e.spike_dir].name : "auto-orient");
            for (int i = 0; i < kPaletteN; ++i) if (kPalette[i] == T_SPIKE_U) e.brush = i;
        }
        if (kp[SC_G]) e.grid = !e.grid;
        if (kp[SC_H] || kp[SC_F2]) e.help = !e.help;
        if (kp[SC_R]) {
            e.room_mode = !e.room_mode;
            e.painting = e.rect_drag = false;
            e.room_drag = RD_NONE;
            undo_end(e);
            set_status(e, e.room_mode ? "room mode" : "tile mode");
        }
        if (kp[SC_N]) editor_new_screen_room(s, mtx, mty);
        if (kp[SC_F]) {
            int ri = room_at_tile(s.world, mtx, mty);
            if (ri >= 0) frame_room(s, ri); else frame_world(s);
        }
        if (kp[SC_ESCAPE]) {
            if (e.room_drag != RD_NONE) { e.room_drag = RD_NONE; set_status(e, "cancelled"); }
            if (e.rect_drag) { e.rect_drag = false; set_status(e, "cancelled"); }
        }
        if (e.room_mode && (kp[SC_DELETE] || kp[SC_BACKSPACE] || kp[SC_X])) {
            int ri = room_at_tile(s.world, mtx, mty);
            if (ri >= 0) editor_delete_room(s, ri);
        }
        if (kp[SC_P]) { editor_exit(s, true); return; }
    }

    // ---- zoom about the cursor
    if (in.mouse_wheel_y != 0) {
        float wx0, wy0;
        screen_to_world(e, in.mouse_x, in.mouse_y, &wx0, &wy0);
        float base = ed_scale_base(s);
        e.zoom = clampf(e.zoom * std::pow(1.2f, in.mouse_wheel_y), base * 0.04f, base * 6.0f);
        e.cam_x = wx0 - in.mouse_x / e.zoom;
        e.cam_y = wy0 - in.mouse_y / e.zoom;
    }

    // ---- middle-drag pan
    if (in.mouse_pressed[1]) { e.panning = true; e.pan_mx = in.mouse_x; e.pan_my = in.mouse_y; }
    if (e.panning) {
        if (!in.mouse_down[1]) e.panning = false;
        else {
            e.cam_x -= (in.mouse_x - e.pan_mx) / e.zoom;
            e.cam_y -= (in.mouse_y - e.pan_my) / e.zoom;
            e.pan_mx = in.mouse_x; e.pan_my = in.mouse_y;
        }
    }

    // ---- palette clicks
    bool over_palette = mouse_over_palette(s);
    if (over_palette && in.mouse_pressed[0] && !e.painting && !e.rect_drag && e.room_drag == RD_NONE) {
        int item = palette_item_at(s, in.mouse_x, in.mouse_y);
        if (item >= 0) { e.brush = item; if (e.room_mode) { e.room_mode = false; set_status(e, "tile mode"); } }
        return;
    }

    // ---- room mode
    if (e.room_mode) {
        if (e.room_drag == RD_NONE && in.mouse_pressed[0] && !over_palette) {
            int edge_room = -1;
            int edges = edges_under_mouse(s, &edge_room);
            int ri = room_at_tile(s.world, mtx, mty);
            e.drag_tx = mtx; e.drag_ty = mty;
            if (edges) {
                e.room_drag = RD_RESIZE; e.drag_room = edge_room; e.drag_edges = (uint8_t)edges;
                e.drag_orig = s.world.rooms[edge_room];
            } else if (ri >= 0) {
                e.room_drag = RD_MOVE; e.drag_room = ri;
                e.drag_orig = s.world.rooms[ri];
            } else {
                e.room_drag = RD_CREATE; e.drag_room = -1;
            }
        }
        if (e.room_drag != RD_NONE) {
            editor_room_drag_update(s, mtx, mty);
            if (!in.mouse_down[0]) editor_room_drag_finish(s);
        }
        return;
    }

    // ---- tile mode
    if (e.rect_drag) {
        bool held = e.rect_erase ? in.mouse_down[2] : in.mouse_down[0];
        if (!held) {
            e.rect_drag = false;
            uint8_t v = e.rect_erase ? (uint8_t)T_EMPTY : kPalette[e.brush];
            int x0 = imin(e.rect_tx, mtx), x1 = imax(e.rect_tx, mtx);
            int y0 = imin(e.rect_ty, mty), y1 = imax(e.rect_ty, mty);
            if ((x1 - x0 + 1) * (y1 - y0 + 1) > kRoomMaxW * kRoomMaxH) {
                set_status(e, "rectangle too large");
            } else {
                undo_begin(e);
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        if (room_at_tile(s.world, x, y) >= 0)
                            ed_set_tile(s, x, y, v == T_SPIKE_U ? spike_for(s, x, y) : v);
                undo_end(e);
            }
        }
        return;
    }

    if (!e.painting && !over_palette) {
        bool lp = in.mouse_pressed[0], rp = in.mouse_pressed[2];
        if ((lp || rp) && alt) {
            uint8_t t = tile_get(s.world, mtx, mty);
            if (tile_is_spike(t)) { e.spike_dir = t; t = T_SPIKE_U; }
            for (int i = 0; i < kPaletteN; ++i) if (kPalette[i] == t) e.brush = i;
            set_status(e, "picked %s", kTileInfo[tile_get(s.world, mtx, mty)].name);
        } else if ((lp || rp) && shift) {
            e.rect_drag = true;
            e.rect_erase = rp;
            e.rect_tx = mtx; e.rect_ty = mty;
        } else if (lp || rp) {
            e.painting = true;
            e.paint_value = lp ? kPalette[e.brush] : (uint8_t)T_EMPTY;
            e.last_tx = mtx; e.last_ty = mty;
            undo_begin(e);
            paint_tile(s, mtx, mty, e.paint_value);
        }
    } else if (e.painting) {
        bool held = e.paint_value == T_EMPTY ? in.mouse_down[2] : in.mouse_down[0];
        if (!held) {
            e.painting = false;
            undo_end(e);
        } else if (mtx != e.last_tx || mty != e.last_ty) {
            paint_line(s, e.last_tx, e.last_ty, mtx, mty, e.paint_value);
            e.last_tx = mtx; e.last_ty = mty;
        }
    }
}

// ---- Render --------------------------------------------------------------

void draw_palette_item(int i, float x, float y, float sz, bool selected, uint8_t spike_dir) {
    srect(x, y, sz, sz, selected ? rgba(80, 76, 120) : rgba(36, 34, 54));
    if (selected) sframe(x - 2, y - 2, sz + 4, sz + 4, 2, rgba(255, 220, 120));
    else sframe(x, y, sz, sz, 1, rgba(70, 68, 96));
    // Draw the tile art into the box through a temporary view at a spot
    // of the world that's guaranteed empty, so neighbour checks see air.
    View saved = g_view;
    float inner = sz * 0.7f;
    const int far_tx = -1000000, far_ty = -1000000;
    g_view.scale = inner / kTile;
    g_view.cam_x = (float)(far_tx * kTile);
    g_view.cam_y = (float)(far_ty * kTile);
    g_view.ox = x + (sz - inner) * 0.5f;
    g_view.oy = y + (sz - inner) * 0.5f;
    g_view.clip_x0 = x; g_view.clip_y0 = y; g_view.clip_x1 = x + sz; g_view.clip_y1 = y + sz;
    g_view.snap = false;
    uint8_t t = kPalette[i];
    if (t == T_SPIKE_U && spike_dir) t = spike_dir;
    draw_tile_static(g_state->world, t, far_tx, far_ty, 1.0f);
    if (t == T_ROCK && have_art()) draw_moss_art(far_tx, (float)(far_tx * kTile), (float)(far_ty * kTile));
    g_view = saved;
    char num[4];
    std::snprintf(num, sizeof(num), "%d", (i + 1) % 10);
    if (i < 10) text(x + 3 * g_ui, y + 12 * g_ui, rgba(255, 255, 255, 200), font_small(), "%s", num);
}

void render_editor(GameState& s) {
    Editor& e = s.ed;
    const float W = (float)s.in.window_w, H = (float)s.in.window_h;
    if (e.zoom <= 0) e.zoom = ed_scale_base(s);

    g_view.cam_x = e.cam_x; g_view.cam_y = e.cam_y;
    g_view.scale = e.zoom;
    g_view.ox = 0; g_view.oy = 0;
    g_view.clip_x0 = 0; g_view.clip_y0 = 0; g_view.clip_x1 = W; g_view.clip_y1 = H;
    g_view.snap = false;

    srect(0, 0, W, H, rgba(14, 12, 22));

    int mtx, mty;
    mouse_tile(s, &mtx, &mty);
    int hover_room = room_at_tile(s.world, mtx, mty);

    // Room interiors.
    for (int i = 0; i < s.world.room_count; ++i) {
        const Room& r = s.world.rooms[i];
        if (!r.used) continue;
        wrect((float)(r.x * kTile), (float)(r.y * kTile), (float)(r.w * kTile), (float)(r.h * kTile),
              rgba(34, 32, 58));
    }

    // Grid.
    float tile_px = kTile * e.zoom;
    if (e.grid && tile_px >= 6) {
        int tx0 = floordiv((int)std::floor(e.cam_x), kTile);
        int ty0 = floordiv((int)std::floor(e.cam_y), kTile);
        int nx = (int)(W / tile_px) + 2, ny = (int)(H / tile_px) + 2;
        uint32_t c = rgba(255, 255, 255, 18);
        for (int i = 0; i < nx; ++i) {
            float x = to_sx((float)((tx0 + i) * kTile));
            push_quad(x, 0, x + 1, H, c);
        }
        for (int j = 0; j < ny; ++j) {
            float y = to_sy((float)((ty0 + j) * kTile));
            push_quad(0, y, W, y + 1, c);
        }
    }

    draw_tiles(s.world, false, PASS_BACK);
    draw_tiles(s.world, false, PASS_SOLID);
    draw_tiles(s.world, false, PASS_MOSS);
    draw_water_front(s.world, s.real_time);

    // Room outlines + names.
    for (int i = 0; i < s.world.room_count; ++i) {
        const Room& r = s.world.rooms[i];
        if (!r.used) continue;
        bool hot = i == hover_room;
        bool dragging_this = e.room_drag != RD_NONE && e.room_drag != RD_CREATE && e.drag_room == i;
        uint32_t c = hot ? rgba(255, 255, 255, e.room_mode ? 220 : 120) : rgba(255, 255, 255, 60);
        if (dragging_this) c = rgba(255, 255, 255, 40);
        wframe((float)(r.x * kTile), (float)(r.y * kTile), (float)(r.w * kTile), (float)(r.h * kTile),
               hot && e.room_mode ? 2.0f * g_ui : 1.0f * g_ui, c);
        float sx = to_sx((float)(r.x * kTile)), sy = to_sy((float)(r.y * kTile));
        if (sx > -200 && sx < W && sy > -40 && sy < H && r.w * tile_px > 60)
            text_sh(sx + 4 * g_ui, sy + 14 * g_ui, rgba(255, 255, 255, hot ? 230 : 140), font_small(), r.name);
    }

    // Room drag preview.
    if (e.room_mode && e.room_drag != RD_NONE) {
        const Room& c = e.drag_cur;
        uint32_t col = e.drag_valid ? rgba(120, 255, 160) : rgba(255, 90, 90);
        wrect((float)(c.x * kTile), (float)(c.y * kTile), (float)(c.w * kTile), (float)(c.h * kTile), with_alpha(col, 0.15f));
        wframe((float)(c.x * kTile), (float)(c.y * kTile), (float)(c.w * kTile), (float)(c.h * kTile), 2.0f * g_ui, col);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%dx%d", c.w, c.h);
        text_sh(to_sx((float)(c.x * kTile)) + 4 * g_ui, to_sy((float)((c.y + c.h) * kTile)) - 6 * g_ui, col, font_small(), buf);
    }

    // Edge hover hint in room mode.
    if (e.room_mode && e.room_drag == RD_NONE) {
        int er = -1;
        int edges = edges_under_mouse(s, &er);
        if (edges) {
            const Room& r = s.world.rooms[er];
            float x0 = (float)(r.x * kTile), y0 = (float)(r.y * kTile);
            float x1 = (float)((r.x + r.w) * kTile), y1 = (float)((r.y + r.h) * kTile);
            float t = 3.0f * g_ui / e.zoom;
            uint32_t c = rgba(255, 220, 120);
            if (edges & EDGE_L) wrect(x0 - t, y0, 2 * t, y1 - y0, c);
            if (edges & EDGE_R) wrect(x1 - t, y0, 2 * t, y1 - y0, c);
            if (edges & EDGE_T) wrect(x0, y0 - t, x1 - x0, 2 * t, c);
            if (edges & EDGE_B) wrect(x0, y1 - t, x1 - x0, 2 * t, c);
        }
    }

    // Player position.
    {
        const Player& p = s.play.p;
        wrect((float)p.x, (float)p.y, kPlayerW, kPlayerH, rgba(230, 72, 92, 150));
    }

    // Respawn point.
    wframe((float)s.play.spawn_x, (float)s.play.spawn_y, kPlayerW, kPlayerH, 1, rgba(90, 220, 230, 160));

    // Cursor.
    bool over_palette = mouse_over_palette(s);
    if (!e.room_mode && !over_palette) {
        if (e.rect_drag) {
            int x0 = imin(e.rect_tx, mtx), x1 = imax(e.rect_tx, mtx);
            int y0 = imin(e.rect_ty, mty), y1 = imax(e.rect_ty, mty);
            uint32_t c = e.rect_erase ? rgba(255, 90, 90) : rgba(120, 200, 255);
            wrect((float)(x0 * kTile), (float)(y0 * kTile), (float)((x1 - x0 + 1) * kTile), (float)((y1 - y0 + 1) * kTile), with_alpha(c, 0.25f));
            wframe((float)(x0 * kTile), (float)(y0 * kTile), (float)((x1 - x0 + 1) * kTile), (float)((y1 - y0 + 1) * kTile), 1.0f * g_ui, c);
        } else {
            bool inside = hover_room >= 0;
            wframe((float)(mtx * kTile), (float)(mty * kTile), kTile, kTile, 1.0f * g_ui,
                   inside ? rgba(255, 255, 255, 200) : rgba(255, 90, 90, 200));
        }
    }

    // ---- UI: palette bar
    float bar_h = palette_bar_h();
    srect(0, H - bar_h, W, bar_h, rgba(20, 18, 32, 235));
    srect(0, H - bar_h, W, 1, rgba(90, 86, 130));
    {
        float sz = palette_item_size(), pad = 6.0f * (float)g_ui;
        float total = kPaletteN * (sz + pad) - pad;
        float x0 = (W - total) * 0.5f;
        float y0 = H - bar_h + 8.0f * (float)g_ui;
        for (int i = 0; i < kPaletteN; ++i)
            draw_palette_item(i, x0 + (float)i * (sz + pad), y0, sz, i == e.brush && !e.room_mode, e.spike_dir);
        int hov = palette_item_at(s, s.in.mouse_x, s.in.mouse_y);
        if (hov >= 0) {
            const char* nm = kPalette[hov] == T_SPIKE_U ? "spikes" : kTileInfo[kPalette[hov]].name;
            text_sh(x0 + (float)hov * (sz + pad), y0 - 10 * g_ui, rgba(255, 255, 255), font_small(), nm);
        }
    }

    // ---- UI: status line
    srect(0, 0, W, line_h() + 8 * g_ui, rgba(20, 18, 32, 220));
    {
        char buf[96];
        const char* brush_name = kPalette[e.brush] == T_SPIKE_U
            ? (e.spike_dir ? kTileInfo[e.spike_dir].name : "spikes (auto)")
            : kTileInfo[kPalette[e.brush]].name;
        std::snprintf(buf, sizeof(buf), "EDIT%s  |  %s  |  %s  |  zoom %.2fx  |  tile %d,%d  %s",
                      e.dirty ? "*" : "",
                      e.room_mode ? "ROOM MODE" : "tile mode",
                      e.room_mode ? "drag: create / move / resize" : brush_name,
                      (double)(e.zoom / ed_scale_base(s)), mtx, mty,
                      hover_room >= 0 ? s.world.rooms[hover_room].name : "(no room)");
        text(8 * g_ui, line_h(), rgba(235, 235, 245), font_small(), "%s", buf);
        if (e.status_timer > 0)
            text(W - (float)std::strlen(e.status) * char_w() - 10 * g_ui, line_h(),
                 with_alpha(rgba(255, 220, 120), clampf(e.status_timer, 0, 1)), font_small(), "%s", e.status);
    }

    // ---- UI: help
    if (e.help) {
        static const char* lines[] = {
            "Tab         back to the game",
            "P           play from the cursor",
            "WASD/arrows pan (Shift = fast)",
            "Mid-drag    pan     Wheel  zoom",
            "F           frame room / world",
            "LMB / RMB   paint / erase",
            "Shift+drag  fill / erase rectangle",
            "Alt+click   pick tile",
            "1-9,0 Q/E   choose brush (Q/E: decor)",
            "T           spike direction",
            "R           room mode on/off",
            "  drag      create / move / resize",
            "  Del / X   delete room",
            "N           new screen-sized room",
            "Ctrl+Z / Y  undo / redo",
            "Ctrl+S      save world",
            "G           grid   H  hide help",
            "",
            "PLAY  move   WASD / arrows",
            "      jump   Space / C / K",
            "      dash   X / J / LShift",
            "      grab   Z / L / RShift",
            "      pad: A jump, X/B dash, LB/RB grab",
        };
        const int n = (int)(sizeof(lines) / sizeof(lines[0]));
        float pw = 40 * char_w() + 16 * g_ui, ph = (float)n * line_h() + 16 * g_ui;
        float px = W - pw - 10 * g_ui, py = line_h() + 18 * g_ui;
        srect(px, py, pw, ph, rgba(20, 18, 32, 220));
        sframe(px, py, pw, ph, 1, rgba(90, 86, 130));
        for (int i = 0; i < n; ++i)
            text(px + 8 * g_ui, py + 8 * g_ui + (float)(i + 1) * line_h() - 3 * g_ui, rgba(220, 220, 235), font_small(), "%s", lines[i]);
    }

    if (s.quad_overflow)
        text(8 * g_ui, line_h() * 2 + 8 * g_ui, rgba(255, 90, 90), font_small(), "too many quads -- zoom in");
}

} // namespace hop
