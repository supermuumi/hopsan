#include "engine/ui_renderer.h"
#include "engine/log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine {

namespace {

constexpr const char* kTag        = "ui";
constexpr int         kAtlasW     = 1024;
constexpr int         kAtlasH     = 1024;
constexpr int         kFirstGlyph = 32;
constexpr int         kNumGlyphs  = 95;   // ASCII 32..126

// Vertex layout: pos(2*f32) + uv(2*f32) + color(4*u8) = 20 bytes.
struct UIVertex {
    float    x, y;
    float    u, v;
    uint32_t color;
};
static_assert(sizeof(UIVertex) == 20, "UIVertex layout drift");

constexpr int kInitVertCap  = 4096;
constexpr int kInitIndexCap = 6144;

uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return nullptr; }
    auto* data = (uint8_t*)std::malloc((size_t)size);
    if (!data) { std::fclose(f); return nullptr; }
    if (std::fread(data, 1, (size_t)size, f) != (size_t)size) {
        std::free(data); std::fclose(f); return nullptr;
    }
    std::fclose(f);
    *out_size = (size_t)size;
    return data;
}

SDL_GPUShader* load_shader(SDL_GPUDevice* gpu, const char* base_path,
                           SDL_GPUShaderStage stage,
                           uint32_t uniform_buffers, uint32_t samplers) {
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(gpu);
    struct Variant { SDL_GPUShaderFormat fmt; const char* ext; const char* ep; };
    const Variant variants[] = {
        { SDL_GPU_SHADERFORMAT_SPIRV, ".spv", "main"  },
        { SDL_GPU_SHADERFORMAT_MSL,   ".msl", "main0" },
    };
    for (const auto& v : variants) {
        if (!(supported & v.fmt)) continue;
        char path[320];
        std::snprintf(path, sizeof(path), "%s%s", base_path, v.ext);
        size_t size = 0;
        uint8_t* code = read_file(path, &size);
        if (!code) continue;
        SDL_GPUShaderCreateInfo sci{};
        sci.code_size           = size;
        sci.code                = code;
        sci.entrypoint          = v.ep;
        sci.format              = v.fmt;
        sci.stage               = stage;
        sci.num_uniform_buffers = uniform_buffers;
        sci.num_samplers        = samplers;
        SDL_GPUShader* sh = SDL_CreateGPUShader(gpu, &sci);
        std::free(code);
        if (sh) return sh;
        LOG_WARN(kTag, "SDL_CreateGPUShader %s: %s", path, SDL_GetError());
    }
    LOG_ERROR(kTag, "no shader variant found for %s", base_path);
    return nullptr;
}

struct UiFont {
    stbtt_packedchar glyphs[kNumGlyphs];
    float            font_px;
    float            line_height;
};

} // namespace

struct UiRenderer {
    SDL_GPUDevice*           gpu              = nullptr;
    SDL_GPUGraphicsPipeline* pipeline         = nullptr;
    SDL_GPUTexture*          atlas_tex        = nullptr;
    SDL_GPUSampler*          atlas_sampler    = nullptr;

    SDL_GPUBuffer*           vbo              = nullptr;
    SDL_GPUBuffer*           ibo              = nullptr;
    int                      gpu_vert_cap     = 0;
    int                      gpu_index_cap    = 0;

    UIVertex*                verts            = nullptr;
    uint32_t*                indices          = nullptr;
    int                      vert_count       = 0;
    int                      index_count      = 0;
    int                      cpu_vert_cap     = 0;
    int                      cpu_index_cap    = 0;

    float                    window_w         = 0.0f;
    float                    window_h         = 0.0f;

    UiFont                   fonts[kMaxFonts];
    int                      font_count       = 0;

    // Single white pixel reserved for untextured quads.
    float                    white_u          = 0.0f;
    float                    white_v          = 0.0f;
};

namespace {

bool create_pipeline(UiRenderer* r, SDL_GPUTextureFormat swap_fmt, const char* shader_dir) {
    char vsp[256], fsp[256];
    std::snprintf(vsp, sizeof(vsp), "%s/ui.vert", shader_dir);
    std::snprintf(fsp, sizeof(fsp), "%s/ui.frag", shader_dir);
    SDL_GPUShader* vs = load_shader(r->gpu, vsp, SDL_GPU_SHADERSTAGE_VERTEX,   1, 0);
    SDL_GPUShader* fs = load_shader(r->gpu, fsp, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    if (!vs || !fs) return false;

    SDL_GPUVertexBufferDescription vb_desc{};
    vb_desc.slot       = 0;
    vb_desc.pitch      = sizeof(UIVertex);
    vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[3] = {};
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;     attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;     attrs[1].offset = 8;
    attrs[2].location = 2; attrs[2].buffer_slot = 0;
    attrs[2].format   = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;attrs[2].offset = 16;

    SDL_GPUColorTargetDescription ct{};
    ct.format = swap_fmt;
    ct.blend_state.enable_blend           = true;
    ct.blend_state.src_color_blendfactor  = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor  = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op         = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor  = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor  = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op         = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.color_write_mask       = 0xF;

    SDL_GPUGraphicsPipelineCreateInfo gpci{};
    gpci.vertex_shader   = vs;
    gpci.fragment_shader = fs;
    gpci.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
    gpci.vertex_input_state.num_vertex_buffers         = 1;
    gpci.vertex_input_state.vertex_attributes          = attrs;
    gpci.vertex_input_state.num_vertex_attributes      = 3;
    gpci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    gpci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    gpci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpci.target_info.color_target_descriptions = &ct;
    gpci.target_info.num_color_targets         = 1;
    gpci.target_info.has_depth_stencil_target  = false;

    r->pipeline = SDL_CreateGPUGraphicsPipeline(r->gpu, &gpci);
    SDL_ReleaseGPUShader(r->gpu, vs);
    SDL_ReleaseGPUShader(r->gpu, fs);
    if (!r->pipeline) {
        LOG_ERROR(kTag, "ui pipeline create: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool create_atlas(UiRenderer* r, const UiFontSpec* fonts, int font_count) {
    if (font_count < 1 || font_count > kMaxFonts) {
        LOG_ERROR(kTag, "font_count %d out of range", font_count);
        return false;
    }
    auto* bitmap = (unsigned char*)std::calloc(1, (size_t)kAtlasW * kAtlasH);
    if (!bitmap) return false;

    // Pack every font / size into the same R8 atlas via stb_truetype's
    // packer. One range per font (ASCII 32..126).
    stbtt_pack_context pc{};
    if (!stbtt_PackBegin(&pc, bitmap, kAtlasW, kAtlasH, 0, 1, nullptr)) {
        LOG_ERROR(kTag, "stbtt_PackBegin failed");
        std::free(bitmap);
        return false;
    }
    stbtt_PackSetOversampling(&pc, 1, 1);

    // Each font needs its TTF buffer to live until PackEnd. Keep them
    // in a local array so we can free them after the bake.
    uint8_t* ttf_buffers[kMaxFonts] = {};
    r->font_count = 0;
    for (int i = 0; i < font_count; ++i) {
        size_t ttf_size = 0;
        ttf_buffers[i] = read_file(fonts[i].path, &ttf_size);
        if (!ttf_buffers[i]) {
            LOG_ERROR(kTag, "open font %s failed", fonts[i].path);
            stbtt_PackEnd(&pc);
            std::free(bitmap);
            for (int j = 0; j < i; ++j) std::free(ttf_buffers[j]);
            return false;
        }
        stbtt_pack_range range{};
        range.font_size                        = fonts[i].px_height;
        range.first_unicode_codepoint_in_range = kFirstGlyph;
        range.num_chars                        = kNumGlyphs;
        range.chardata_for_range               = r->fonts[i].glyphs;
        if (!stbtt_PackFontRanges(&pc, ttf_buffers[i], 0, &range, 1)) {
            LOG_ERROR(kTag, "stbtt_PackFontRanges failed for %s (atlas too small?)",
                      fonts[i].path);
            stbtt_PackEnd(&pc);
            std::free(bitmap);
            for (int j = 0; j <= i; ++j) std::free(ttf_buffers[j]);
            return false;
        }
        r->fonts[i].font_px = fonts[i].px_height;

        // Pull the font's actual vertical metrics so line spacing matches
        // each typeface (NovaRound, Almendra and ProggyClean all need
        // different leading -- a flat `font_px * 1.25` lies for some of
        // them). Falls back to that heuristic on InitFont failure.
        stbtt_fontinfo info;
        if (stbtt_InitFont(&info, ttf_buffers[i],
                           stbtt_GetFontOffsetForIndex(ttf_buffers[i], 0))) {
            int ascent = 0, descent = 0, line_gap = 0;
            stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
            float scale = stbtt_ScaleForPixelHeight(&info, fonts[i].px_height);
            r->fonts[i].line_height = scale * (float)(ascent - descent + line_gap);
        } else {
            r->fonts[i].line_height = fonts[i].px_height * 1.25f;
        }
        r->font_count = i + 1;
    }
    stbtt_PackEnd(&pc);
    for (int i = 0; i < font_count; ++i) std::free(ttf_buffers[i]);

    // Reserve a white pixel at bottom-right for untextured quads.
    bitmap[(kAtlasH - 1) * kAtlasW + (kAtlasW - 1)] = 0xFF;
    r->white_u = ((float)kAtlasW - 0.5f) / (float)kAtlasW;
    r->white_v = ((float)kAtlasH - 0.5f) / (float)kAtlasH;

    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    tci.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width                = (uint32_t)kAtlasW;
    tci.height               = (uint32_t)kAtlasH;
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    r->atlas_tex = SDL_CreateGPUTexture(r->gpu, &tci);
    if (!r->atlas_tex) {
        LOG_ERROR(kTag, "atlas texture create: %s", SDL_GetError());
        std::free(bitmap);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size  = (uint32_t)(kAtlasW * kAtlasH);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r->gpu, &tbci);
    if (!tb) { std::free(bitmap); return false; }
    void* dst = SDL_MapGPUTransferBuffer(r->gpu, tb, false);
    std::memcpy(dst, bitmap, (size_t)kAtlasW * kAtlasH);
    SDL_UnmapGPUTransferBuffer(r->gpu, tb);
    std::free(bitmap);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset          = 0;
    src.pixels_per_row  = (uint32_t)kAtlasW;
    src.rows_per_layer  = (uint32_t)kAtlasH;
    SDL_GPUTextureRegion rgn{};
    rgn.texture = r->atlas_tex;
    rgn.w = (uint32_t)kAtlasW;
    rgn.h = (uint32_t)kAtlasH;
    rgn.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &rgn, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->gpu, tb);

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter        = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter        = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r->atlas_sampler = SDL_CreateGPUSampler(r->gpu, &sci);
    if (!r->atlas_sampler) {
        LOG_ERROR(kTag, "atlas sampler create: %s", SDL_GetError());
        return false;
    }
    return true;
}

void grow_cpu_buffers(UiRenderer* r, int need_verts, int need_indices) {
    if (need_verts > r->cpu_vert_cap) {
        int cap = r->cpu_vert_cap > 0 ? r->cpu_vert_cap : kInitVertCap;
        while (cap < need_verts) cap *= 2;
        r->verts = (UIVertex*)std::realloc(r->verts, (size_t)cap * sizeof(UIVertex));
        r->cpu_vert_cap = cap;
    }
    if (need_indices > r->cpu_index_cap) {
        int cap = r->cpu_index_cap > 0 ? r->cpu_index_cap : kInitIndexCap;
        while (cap < need_indices) cap *= 2;
        r->indices = (uint32_t*)std::realloc(r->indices, (size_t)cap * sizeof(uint32_t));
        r->cpu_index_cap = cap;
    }
}

void emit_quad(UiRenderer* r,
               float x0, float y0, float x1, float y1,
               float u0, float v0, float u1, float v1,
               uint32_t color) {
    grow_cpu_buffers(r, r->vert_count + 4, r->index_count + 6);
    const uint32_t base = (uint32_t)r->vert_count;
    UIVertex* v = &r->verts[r->vert_count];
    v[0] = { x0, y0, u0, v0, color };
    v[1] = { x1, y0, u1, v0, color };
    v[2] = { x1, y1, u1, v1, color };
    v[3] = { x0, y1, u0, v1, color };
    r->vert_count += 4;
    uint32_t* i = &r->indices[r->index_count];
    i[0] = base + 0;
    i[1] = base + 1;
    i[2] = base + 2;
    i[3] = base + 0;
    i[4] = base + 2;
    i[5] = base + 3;
    r->index_count += 6;
}

void ensure_gpu_buffers(UiRenderer* r) {
    if (r->vert_count > r->gpu_vert_cap) {
        if (r->vbo) SDL_ReleaseGPUBuffer(r->gpu, r->vbo);
        int cap = r->gpu_vert_cap > 0 ? r->gpu_vert_cap : kInitVertCap;
        while (cap < r->vert_count) cap *= 2;
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size  = (uint32_t)(cap * sizeof(UIVertex));
        r->vbo = SDL_CreateGPUBuffer(r->gpu, &bci);
        r->gpu_vert_cap = cap;
    }
    if (r->index_count > r->gpu_index_cap) {
        if (r->ibo) SDL_ReleaseGPUBuffer(r->gpu, r->ibo);
        int cap = r->gpu_index_cap > 0 ? r->gpu_index_cap : kInitIndexCap;
        while (cap < r->index_count) cap *= 2;
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        bci.size  = (uint32_t)(cap * sizeof(uint32_t));
        r->ibo = SDL_CreateGPUBuffer(r->gpu, &bci);
        r->gpu_index_cap = cap;
    }
}

bool font_index_valid(const UiRenderer* r, int font_id) {
    return font_id >= 0 && font_id < r->font_count;
}

} // namespace

UiRenderer* ui_renderer_create(SDL_GPUDevice* gpu, SDL_GPUTextureFormat swap_fmt,
                               const UiFontSpec* fonts, int font_count,
                               const char* shader_dir) {
    auto* r = new UiRenderer();
    r->gpu = gpu;
    if (!create_atlas(r, fonts, font_count)) { ui_renderer_destroy(r); return nullptr; }
    if (!create_pipeline(r, swap_fmt, shader_dir)) { ui_renderer_destroy(r); return nullptr; }
    char fonts_summary[256] = {};
    for (int i = 0; i < font_count; ++i) {
        std::snprintf(fonts_summary + std::strlen(fonts_summary),
                      sizeof(fonts_summary) - std::strlen(fonts_summary),
                      "%s%s@%.1fpx", i == 0 ? "" : ", ",
                      fonts[i].path, fonts[i].px_height);
    }
    LOG_INFO(kTag, "ui_renderer: atlas=%dx%d, %d font(s): %s",
             kAtlasW, kAtlasH, font_count, fonts_summary);
    return r;
}

void ui_renderer_destroy(UiRenderer* r) {
    if (!r) return;
    if (r->vbo)           SDL_ReleaseGPUBuffer(r->gpu, r->vbo);
    if (r->ibo)           SDL_ReleaseGPUBuffer(r->gpu, r->ibo);
    if (r->atlas_tex)     SDL_ReleaseGPUTexture(r->gpu, r->atlas_tex);
    if (r->atlas_sampler) SDL_ReleaseGPUSampler(r->gpu, r->atlas_sampler);
    if (r->pipeline)      SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->pipeline);
    std::free(r->verts);
    std::free(r->indices);
    delete r;
}

void ui_renderer_begin_frame(UiRenderer* r, int window_w, int window_h) {
    r->vert_count  = 0;
    r->index_count = 0;
    r->window_w    = (float)window_w;
    r->window_h    = (float)window_h;
}

void ui_renderer_queue_rect(UiRenderer* r, float x, float y, float w, float h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    emit_quad(r, x, y, x + w, y + h, r->white_u, r->white_v, r->white_u, r->white_v, color);
}

float ui_renderer_queue_text(UiRenderer* r, int font_id, float x, float y,
                             const char* text, uint32_t color) {
    if (!text || !font_index_valid(r, font_id)) return x;
    const UiFont& F = r->fonts[font_id];
    float cx = x, cy = y;
    while (*text) {
        unsigned char ch = (unsigned char)*text++;
        if (ch < kFirstGlyph || ch >= kFirstGlyph + kNumGlyphs) continue;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad((stbtt_packedchar*)F.glyphs, kAtlasW, kAtlasH,
                            ch - kFirstGlyph, &cx, &cy, &q, 0);
        emit_quad(r, q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1, color);
    }
    return cx;
}

float ui_renderer_text_width(const UiRenderer* r, int font_id, const char* text) {
    if (!text || !font_index_valid(r, font_id)) return 0.0f;
    const UiFont& F = r->fonts[font_id];
    float cx = 0.0f, cy = 0.0f;
    while (*text) {
        unsigned char ch = (unsigned char)*text++;
        if (ch < kFirstGlyph || ch >= kFirstGlyph + kNumGlyphs) continue;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad((stbtt_packedchar*)F.glyphs, kAtlasW, kAtlasH,
                            ch - kFirstGlyph, &cx, &cy, &q, 0);
    }
    return cx;
}

float ui_renderer_line_height(const UiRenderer* r, int font_id) {
    if (!font_index_valid(r, font_id)) return 0.0f;
    return r->fonts[font_id].line_height;
}

void ui_renderer_prepare(UiRenderer* r, SDL_GPUCommandBuffer* cmd) {
    if (r->vert_count == 0 || r->index_count == 0) return;
    ensure_gpu_buffers(r);

    const uint32_t vbytes = (uint32_t)(r->vert_count * sizeof(UIVertex));
    const uint32_t ibytes = (uint32_t)(r->index_count * sizeof(uint32_t));
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size  = vbytes + ibytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r->gpu, &tbci);
    if (!tb) return;
    uint8_t* dst = (uint8_t*)SDL_MapGPUTransferBuffer(r->gpu, tb, true);
    std::memcpy(dst,          r->verts,   vbytes);
    std::memcpy(dst + vbytes, r->indices, ibytes);
    SDL_UnmapGPUTransferBuffer(r->gpu, tb);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src_v{ tb, 0 };
    SDL_GPUBufferRegion           dst_v{ r->vbo, 0, vbytes };
    SDL_UploadToGPUBuffer(cp, &src_v, &dst_v, true);
    SDL_GPUTransferBufferLocation src_i{ tb, vbytes };
    SDL_GPUBufferRegion           dst_i{ r->ibo, 0, ibytes };
    SDL_UploadToGPUBuffer(cp, &src_i, &dst_i, true);
    SDL_EndGPUCopyPass(cp);
    SDL_ReleaseGPUTransferBuffer(r->gpu, tb);
}

void ui_renderer_draw(UiRenderer* r, SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
    if (r->vert_count == 0 || r->index_count == 0) return;
    if (r->window_w <= 0 || r->window_h <= 0)      return;

    SDL_BindGPUGraphicsPipeline(pass, r->pipeline);

    float inv_size[2] = { 1.0f / r->window_w, 1.0f / r->window_h };
    SDL_PushGPUVertexUniformData(cmd, 0, inv_size, sizeof(inv_size));

    SDL_GPUBufferBinding vb{};
    vb.buffer = r->vbo;
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUBufferBinding ib{};
    ib.buffer = r->ibo;
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = r->atlas_tex;
    tsb.sampler = r->atlas_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

    SDL_DrawGPUIndexedPrimitives(pass, (uint32_t)r->index_count, 1, 0, 0, 0);
}

} // namespace engine
