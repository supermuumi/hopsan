// 2D drawing: a world -> screen view transform, a quad list that the host
// renders as screen-space rects, text via HudText, and procedural
// placeholder art for every tile type (no textures needed yet).

namespace hop {

struct View {
    float cam_x, cam_y;   // world px at the view's top-left
    float scale;          // screen px per world px
    float ox, oy;         // screen offset of the view's top-left
    float clip_x0, clip_y0, clip_x1, clip_y1;   // screen-space clip rect
    bool  snap;           // round world coords to whole pixels (play mode)
};

View            g_view;
RenderCommands* g_out = nullptr;
int             g_ui  = 1;        // UI scale: 2 on hi-dpi windows

int  font_small() { return g_ui >= 2 ? HUD_FONT_SMALL_2X : HUD_FONT_SMALL; }
int  font_title() { return g_ui >= 2 ? HUD_FONT_TITLE_2X : HUD_FONT_TITLE; }
float char_w()    { return 7.0f * (float)g_ui; }   // ProggyClean is monospace
float line_h()    { return 15.0f * (float)g_ui; }

// Emit one screen-space quad, clipped to the view. Clipping a textured
// quad moves its UVs proportionally so the visible part stays in place.
void push_quad_uv(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  uint32_t color, AssetHandle tex) {
    GameState* s = g_state;
    if ((color >> 24) == 0) return;
    if (x1 <= x0 || y1 <= y0) return;
    const float w = x1 - x0, h = y1 - y0;
    if (x0 < g_view.clip_x0) { u0 += (u1 - u0) * (g_view.clip_x0 - x0) / w; x0 = g_view.clip_x0; }
    if (x1 > g_view.clip_x1) { u1 -= (u1 - u0) * (x1 - g_view.clip_x1) / (x1 - x0); x1 = g_view.clip_x1; }
    if (y0 < g_view.clip_y0) { v0 += (v1 - v0) * (g_view.clip_y0 - y0) / h; y0 = g_view.clip_y0; }
    if (y1 > g_view.clip_y1) { v1 -= (v1 - v0) * (y1 - g_view.clip_y1) / (y1 - y0); y1 = g_view.clip_y1; }
    if (x1 <= x0 || y1 <= y0) return;
    if (s->quad_count >= kMaxQuads) { s->quad_overflow = true; return; }
    Quad2D& q = s->quads[s->quad_count++];
    q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1; q.color = color;
    q.texture = tex;
    q.u0 = u0; q.v0 = v0; q.u1 = u1; q.v1 = v1;
}

void push_quad(float x0, float y0, float x1, float y1, uint32_t color) {
    push_quad_uv(x0, y0, x1, y1, 0, 0, 0, 0, color, 0);
}

// Screen-space rect (pixels), ignores the view clip.
void srect(float x, float y, float w, float h, uint32_t color) {
    View saved = g_view;
    g_view.clip_x0 = -1e9f; g_view.clip_y0 = -1e9f;
    g_view.clip_x1 =  1e9f; g_view.clip_y1 =  1e9f;
    push_quad(x, y, x + w, y + h, color);
    g_view = saved;
}

void sframe(float x, float y, float w, float h, float t, uint32_t color) {
    srect(x, y, w, t, color);
    srect(x, y + h - t, w, t, color);
    srect(x, y + t, t, h - 2 * t, color);
    srect(x + w - t, y + t, t, h - 2 * t, color);
}

float to_sx(float wx) { return g_view.ox + (wx - g_view.cam_x) * g_view.scale; }
float to_sy(float wy) { return g_view.oy + (wy - g_view.cam_y) * g_view.scale; }

// Round to the art-pixel grid (half world units) in play mode.
float snapw(float v) { return g_view.snap ? std::floor(v * kArtScale + 0.5f) / kArtScale : v; }

// World-space rect.
void wrect(float x, float y, float w, float h, uint32_t color) {
    x = snapw(x); y = snapw(y); w = snapw(w); h = snapw(h);
    push_quad(to_sx(x), to_sy(y), to_sx(x + w), to_sy(y + h), color);
}

// ---- Sprites from the atlas ----------------------------------------------

bool have_art() { return g_state && g_state->atlas != 0; }

// Draw atlas sub-rect (sx, sy, sw, sh) in art px of sprite `id` into the
// world rect (x, y, w, h). flip_x mirrors horizontally.
void wsprite_sub(int id, float sx, float sy, float sw, float sh,
                 float x, float y, float w, float h, uint32_t color = 0xFFFFFFFFu, bool flip_x = false) {
    const AtlasRect& r = kSprites[id];
    x = snapw(x); y = snapw(y);
    float u0 = (r.x + sx) / (float)kAtlasW, u1 = (r.x + sx + sw) / (float)kAtlasW;
    float v0 = (r.y + sy) / (float)kAtlasH, v1 = (r.y + sy + sh) / (float)kAtlasH;
    if (flip_x) { float t = u0; u0 = u1; u1 = t; }
    push_quad_uv(to_sx(x), to_sy(y), to_sx(x + w), to_sy(y + h), u0, v0, u1, v1, color, g_state->atlas);
}

float spr_w(int id) { return (float)kSprites[id].w / kArtScale; }   // world units
float spr_h(int id) { return (float)kSprites[id].h / kArtScale; }

// Whole sprite at its natural size with its top-left at (x, y).
void wsprite(int id, float x, float y, uint32_t color = 0xFFFFFFFFu, bool flip_x = false) {
    const AtlasRect& r = kSprites[id];
    wsprite_sub(id, 0, 0, r.w, r.h, x, y, spr_w(id), spr_h(id), color, flip_x);
}

// Sprite standing on the floor of the tile at (px, py), centred.
void wsprite_floor(int id, float px, float py, uint32_t color = 0xFFFFFFFFu, bool flip_x = false) {
    wsprite(id, px + (kTile - spr_w(id)) * 0.5f, py + kTile - spr_h(id), color, flip_x);
}

// World-space outline whose thickness is in *screen* pixels.
void wframe(float x, float y, float w, float h, float t_px, uint32_t color) {
    float x0 = to_sx(x), y0 = to_sy(y), x1 = to_sx(x + w), y1 = to_sy(y + h);
    push_quad(x0, y0, x1, y0 + t_px, color);
    push_quad(x0, y1 - t_px, x1, y1, color);
    push_quad(x0, y0 + t_px, x0 + t_px, y1 - t_px, color);
    push_quad(x1 - t_px, y0 + t_px, x1, y1 - t_px, color);
}

bool world_rect_visible(float x, float y, float w, float h) {
    float x0 = to_sx(x), y0 = to_sy(y), x1 = to_sx(x + w), y1 = to_sy(y + h);
    return x1 > g_view.clip_x0 && x0 < g_view.clip_x1 && y1 > g_view.clip_y0 && y0 < g_view.clip_y1;
}

void text(float x, float y, uint32_t color, int font, const char* fmt, ...) {
    if (!g_out || g_out->hud_text_count >= kMaxHudTexts) return;
    HudText& t = g_out->hud_texts[g_out->hud_text_count++];
    t.x = x; t.y = y; t.color = color; t.font = font;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(t.text, sizeof(t.text), fmt, ap);
    va_end(ap);
}

// Text with a 1px drop shadow, for readability over the world.
void text_sh(float x, float y, uint32_t color, int font, const char* str) {
    float o = (float)g_ui;
    text(x + o, y + o, rgba(0, 0, 0, 200), font, "%s", str);
    text(x, y, color, font, "%s", str);
}

// ---- Palette -------------------------------------------------------------

constexpr uint32_t kRock      = rgba(92, 84, 124);
constexpr uint32_t kRockDark  = rgba(66, 60, 92);
constexpr uint32_t kRockLight = rgba(124, 116, 158);
constexpr uint32_t kGrass     = rgba(102, 196, 120);
constexpr uint32_t kGrassDark = rgba(64, 140, 92);
constexpr uint32_t kBrick     = rgba(140, 82, 66);
constexpr uint32_t kMortar    = rgba(92, 52, 46);
constexpr uint32_t kBackdrop  = rgba(44, 42, 66);
constexpr uint32_t kBackdropD = rgba(36, 34, 54);
constexpr uint32_t kPlank     = rgba(170, 124, 78);
constexpr uint32_t kPlankDark = rgba(110, 76, 48);
constexpr uint32_t kSteel     = rgba(214, 222, 236);
constexpr uint32_t kSteelDark = rgba(130, 138, 160);

bool is_solid_tile(uint8_t t) { return t == T_ROCK || t == T_BRICK; }

// Defined in hazards.cpp (included after this file).
void draw_hazard_static(GameState& s, uint8_t t, int tx, int ty);
void draw_conveyor(int tx, int ty, int dir, float t);

// ---- Tile art ------------------------------------------------------------
// All tile art is authored in a 16x16 local space and placed at (px, py).

void tr(float px, float py, float x, float y, float w, float h, uint32_t c) {
    wrect(px + x, py + y, w, h, c);
}

// Solid tile from the world-aligned rock texture (4x4 tiles per repeat),
// with an outline + shading on faces that are exposed to air.
void draw_solid_art(const World& w, int tx, int ty, float px, float py, uint32_t tint) {
    wsprite_sub(SPR_ROCK, (float)((tx & 3) * 32), (float)((ty & 3) * 32), 32, 32, px, py, kTile, kTile, tint);
    const float a = 1.0f / kArtScale;   // one art pixel in world units
    const uint32_t outline = rgba(8, 10, 22);
    const uint32_t shadow  = rgba(0, 0, 0, 90);
    const uint32_t light   = rgba(170, 190, 210, 70);
    bool up    = is_solid_tile(tile_get(w, tx, ty - 1));
    bool down  = is_solid_tile(tile_get(w, tx, ty + 1));
    bool left  = is_solid_tile(tile_get(w, tx - 1, ty));
    bool right = is_solid_tile(tile_get(w, tx + 1, ty));
    if (!up)    { wrect(px, py, kTile, a, outline); wrect(px, py + a, kTile, a, light); }
    if (!left)  { wrect(px, py, a, kTile, outline); wrect(px + a, py, a, kTile, light); }
    if (!right) { wrect(px + kTile - a, py, a, kTile, outline); wrect(px + kTile - 3 * a, py, 2 * a, kTile, shadow); }
    if (!down)  { wrect(px, py + kTile - a, kTile, a, outline); wrect(px, py + kTile - 3 * a, kTile, 2 * a, shadow); }
}

// Moss strip along the top of a rock tile (drawn in its own pass so the
// grass can poke into the tile above).
void draw_moss_art(int tx, float px, float py) {
    const AtlasRect& r = kSprites[SPR_MOSS];
    float h = (float)r.h / kArtScale;
    wsprite_sub(SPR_MOSS, (float)((tx & 3) * 32), 0, 32, r.h, px, py - (float)kMossBandTop / kArtScale, kTile, h);
}

void draw_decor(uint8_t t, int tx, int ty, float px, float py) {
    if (!have_art()) {
        // Placeholder: a small coloured mark.
        uint32_t c = kTileInfo[t].color;
        if (t == T_DECOR_VINES || t == T_DECOR_CHAIN) wrect(px + 6, py, 4, 12, c);
        else if (t == T_DECOR_PILLAR) wrect(px + 3, py - 16, 10, 32, c);
        else wrect(px + 3, py + 8, 10, 8, c);
        return;
    }
    bool flip = (hash2(tx, ty) & 1) != 0;
    switch (t) {
        case T_DECOR_GRASS:   wsprite_floor(SPR_GRASS, px, py, 0xFFFFFFFFu, flip); break;
        case T_DECOR_FERN:    wsprite_floor(SPR_FERN, px, py, 0xFFFFFFFFu, flip); break;
        case T_DECOR_SHROOMS: wsprite_floor(SPR_SHROOMS, px, py, 0xFFFFFFFFu, flip); break;
        case T_DECOR_RUBBLE:  wsprite_floor(SPR_RUBBLE, px, py, 0xFFFFFFFFu, flip); break;
        case T_DECOR_PILLAR:  wsprite_floor(SPR_PILLAR, px, py, rgba(200, 205, 215)); break;
        case T_DECOR_TORCH: {
            // a little flicker
            float f = 0.85f + 0.15f * std::sin(g_state->real_time * 13.0f + (float)tx);
            int v = (int)(255 * f);
            wsprite_floor(SPR_TORCH, px, py, rgba(255, v, v));
            break;
        }
        case T_DECOR_VINES: wsprite(SPR_VINES, px + (kTile - spr_w(SPR_VINES)) * 0.5f, py, 0xFFFFFFFFu, flip); break;
        case T_DECOR_CHAIN: wsprite(SPR_CHAIN, px + (kTile - spr_w(SPR_CHAIN)) * 0.5f, py); break;
        default: break;
    }
}

void draw_rock(const World& w, int tx, int ty, float px, float py) {
    if (have_art()) { draw_solid_art(w, tx, ty, px, py, 0xFFFFFFFFu); return; }
    bool up    = is_solid_tile(tile_get(w, tx, ty - 1));
    bool down  = is_solid_tile(tile_get(w, tx, ty + 1));
    bool left  = is_solid_tile(tile_get(w, tx - 1, ty));
    bool right = is_solid_tile(tile_get(w, tx + 1, ty));
    tr(px, py, 0, 0, 16, 16, kRock);
    uint32_t h = hash2(tx, ty);
    // speckles
    tr(px, py, (float)(h & 7) + 3, (float)((h >> 3) & 7) + 4, 2, 2, kRockDark);
    tr(px, py, (float)((h >> 6) & 7) + 2, (float)((h >> 9) & 7) + 2, 1, 1, kRockLight);
    if (!left)  tr(px, py, 0, 0, 2, 16, kRockLight);
    if (!right) tr(px, py, 14, 0, 2, 16, kRockDark);
    if (!down)  tr(px, py, 0, 14, 16, 2, kRockDark);
    if (!up) {
        tr(px, py, 0, 0, 16, 4, kGrass);
        tr(px, py, 0, 4, 16, 1, kGrassDark);
        // tufts
        tr(px, py, (float)((h >> 12) & 7) + 1, -2, 1, 2, kGrass);
        tr(px, py, (float)((h >> 15) & 7) + 8, -1, 1, 1, kGrass);
        if (!left)  tr(px, py, 0, 4, 1, 2, kGrassDark);
        if (!right) tr(px, py, 15, 4, 1, 2, kGrassDark);
    }
}

void draw_brick(const World& w, int tx, int ty, float px, float py) {
    if (have_art()) { draw_solid_art(w, tx, ty, px, py, rgba(236, 196, 168)); return; }
    tr(px, py, 0, 0, 16, 16, kBrick);
    tr(px, py, 0, 7, 16, 1, kMortar);
    tr(px, py, 0, 15, 16, 1, kMortar);
    int off = (tx + ty) & 1 ? 8 : 0;
    tr(px, py, (float)(7 + off) - (off ? 8 : 0), 0, 1, 7, kMortar);
    tr(px, py, (float)(15 - off), 8, 1, 7, kMortar);
    if (!is_solid_tile(tile_get(w, tx, ty - 1))) tr(px, py, 0, 0, 16, 1, shade(kBrick, 1.3f));
}

void draw_backdrop(int tx, int ty, float px, float py) {
    if (have_art()) {
        wsprite_sub(SPR_BACKWALL, (float)((tx & 3) * 32), (float)((ty & 3) * 32), 32, 32, px, py, kTile, kTile);
        return;
    }
    tr(px, py, 0, 0, 16, 16, kBackdrop);
    uint32_t h = hash2(tx, ty);
    tr(px, py, (float)(h & 15), (float)((h >> 4) & 15), 2, 1, kBackdropD);
    tr(px, py, (float)((h >> 8) & 15), (float)((h >> 12) & 15), 1, 2, kBackdropD);
}

void draw_oneway(const World& w, int tx, int ty, float px, float py) {
    if (have_art()) { wsprite(SPR_PLANK, px, py); return; }
    tr(px, py, 0, 0, 16, 3, kPlank);
    tr(px, py, 0, 3, 16, 1, kPlankDark);
    if (tile_get(w, tx - 1, ty) != T_ONEWAY) tr(px, py, 1, 4, 2, 5, kPlankDark);
    if (tile_get(w, tx + 1, ty) != T_ONEWAY) tr(px, py, 13, 4, 2, 5, kPlankDark);
}

// Spike rect in local (u along the base, v away from the mount) space.
void spike_rect(uint8_t t, float px, float py, float u, float v, float du, float dv, uint32_t c) {
    switch (t) {
        case T_SPIKE_U: tr(px, py, u, 16 - v - dv, du, dv, c); break;
        case T_SPIKE_D: tr(px, py, u, v, du, dv, c); break;
        case T_SPIKE_L: tr(px, py, 16 - v - dv, u, dv, du, c); break;
        case T_SPIKE_R: tr(px, py, v, u, dv, du, c); break;
    }
}

void draw_spikes(uint8_t t, float px, float py, float alpha = 1.0f) {
    if (have_art()) {
        uint32_t c = with_alpha(0xFFFFFFFFu, alpha);
        switch (t) {
            case T_SPIKE_U: wsprite(SPR_SPIKES_U, px, py + kTile - spr_h(SPR_SPIKES_U), c); break;
            case T_SPIKE_D: wsprite(SPR_SPIKES_D, px, py, c); break;
            case T_SPIKE_L: wsprite(SPR_SPIKES_L, px + kTile - spr_w(SPR_SPIKES_L), py, c); break;
            case T_SPIKE_R: wsprite(SPR_SPIKES_R, px, py, c); break;
        }
        return;
    }
    static const int widths[7] = { 2, 2, 4, 4, 6, 6, 8 };   // tip -> base
    spike_rect(t, px, py, 0, 0, 16, 2, with_alpha(kSteelDark, alpha));
    for (int s = 0; s < 2; ++s) {
        float u0 = (float)(s * 8);
        for (int r = 0; r < 7; ++r) {
            float wdt = (float)widths[r];
            float v = (float)(8 - r);
            float u = u0 + (8 - wdt) * 0.5f;
            spike_rect(t, px, py, u, v, wdt * 0.5f, 1, with_alpha(kSteel, alpha));
            spike_rect(t, px, py, u + wdt * 0.5f, v, wdt * 0.5f, 1, with_alpha(kSteelDark, alpha));
        }
    }
}

void draw_crumble(float px, float py, float alpha) {
    if (have_art()) {
        wsprite_sub(SPR_STONE, 0, 0, 32, 32, px, py, kTile, kTile, with_alpha(rgba(240, 206, 160), alpha));
        return;
    }
    uint32_t c = with_alpha(rgba(196, 156, 104), alpha);
    uint32_t d = with_alpha(rgba(132, 98, 62), alpha);
    tr(px, py, 0, 0, 16, 16, d);
    tr(px, py, 1, 1, 14, 14, c);
    tr(px, py, 5, 2, 1, 4, d);
    tr(px, py, 6, 5, 3, 1, d);
    tr(px, py, 10, 9, 1, 5, d);
    tr(px, py, 3, 11, 4, 1, d);
}

void draw_spring(float px, float py, float compress) {
    if (have_art()) {
        float w = spr_w(SPR_SPRING), h = spr_h(SPR_SPRING) * (1.0f - 0.4f * clampf(compress, 0, 1));
        const AtlasRect& r = kSprites[SPR_SPRING];
        wsprite_sub(SPR_SPRING, 0, 0, r.w, r.h, px + (kTile - w) * 0.5f, py + kTile - h, w, h);
        return;
    }
    float c = clampf(compress, 0, 1) * 4.0f;
    tr(px, py, 2, 13, 12, 3, rgba(80, 80, 96));
    tr(px, py, 4, 9 + c, 2, 4 - c, rgba(170, 170, 190));
    tr(px, py, 10, 9 + c, 2, 4 - c, rgba(170, 170, 190));
    tr(px, py, 1, 7 + c, 14, 3, rgba(236, 200, 70));
    tr(px, py, 1, 7 + c, 14, 1, rgba(255, 236, 140));
}

void draw_crystal(float px, float py, float bob, bool available) {
    if (have_art()) {
        float x = px + (kTile - spr_w(SPR_CRYSTAL)) * 0.5f, y = py + (kTile - spr_h(SPR_CRYSTAL)) * 0.5f + bob;
        wsprite(SPR_CRYSTAL, x, y, available ? 0xFFFFFFFFu : rgba(120, 160, 200, 70));
        return;
    }
    static const int half[12] = { 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1 };
    float oy = 2 + bob;
    for (int r = 0; r < 12; ++r) {
        float hw = (float)half[r];
        if (available) {
            tr(px, py, 8 - hw, oy + r, hw, 1, rgba(150, 255, 190));
            tr(px, py, 8, oy + r, hw, 1, rgba(70, 200, 130));
        } else {
            tr(px, py, 8 - hw, oy + r, 1, 1, rgba(96, 236, 150, 90));
            tr(px, py, 8 + hw - 1, oy + r, 1, 1, rgba(96, 236, 150, 90));
        }
    }
}

void draw_checkpoint(float px, float py, bool active, float t) {
    if (have_art()) {
        uint32_t c = active ? rgba(170, 255, 240) : rgba(150, 120, 130);
        wsprite(SPR_FLAG, px + 3, py + kTile - spr_h(SPR_FLAG), c);
        return;
    }
    tr(px, py, 7, 1, 2, 15, rgba(170, 170, 190));
    tr(px, py, 5, 14, 6, 2, rgba(110, 110, 130));
    uint32_t c = active ? rgba(90, 220, 230) : rgba(150, 72, 80);
    for (int i = 0; i < 7; ++i) {
        float wave = active ? std::floor(std::sin(t * 8.0f - (float)i * 0.9f) + 0.5f) : 0.0f;
        tr(px, py, 9 + (float)i, 1 + wave, 1, 5 - (float)i * 0.4f, c);
    }
}

void draw_walker(float x, float y, int dir, float t) {
    if (have_art()) {
        int frame = SPR_BEETLE_0 + (((int)(t * 8.0f)) & 3);
        // Hitbox 14x12 at (x, y); the beetle stands on the hitbox floor.
        // Source art faces right.
        wsprite(frame, x + 7 - spr_w(frame) * 0.5f, y + 12 - spr_h(frame), 0xFFFFFFFFu, dir < 0);
        return;
    }
    // Hitbox 14x12 with top-left at (x, y).
    uint32_t body = rgba(150, 70, 176);
    uint32_t dark = rgba(98, 40, 120);
    wrect(x + 1, y, 12, 11, body);
    wrect(x, y + 2, 14, 7, body);
    wrect(x + 1, y + 9, 12, 2, dark);
    int step = ((int)(t * 8.0f)) & 1;
    wrect(x + 2 + step, y + 11, 3, 1, dark);
    wrect(x + 9 - step, y + 11, 3, 1, dark);
    float ex = dir > 0 ? x + 8 : x + 2;
    wrect(ex, y + 3, 4, 4, rgba(255, 255, 255));
    wrect(ex + (dir > 0 ? 2 : 0), y + 4, 2, 2, rgba(20, 16, 30));
}

// Plan piece: a shredded strip of blueprint (placeholder art).
void draw_star(float px, float py, float bob, bool ghost) {
    float x = px + 4, y = py + 1 + bob;
    uint32_t paper = ghost ? rgba(170, 200, 255, 50) : rgba(58, 104, 190);
    uint32_t ink   = ghost ? rgba(220, 235, 255, 40) : rgba(210, 230, 255);
    // a slightly ragged strip
    wrect(x, y, 8, 14, paper);
    wrect(x + 1, y + 14, 2, 1, paper);
    wrect(x + 5, y + 14, 3, 1, paper);
    wrect(x + 2, y - 1, 3, 1, paper);
    if (!ghost) {
        wrect(x + 1, y + 2, 6, 1, ink);
        wrect(x + 1, y + 5, 4, 1, ink);
        wrect(x + 5, y + 5, 1, 4, ink);
        wrect(x + 1, y + 9, 6, 1, ink);
        wrect(x + 2, y + 11, 2, 2, ink);
        wrect(x, y, 1, 14, rgba(255, 255, 255, 90));   // edge highlight
    }
}

void draw_start_marker(float px, float py) {
    // Ghost of the player standing on this tile's floor.
    float x = px + (16 - kPlayerW) * 0.5f;
    float y = py + 16 - kPlayerH;
    wrect(x, y, kPlayerW, kPlayerH, rgba(255, 255, 255, 70));
    wrect(x, y, kPlayerW, 3, rgba(62, 42, 36, 180));          // hair
    wrect(x, y + 6, kPlayerW, 6, rgba(40, 54, 104, 160));     // blazer
}

// Static look of any tile (used by the editor for every tile, and by play
// mode for the tiles that don't turn into entities).
void draw_tile_static(const World& w, uint8_t t, int tx, int ty, float alpha_entities) {
    float px = (float)(tx * kTile), py = (float)(ty * kTile);
    switch (t) {
        case T_ROCK:       draw_rock(w, tx, ty, px, py); break;
        case T_BRICK:      draw_brick(w, tx, ty, px, py); break;
        case T_BG:         draw_backdrop(tx, ty, px, py); break;
        case T_ONEWAY:     draw_oneway(w, tx, ty, px, py); break;
        case T_SPIKE_U: case T_SPIKE_D: case T_SPIKE_L: case T_SPIKE_R:
                           draw_spikes(t, px, py); break;
        case T_CRUMBLE:    draw_crumble(px, py, 1.0f); break;
        case T_SPRING:     draw_spring(px, py, 0); break;
        case T_CRYSTAL:    draw_crystal(px, py, 0, true); break;
        case T_CHECKPOINT: draw_checkpoint(px, py, false, 0); break;
        case T_WALKER:     draw_walker(px + 1, py + 4, 1, 0); break;
        case T_START:      if (alpha_entities > 0) draw_start_marker(px, py); break;
        case T_STAR:       draw_star(px, py, 0, false); break;
        case T_CONVEYOR_L: case T_CONVEYOR_R:
            draw_conveyor(tx, ty, conveyor_dir(t), g_state->mode == MODE_PLAY ? g_state->play.time : g_state->real_time);
            break;
        case T_LASER: case T_LASER_A: case T_LASER_B: case T_SHARK: case T_DRONE_H: case T_DRONE_V:
            draw_hazard_static(*g_state, t, tx, ty);
            break;
        default:
            if (tile_is_decor(t)) {
                // A tile holds one thing, so a decoration placed in a
                // backdrop wall would leave a hole: fill it back in.
                int bg = (tile_get(w, tx - 1, ty) == T_BG) + (tile_get(w, tx + 1, ty) == T_BG)
                       + (tile_get(w, tx, ty - 1) == T_BG) + (tile_get(w, tx, ty + 1) == T_BG);
                if (bg >= 2) draw_backdrop(tx, ty, px, py);
                draw_decor(t, tx, ty, px, py);
            }
            break;
    }
}

// Tiles that become runtime entities in play mode.
bool tile_is_entity(uint8_t t) {
    return t == T_CRUMBLE || t == T_SPRING || t == T_CRYSTAL || t == T_CHECKPOINT
        || t == T_WALKER || t == T_START || t == T_STAR
        || tile_is_laser(t) || t == T_SHARK || t == T_DRONE_H || t == T_DRONE_V;
}

enum TilePass { PASS_BACK, PASS_SOLID, PASS_MOSS };

// Back pass: backdrop walls + decorations. Solid pass: everything else.
// Moss pass: moss on exposed rock tops (art only).
bool tile_in_pass(uint8_t t, int pass) {
    bool back = t == T_BG || tile_is_decor(t);
    return pass == PASS_BACK ? back : !back;
}

// Draw every tile within the visible world rect. `skip_entities` leaves
// entity tiles to the play-mode entity renderer. At low zoom, tiles are
// drawn as merged single-colour runs to keep the quad count down.
void draw_tiles(const World& w, bool skip_entities, int pass) {
    float wx0 = g_view.cam_x + (g_view.clip_x0 - g_view.ox) / g_view.scale;
    float wy0 = g_view.cam_y + (g_view.clip_y0 - g_view.oy) / g_view.scale;
    float wx1 = g_view.cam_x + (g_view.clip_x1 - g_view.ox) / g_view.scale;
    float wy1 = g_view.cam_y + (g_view.clip_y1 - g_view.oy) / g_view.scale;
    // Margins: decorations like pillars reach up to 2 tiles above their
    // tile, vines hang a tile and a half below theirs.
    int tx0 = floordiv((int)std::floor(wx0), kTile) - 1;
    int ty0 = floordiv((int)std::floor(wy0), kTile) - 2;
    int tx1 = floordiv((int)std::ceil(wx1), kTile) + 1;
    int ty1 = floordiv((int)std::ceil(wy1), kTile) + 3;

    if (g_view.scale * kTile < 6.0f) {
        if (pass == PASS_MOSS) return;
        // Low zoom: one quad per run of equal tiles.
        for (int ty = ty0; ty <= ty1; ++ty) {
            int run_start = tx0;
            uint8_t run_t = tile_get(w, tx0, ty);
            for (int tx = tx0 + 1; tx <= tx1 + 1; ++tx) {
                uint8_t t = tx <= tx1 ? tile_get(w, tx, ty) : 0xFF;
                if (t == run_t) continue;
                if (run_t != T_EMPTY && run_t < T_COUNT && tile_in_pass(run_t, pass)
                    && !(skip_entities && tile_is_entity(run_t))) {
                    wrect((float)(run_start * kTile), (float)(ty * kTile),
                          (float)((tx - run_start) * kTile), (float)kTile, kTileInfo[run_t].color);
                }
                run_start = tx;
                run_t = t;
            }
        }
        return;
    }

    if (pass == PASS_MOSS) {
        if (!have_art()) return;
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx)
                if (tile_get(w, tx, ty) == T_ROCK && !is_solid_tile(tile_get(w, tx, ty - 1)))
                    draw_moss_art(tx, (float)(tx * kTile), (float)(ty * kTile));
        return;
    }

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            uint8_t t = tile_get(w, tx, ty);
            if (t == T_EMPTY || t >= T_COUNT) continue;
            if (!tile_in_pass(t, pass)) continue;
            if (skip_entities && tile_is_entity(t)) continue;
            draw_tile_static(w, t, tx, ty, skip_entities ? 0.0f : 1.0f);
        }
    }
}

// Parallax hills behind everything. `depth` 0..1 (smaller = farther).
void draw_parallax(float depth, uint32_t color, float base_h, float amp, uint32_t seed) {
    float vx0 = g_view.cam_x + (g_view.clip_x0 - g_view.ox) / g_view.scale;
    float vw  = (g_view.clip_x1 - g_view.clip_x0) / g_view.scale;
    float vy1 = g_view.cam_y + (g_view.clip_y1 - g_view.oy) / g_view.scale;
    float vh  = (g_view.clip_y1 - g_view.clip_y0) / g_view.scale;
    const float col = 8.0f;
    float off = vx0 * depth;
    int c0 = (int)std::floor(off / col);
    int cols = (int)(vw / col) + 2;
    for (int i = 0; i < cols; ++i) {
        int c = c0 + i;
        // two octaves of smooth-ish value noise
        auto n = [&](int k, int period) {
            int a = floordiv(k, period), b = a + 1;
            float f = (float)(k - a * period) / (float)period;
            float va = (float)(hash2(a, (int)seed) & 1023) / 1023.0f;
            float vb = (float)(hash2(b, (int)seed) & 1023) / 1023.0f;
            f = f * f * (3 - 2 * f);
            return va + (vb - va) * f;
        };
        float h = base_h + amp * (0.7f * n(c, 12) + 0.3f * n(c, 4));
        float sx = vx0 + (float)c * col - off;
        float top = vy1 - vh * 0.15f - h - (g_view.cam_y * depth * 0.1f);
        wrect(sx, top, col, vy1 - top + 2, color);
    }
}

} // namespace hop
