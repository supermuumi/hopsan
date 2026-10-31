#pragma once

// Native screen-space UI renderer (#76, #77, #81). Loads N TTF/OTF fonts,
// bakes them into a single ASCII atlas via stb_truetype, owns an
// alpha-blended SDL_GPU pipeline, and exposes a queue / flush API.
//
// The abstraction layer the issue called for: stb_truetype lives entirely
// inside this TU. Callers (and the game DLL via the RenderCommands HUD
// primitives) never see it; swapping the underlying font library is a
// one-file change.

#include <SDL3/SDL.h>
#include <cstdint>

namespace engine {

struct UiRenderer;

// One font slot: filesystem path + desired pixel height. Indexed by
// position in the array passed to ui_renderer_create -- callers refer
// back to that index as the `font_id` argument later. font 0 is the
// implicit default for HUD primitives that don't carry a font id.
struct UiFontSpec {
    const char* path;
    float       px_height;
};

constexpr int kMaxFonts = 8;

UiRenderer* ui_renderer_create(SDL_GPUDevice* gpu, SDL_GPUTextureFormat swap_fmt,
                               const UiFontSpec* fonts, int font_count,
                               const char* shader_dir);
void        ui_renderer_destroy(UiRenderer* r);

void        ui_renderer_begin_frame(UiRenderer* r, int window_w, int window_h);

// Solid-color screen-space rectangle. (x, y) is top-left in pixels.
// `color` is IM_COL32-packed ABGR.
void        ui_renderer_queue_rect(UiRenderer* r, float x, float y,
                                   float w, float h, uint32_t color);

// Text run at baseline (x, y) in `font_id`. Returns the resulting x in
// pixels so the caller can stack runs. ASCII 32..126 only.
float       ui_renderer_queue_text(UiRenderer* r, int font_id,
                                   float x, float y,
                                   const char* text, uint32_t color);

float       ui_renderer_text_width(const UiRenderer* r, int font_id,
                                   const char* text);

// Pixel line-advance for the given font (font_size * 1.25 as a rough
// default; we don't pull true vertical metrics out of the font).
float       ui_renderer_line_height(const UiRenderer* r, int font_id);

void        ui_renderer_prepare(UiRenderer* r, SDL_GPUCommandBuffer* cmd);
void        ui_renderer_draw(UiRenderer* r, SDL_GPUCommandBuffer* cmd,
                             SDL_GPURenderPass* pass);

} // namespace engine
