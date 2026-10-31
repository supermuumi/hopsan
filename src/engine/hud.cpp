#include "engine/hud.h"
#include "engine/ui_renderer.h"

#include <cstring>

namespace engine {

namespace {

// (view * proj) * (x, y, 0, 1) -> clip space. Column-major.
struct Vec4 { float x, y, z, w; };

Vec4 transform_world(const float* view, const float* proj, float wx, float wy) {
    // View first.
    float vx = view[0]*wx + view[4]*wy + view[12];
    float vy = view[1]*wx + view[5]*wy + view[13];
    float vz = view[2]*wx + view[6]*wy + view[14];
    float vw = view[3]*wx + view[7]*wy + view[15];
    // Then proj.
    Vec4 c;
    c.x = proj[0]*vx + proj[4]*vy + proj[8]*vz  + proj[12]*vw;
    c.y = proj[1]*vx + proj[5]*vy + proj[9]*vz  + proj[13]*vw;
    c.z = proj[2]*vx + proj[6]*vy + proj[10]*vz + proj[14]*vw;
    c.w = proj[3]*vx + proj[7]*vy + proj[11]*vz + proj[15]*vw;
    return c;
}

bool project_world_to_screen(const float* view, const float* proj,
                             float wx, float wy,
                             int window_w, int window_h,
                             float* out_sx, float* out_sy) {
    Vec4 c = transform_world(view, proj, wx, wy);
    if (c.w <= 0.0001f) return false;
    float ndc_x = c.x / c.w;
    float ndc_y = c.y / c.w;
    if (ndc_x < -1.5f || ndc_x > 1.5f) return false;
    if (ndc_y < -1.5f || ndc_y > 1.5f) return false;
    *out_sx = (ndc_x * 0.5f + 0.5f) * (float)window_w;
    *out_sy = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)window_h;
    return true;
}

constexpr uint32_t kHudBarBgColor     = 0xC8141414u; // dark, alpha=200
constexpr uint32_t kHudBarBorderColor = 0xDC000000u; // black, alpha=220

} // namespace

void hud_queue(const RenderCommands* cmds, UiRenderer* ui,
               int window_w, int window_h) {
    if (!cmds || !ui) return;

    // Panels first, so text + bars layer over them.
    for (int i = 0; i < cmds->hud_panel_count; ++i) {
        const HudPanel& p = cmds->hud_panels[i];
        if (p.bg_color)     ui_renderer_queue_rect(ui, p.x, p.y, p.w, p.h, p.bg_color);
        if (p.border_color) {
            // 1px border drawn as four thin rects.
            ui_renderer_queue_rect(ui, p.x,             p.y,             p.w, 1.0f, p.border_color);
            ui_renderer_queue_rect(ui, p.x,             p.y + p.h - 1.0f, p.w, 1.0f, p.border_color);
            ui_renderer_queue_rect(ui, p.x,             p.y, 1.0f, p.h, p.border_color);
            ui_renderer_queue_rect(ui, p.x + p.w - 1.0f, p.y, 1.0f, p.h, p.border_color);
        }
    }

    // World-space HUD bars: project, then emit bg + fg + 2px border.
    constexpr float bar_w_px    = 36.0f;
    constexpr float bar_h_px    = 5.0f;
    constexpr float offset_y_px = 28.0f;
    for (int i = 0; i < cmds->hud_bar_count; ++i) {
        const HudBar& b = cmds->hud_bars[i];
        float sx = 0.0f, sy = 0.0f;
        if (!project_world_to_screen(cmds->view_matrix, cmds->proj_matrix,
                                     b.world_x, b.world_y,
                                     window_w, window_h, &sx, &sy)) continue;
        const float x0 = sx - bar_w_px * 0.5f;
        const float y0 = sy - offset_y_px;
        // Background
        ui_renderer_queue_rect(ui, x0, y0, bar_w_px, bar_h_px, kHudBarBgColor);
        // Foreground
        if (b.frac > 0.0f) {
            float fw = bar_w_px * b.frac;
            if (fw > bar_w_px) fw = bar_w_px;
            ui_renderer_queue_rect(ui, x0, y0, fw, bar_h_px, b.color);
        }
        // 2px border (four thin rects, matches the previous look)
        ui_renderer_queue_rect(ui, x0,                y0,                bar_w_px, 2.0f, kHudBarBorderColor);
        ui_renderer_queue_rect(ui, x0,                y0 + bar_h_px - 2.0f, bar_w_px, 2.0f, kHudBarBorderColor);
        ui_renderer_queue_rect(ui, x0,                y0, 2.0f, bar_h_px, kHudBarBorderColor);
        ui_renderer_queue_rect(ui, x0 + bar_w_px - 2.0f, y0, 2.0f, bar_h_px, kHudBarBorderColor);
    }

    // Text last, on top. HudText doesn't carry a font id yet -- default to
    // font 0 (the HUD font: ProggyClean). Once we want per-label fonts
    // here, grow HudText with a font field.
    for (int i = 0; i < cmds->hud_text_count; ++i) {
        const HudText& t = cmds->hud_texts[i];
        if (t.text[0] == '\0') continue;
        ui_renderer_queue_text(ui, 0, t.x, t.y, t.text, t.color);
    }
}

} // namespace engine
