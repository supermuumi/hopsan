#include "engine/renderer.h"
#include "engine/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine {

namespace {

constexpr const char* kRendererTag = "renderer";

uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    auto* buf = (uint8_t*)std::malloc((size_t)n);
    if (std::fread(buf, 1, (size_t)n, f) != (size_t)n) { std::free(buf); std::fclose(f); return nullptr; }
    std::fclose(f);
    *out_size = (size_t)n;
    return buf;
}

// Tries SPIR-V first, then MSL. `base_path` is something like
// "assets/shaders/compiled/tile.vert" -- the loader appends the extension.
SDL_GPUShader* load_shader(SDL_GPUDevice* gpu, const char* base_path, SDL_GPUShaderStage stage,
                           uint32_t uniform_buffers, uint32_t samplers) {
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(gpu);

    struct Variant {
        SDL_GPUShaderFormat fmt;
        const char*         ext;
        const char*         entrypoint;
    };
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
        if (!code) continue; // try next format
        SDL_GPUShaderCreateInfo sci{};
        sci.code_size           = size;
        sci.code                = code;
        sci.entrypoint          = v.entrypoint;
        sci.format              = v.fmt;
        sci.stage               = stage;
        sci.num_uniform_buffers = uniform_buffers;
        sci.num_samplers        = samplers;
        SDL_GPUShader* sh = SDL_CreateGPUShader(gpu, &sci);
        std::free(code);
        if (sh) return sh;
        LOG_WARN(kRendererTag, "SDL_CreateGPUShader %s: %s", path, SDL_GetError());
    }
    LOG_ERROR(kRendererTag, "no shader variant found for %s (supported=0x%x)",
              base_path, (unsigned)supported);
    return nullptr;
}

bool create_tile_pipeline(Renderer* r, SDL_GPUTextureFormat swap_format,
                          const char* dir) {
    char vsp[256], fsp[256];
    std::snprintf(vsp, sizeof(vsp), "%s/tile.vert", dir);
    std::snprintf(fsp, sizeof(fsp), "%s/tile.frag", dir);

    SDL_GPUShader* vs = load_shader(r->gpu, vsp, SDL_GPU_SHADERSTAGE_VERTEX,   1, 0);
    SDL_GPUShader* fs = load_shader(r->gpu, fsp, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 2); // +1 uniform: day/night tint
    if (!vs || !fs) return false;

    // #172: the per-instance buffer is shared with the cube pipeline,
    // which now reads a 5th float (per-instance Z rotation in radians)
    // for autotiled SHAPE_TILED draws. The quad pipeline ignores that
    // attribute but its instance pitch must match the upload's 5-float
    // stride or instance offsets drift.
    SDL_GPUVertexBufferDescription vb_descs[2] = {};
    vb_descs[0].slot = 0; vb_descs[0].pitch = sizeof(float) * 8;
    vb_descs[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vb_descs[1].slot = 1; vb_descs[1].pitch = sizeof(float) * 5;
    vb_descs[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_GPUVertexAttribute attrs[4] = {};
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = sizeof(float)*3;
    attrs[2].location = 2; attrs[2].buffer_slot = 1;
    // Per-instance (x, y, z, brightness) -- z lifts upper floors, w dims
    // floors below the active one (#144).
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 0;
    attrs[3].location = 3; attrs[3].buffer_slot = 0;       // normal (#171)
    attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[3].offset = sizeof(float)*5;

    SDL_GPUColorTargetDescription ct{};
    ct.format = swap_format;
    ct.blend_state.enable_blend = false;

    SDL_GPUGraphicsPipelineCreateInfo gpci{};
    gpci.vertex_shader   = vs;
    gpci.fragment_shader = fs;
    gpci.vertex_input_state.vertex_buffer_descriptions = vb_descs;
    gpci.vertex_input_state.num_vertex_buffers = 2;
    gpci.vertex_input_state.vertex_attributes  = attrs;
    gpci.vertex_input_state.num_vertex_attributes = 4;
    gpci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    gpci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    gpci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpci.target_info.color_target_descriptions = &ct;
    gpci.target_info.num_color_targets = 1;
    // Depth-tested 3D pass (#152). RH_ZO projection -> near maps to 0, so
    // clear depth to 1 and keep the nearer (smaller-z) fragment.
    gpci.target_info.has_depth_stencil_target = true;
    gpci.target_info.depth_stencil_format     = r->depth_fmt;
    gpci.depth_stencil_state.enable_depth_test  = true;
    gpci.depth_stencil_state.enable_depth_write = true;
    gpci.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    r->tile_pipeline = SDL_CreateGPUGraphicsPipeline(r->gpu, &gpci);
    SDL_ReleaseGPUShader(r->gpu, vs);
    SDL_ReleaseGPUShader(r->gpu, fs);
    if (!r->tile_pipeline) {
        LOG_ERROR(kRendererTag, "tile pipeline create: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool create_mesh_pipeline(Renderer* r, SDL_GPUTextureFormat swap_format,
                          const char* dir) {
    char vsp[256], fsp[256];
    std::snprintf(vsp, sizeof(vsp), "%s/mesh.vert", dir);
    std::snprintf(fsp, sizeof(fsp), "%s/mesh.frag", dir);

    // vertex stage uses two uniform buffers (camera, model)
    SDL_GPUShader* vs = load_shader(r->gpu, vsp, SDL_GPU_SHADERSTAGE_VERTEX,   2, 0);
    SDL_GPUShader* fs = load_shader(r->gpu, fsp, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 2); // +1 uniform: day/night tint
    if (!vs || !fs) return false;

    SDL_GPUVertexBufferDescription vb_desc{};
    vb_desc.slot = 0; vb_desc.pitch = sizeof(float) * 8;
    vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[3] = {};
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = sizeof(float)*3;
    attrs[2].location = 2; attrs[2].buffer_slot = 0;       // normal (#171)
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[2].offset = sizeof(float)*5;

    SDL_GPUColorTargetDescription ct{};
    ct.format = swap_format;
    ct.blend_state.enable_blend = false;

    SDL_GPUGraphicsPipelineCreateInfo gpci{};
    gpci.vertex_shader   = vs;
    gpci.fragment_shader = fs;
    gpci.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
    gpci.vertex_input_state.num_vertex_buffers = 1;
    gpci.vertex_input_state.vertex_attributes  = attrs;
    gpci.vertex_input_state.num_vertex_attributes = 3;
    gpci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    gpci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    gpci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpci.target_info.color_target_descriptions = &ct;
    gpci.target_info.num_color_targets = 1;
    gpci.target_info.has_depth_stencil_target = true;          // #152
    gpci.target_info.depth_stencil_format     = r->depth_fmt;
    gpci.depth_stencil_state.enable_depth_test  = true;
    gpci.depth_stencil_state.enable_depth_write = true;
    gpci.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    r->mesh_pipeline = SDL_CreateGPUGraphicsPipeline(r->gpu, &gpci);
    SDL_ReleaseGPUShader(r->gpu, vs);
    SDL_ReleaseGPUShader(r->gpu, fs);
    if (!r->mesh_pipeline) {
        LOG_ERROR(kRendererTag, "mesh pipeline create: %s", SDL_GetError());
        return false;
    }
    return true;
}

// Cube tile pipeline (#152): same instance layout as the tile pipeline
// (slot 1 = per-instance xyz+brightness), but slot 0 is the unit cube
// geometry and the vertex stage takes a second uniform (height). Cull
// is disabled so we don't depend on getting box winding exactly right;
// depth testing resolves overlap.
bool create_cube_pipeline(Renderer* r, SDL_GPUTextureFormat swap_format,
                          const char* dir) {
    char vsp[256], fsp[256];
    std::snprintf(vsp, sizeof(vsp), "%s/cube.vert", dir);
    std::snprintf(fsp, sizeof(fsp), "%s/tile.frag", dir);   // shares the tile frag

    SDL_GPUShader* vs = load_shader(r->gpu, vsp, SDL_GPU_SHADERSTAGE_VERTEX,   2, 0);
    SDL_GPUShader* fs = load_shader(r->gpu, fsp, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 2);
    if (!vs || !fs) return false;

    // #172: 5-float per-instance stride (x, y, z, brightness, rot_radians).
    // The new rot float (location 4) is the per-instance Z rotation
    // composed into the cube/mesh transform for SHAPE_TILED autotiling;
    // non-TILED draws upload 0 for that slot.
    SDL_GPUVertexBufferDescription vb_descs[2] = {};
    vb_descs[0].slot = 0; vb_descs[0].pitch = sizeof(float) * 8;
    vb_descs[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vb_descs[1].slot = 1; vb_descs[1].pitch = sizeof(float) * 5;
    vb_descs[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_GPUVertexAttribute attrs[5] = {};
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = sizeof(float)*3;
    attrs[2].location = 2; attrs[2].buffer_slot = 1;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 0;
    attrs[3].location = 3; attrs[3].buffer_slot = 0;       // normal (#171)
    attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[3].offset = sizeof(float)*5;
    attrs[4].location = 4; attrs[4].buffer_slot = 1;       // #172: per-instance Z rotation (radians)
    attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;  attrs[4].offset = sizeof(float)*4;

    SDL_GPUColorTargetDescription ct{};
    ct.format = swap_format;
    ct.blend_state.enable_blend = false;

    SDL_GPUGraphicsPipelineCreateInfo gpci{};
    gpci.vertex_shader   = vs;
    gpci.fragment_shader = fs;
    gpci.vertex_input_state.vertex_buffer_descriptions = vb_descs;
    gpci.vertex_input_state.num_vertex_buffers = 2;
    gpci.vertex_input_state.vertex_attributes  = attrs;
    gpci.vertex_input_state.num_vertex_attributes = 5;
    gpci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    gpci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    gpci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpci.target_info.color_target_descriptions = &ct;
    gpci.target_info.num_color_targets = 1;
    gpci.target_info.has_depth_stencil_target = true;
    gpci.target_info.depth_stencil_format     = r->depth_fmt;
    gpci.depth_stencil_state.enable_depth_test  = true;
    gpci.depth_stencil_state.enable_depth_write = true;
    gpci.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    r->cube_pipeline = SDL_CreateGPUGraphicsPipeline(r->gpu, &gpci);
    SDL_ReleaseGPUShader(r->gpu, vs);
    SDL_ReleaseGPUShader(r->gpu, fs);
    if (!r->cube_pipeline) {
        LOG_ERROR(kRendererTag, "cube pipeline create: %s", SDL_GetError());
        return false;
    }
    return true;
}

// Shadow-caster pipelines (#171). Depth-only, no colour target: render the
// scene from the sun into the shadow map. They reuse the cube / mesh vertex
// shaders (fed the light-space matrix as their "camera" uniform) with a
// trivial fragment stage, so geometry transforms stay identical to the main
// pass. `vert_base` is "<dir>/cube.vert" or "<dir>/mesh.vert"; `instanced`
// adds the per-instance buffer slot (cube/mesh tiles) vs a single VBO
// (standalone meshes).
bool create_shadow_pipeline(Renderer* r, const char* dir, const char* vert_name,
                            bool instanced, SDL_GPUGraphicsPipeline** out) {
    char vsp[256], fsp[256];
    std::snprintf(vsp, sizeof(vsp), "%s/%s", dir, vert_name);
    std::snprintf(fsp, sizeof(fsp), "%s/shadow.frag", dir);

    // Both vertex shaders use 2 uniform buffers (camera/light + params/model).
    SDL_GPUShader* vs = load_shader(r->gpu, vsp, SDL_GPU_SHADERSTAGE_VERTEX,   2, 0);
    SDL_GPUShader* fs = load_shader(r->gpu, fsp, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vs || !fs) return false;

    SDL_GPUVertexBufferDescription vb_descs[2] = {};
    vb_descs[0].slot = 0; vb_descs[0].pitch = sizeof(float) * 8;
    vb_descs[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    // #172: pitch matches the main cube pipeline -- 5 floats per instance.
    vb_descs[1].slot = 1; vb_descs[1].pitch = sizeof(float) * 5;
    vb_descs[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    // Same attribute layout as the matching main pipeline so the reused
    // vertex shader reads identical inputs. #172: when instanced, also
    // wire up the per-instance rotation attribute (location 4) so the
    // cube vertex shader resolves it identically to the main pass.
    SDL_GPUVertexAttribute attrs[5] = {};
    attrs[0].location = 0; attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = sizeof(float)*3;
    int attr_count;
    if (instanced) {
        attrs[2].location = 2; attrs[2].buffer_slot = 1;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 0;
        attrs[3].location = 3; attrs[3].buffer_slot = 0;
        attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[3].offset = sizeof(float)*5;
        attrs[4].location = 4; attrs[4].buffer_slot = 1;        // #172
        attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;   attrs[4].offset = sizeof(float)*4;
        attr_count = 5;
    } else {
        attrs[2].location = 2; attrs[2].buffer_slot = 0;   // mesh.vert: normal at loc 2
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[2].offset = sizeof(float)*5;
        attr_count = 3;
    }

    SDL_GPUGraphicsPipelineCreateInfo gpci{};
    gpci.vertex_shader   = vs;
    gpci.fragment_shader = fs;
    gpci.vertex_input_state.vertex_buffer_descriptions = vb_descs;
    gpci.vertex_input_state.num_vertex_buffers = instanced ? 2 : 1;
    gpci.vertex_input_state.vertex_attributes  = attrs;
    gpci.vertex_input_state.num_vertex_attributes = attr_count;
    gpci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    gpci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    gpci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    // A small constant + slope depth bias in the rasterizer trims acne on
    // top of the shader-side bias.
    gpci.rasterizer_state.enable_depth_bias = true;
    gpci.rasterizer_state.depth_bias_constant_factor = 1.5f;
    gpci.rasterizer_state.depth_bias_slope_factor    = 2.0f;
    gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpci.target_info.num_color_targets = 0;            // depth-only
    gpci.target_info.has_depth_stencil_target = true;
    gpci.target_info.depth_stencil_format     = r->shadow_fmt;
    gpci.depth_stencil_state.enable_depth_test  = true;
    gpci.depth_stencil_state.enable_depth_write = true;
    gpci.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    *out = SDL_CreateGPUGraphicsPipeline(r->gpu, &gpci);
    SDL_ReleaseGPUShader(r->gpu, vs);
    SDL_ReleaseGPUShader(r->gpu, fs);
    if (!*out) {
        LOG_ERROR(kRendererTag, "shadow pipeline (%s) create: %s", vert_name, SDL_GetError());
        return false;
    }
    return true;
}

// Allocate the shadow map texture + its sampler (#171). Returns false (and
// leaves shadows disabled) if the device can't render to / sample a depth
// format -- the rest of the renderer still works, just without shadows.
bool create_shadow_resources(Renderer* r) {
    const SDL_GPUTextureUsageFlags want =
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (SDL_GPUTextureSupportsFormat(r->gpu, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D, want)) {
        r->shadow_fmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    } else if (SDL_GPUTextureSupportsFormat(r->gpu, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D, want)) {
        r->shadow_fmt = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else {
        LOG_WARN(kRendererTag, "no sampleable depth format; shadows disabled");
        return false;
    }
    r->shadow_dim = 2048;
    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = r->shadow_fmt;
    tci.usage                = want;
    tci.width                = r->shadow_dim;
    tci.height               = r->shadow_dim;
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    r->shadow_map = SDL_CreateGPUTexture(r->gpu, &tci);
    if (!r->shadow_map) {
        LOG_WARN(kRendererTag, "shadow map create failed; shadows disabled: %s", SDL_GetError());
        return false;
    }
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter     = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter     = SDL_GPU_FILTER_NEAREST;
    sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r->shadow_sampler = SDL_CreateGPUSampler(r->gpu, &sci);
    return r->shadow_sampler != nullptr;
}

// Unit cube [0,1]^3, 24 verts (per-face UVs), 36 indices. z is scaled by
// the per-type height in the vertex shader. Cull is off so winding is
// not load-bearing.
bool create_unit_cube(Renderer* r) {
    // [pos.xyz uv.uv normal.xyz] -- 8 floats/vert (#171: normals for lighting).
    auto F = [](float x, float y, float z, float u, float v,
                float nx, float ny, float nz, float* o) {
        o[0]=x; o[1]=y; o[2]=z; o[3]=u; o[4]=v; o[5]=nx; o[6]=ny; o[7]=nz;
    };
    float verts[24 * 8];
    int vi = 0;
    // Each face: corners (bl, br, tr, tl) with UV (0,1)(1,1)(1,0)(0,0).
    struct Quad { float c[4][3]; };
    const Quad faces[6] = {
        {{{0,0,0},{1,0,0},{1,1,0},{0,1,0}}}, // bottom z=0
        {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}}, // top z=1
        {{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}}, // front y=0
        {{{0,1,0},{1,1,0},{1,1,1},{0,1,1}}}, // back y=1
        {{{0,0,0},{0,1,0},{0,1,1},{0,0,1}}}, // left x=0
        {{{1,0,0},{1,1,0},{1,1,1},{1,0,1}}}, // right x=1
    };
    // Outward face normals, matching `faces` order.
    const float fn[6][3] = {
        {0,0,-1}, {0,0,1}, {0,-1,0}, {0,1,0}, {-1,0,0}, {1,0,0},
    };
    const float uv[4][2] = { {0,1},{1,1},{1,0},{0,0} };
    for (int f = 0; f < 6; ++f)
        for (int c = 0; c < 4; ++c) {
            F(faces[f].c[c][0], faces[f].c[c][1], faces[f].c[c][2],
              uv[c][0], uv[c][1], fn[f][0], fn[f][1], fn[f][2], &verts[vi * 8]);
            ++vi;
        }
    uint16_t indices[36];
    for (int f = 0; f < 6; ++f) {
        uint16_t b = (uint16_t)(f * 4);
        int o = f * 6;
        indices[o+0]=b; indices[o+1]=b+1; indices[o+2]=b+2;
        indices[o+3]=b; indices[o+4]=b+2; indices[o+5]=b+3;
    }

    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size  = sizeof(verts);
    r->unit_cube_vbo = SDL_CreateGPUBuffer(r->gpu, &bci);
    bci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bci.size  = sizeof(indices);
    r->unit_cube_ibo = SDL_CreateGPUBuffer(r->gpu, &bci);
    if (!r->unit_cube_vbo || !r->unit_cube_ibo) return false;

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size  = sizeof(verts) + sizeof(indices);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r->gpu, &tci);
    uint8_t* dst = (uint8_t*)SDL_MapGPUTransferBuffer(r->gpu, tb, false);
    std::memcpy(dst, verts, sizeof(verts));
    std::memcpy(dst + sizeof(verts), indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(r->gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{ tb, 0 };
    SDL_GPUBufferRegion           rgn{ r->unit_cube_vbo, 0, sizeof(verts) };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, false);
    src.offset = sizeof(verts);
    rgn = { r->unit_cube_ibo, 0, sizeof(indices) };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->gpu, tb);
    return true;
}

bool create_unit_quad(Renderer* r) {
    // CCW winding viewed from +Z. [pos.xyz uv.uv normal.xyz]; the floor
    // quad always faces straight up (#171).
    const float verts[] = {
        0.0f, 0.0f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
    };
    const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size  = sizeof(verts);
    r->unit_quad_vbo = SDL_CreateGPUBuffer(r->gpu, &bci);

    bci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bci.size  = sizeof(indices);
    r->unit_quad_ibo = SDL_CreateGPUBuffer(r->gpu, &bci);

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size  = sizeof(verts) + sizeof(indices);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r->gpu, &tci);
    uint8_t* dst = (uint8_t*)SDL_MapGPUTransferBuffer(r->gpu, tb, false);
    std::memcpy(dst, verts, sizeof(verts));
    std::memcpy(dst + sizeof(verts), indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(r->gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{ tb, 0 };
    SDL_GPUBufferRegion           rgn{ r->unit_quad_vbo, 0, sizeof(verts) };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, false);

    src.offset = sizeof(verts);
    rgn = { r->unit_quad_ibo, 0, sizeof(indices) };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, false);

    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->gpu, tb);
    return r->unit_quad_vbo && r->unit_quad_ibo;
}

// Sizes the per-instance buffer. `needed` is a count of tile instances;
// each instance is 5 floats (x, y, z, brightness, rot_radians) -- #172.
bool ensure_instance_buffer(Renderer* r, uint32_t needed) {
    if (r->tile_instance_capacity >= needed) return true;
    if (r->tile_instance_vbo) SDL_ReleaseGPUBuffer(r->gpu, r->tile_instance_vbo);
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size  = needed * sizeof(float) * 5;
    r->tile_instance_vbo = SDL_CreateGPUBuffer(r->gpu, &bci);
    if (!r->tile_instance_vbo) { r->tile_instance_capacity = 0; return false; }
    r->tile_instance_capacity = needed;
    return true;
}

} // namespace

bool renderer_init(Renderer* r, SDL_GPUDevice* gpu, SDL_GPUTextureFormat swap_format,
                   const char* shader_dir) {
    r->gpu = gpu;
    // Pick a depth format the device supports (#152). D24_S8 is the most
    // broadly available; fall back to D32_FLOAT.
    if (SDL_GPUTextureSupportsFormat(gpu, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        r->depth_fmt = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else {
        r->depth_fmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    }
    if (!create_tile_pipeline(r, swap_format, shader_dir)) return false;
    if (!create_mesh_pipeline(r, swap_format, shader_dir)) return false;
    if (!create_cube_pipeline(r, swap_format, shader_dir)) return false;
    if (!create_unit_quad(r)) return false;
    if (!create_unit_cube(r)) return false;
    // Shadow mapping (#171) is best-effort: if the device can't provide a
    // sampleable depth target or a pipeline fails, log and carry on without
    // shadows (the shaders fall back to unshadowed sunlight).
    if (create_shadow_resources(r)
        && create_shadow_pipeline(r, shader_dir, "cube.vert", true,  &r->shadow_cube_pipeline)
        && create_shadow_pipeline(r, shader_dir, "mesh.vert", false, &r->shadow_mesh_pipeline)) {
        LOG_INFO(kRendererTag, "shadow map ready (%ux%u)", r->shadow_dim, r->shadow_dim);
    } else {
        LOG_WARN(kRendererTag, "shadows unavailable; rendering sunlight only");
        if (r->shadow_cube_pipeline) { SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->shadow_cube_pipeline); r->shadow_cube_pipeline = nullptr; }
        if (r->shadow_mesh_pipeline) { SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->shadow_mesh_pipeline); r->shadow_mesh_pipeline = nullptr; }
        if (r->shadow_map)     { SDL_ReleaseGPUTexture(r->gpu, r->shadow_map); r->shadow_map = nullptr; }
        if (r->shadow_sampler) { SDL_ReleaseGPUSampler(r->gpu, r->shadow_sampler); r->shadow_sampler = nullptr; }
    }
    return true;
}

SDL_GPUTexture* renderer_acquire_depth(Renderer* r, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return r->depth_tex;
    if (r->depth_tex && r->depth_w == w && r->depth_h == h) return r->depth_tex;
    if (r->depth_tex) SDL_ReleaseGPUTexture(r->gpu, r->depth_tex);
    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = r->depth_fmt;
    tci.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tci.width                = w;
    tci.height               = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    r->depth_tex = SDL_CreateGPUTexture(r->gpu, &tci);
    if (r->depth_tex) { r->depth_w = w; r->depth_h = h; }
    else              { r->depth_w = r->depth_h = 0; }
    return r->depth_tex;
}

void renderer_shutdown(Renderer* r) {
    if (r->tile_pipeline)     SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->tile_pipeline);
    if (r->mesh_pipeline)     SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->mesh_pipeline);
    if (r->cube_pipeline)     SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->cube_pipeline);
    if (r->unit_quad_vbo)     SDL_ReleaseGPUBuffer(r->gpu, r->unit_quad_vbo);
    if (r->unit_quad_ibo)     SDL_ReleaseGPUBuffer(r->gpu, r->unit_quad_ibo);
    if (r->unit_cube_vbo)     SDL_ReleaseGPUBuffer(r->gpu, r->unit_cube_vbo);
    if (r->unit_cube_ibo)     SDL_ReleaseGPUBuffer(r->gpu, r->unit_cube_ibo);
    if (r->tile_instance_vbo) SDL_ReleaseGPUBuffer(r->gpu, r->tile_instance_vbo);
    if (r->depth_tex)         SDL_ReleaseGPUTexture(r->gpu, r->depth_tex);
    if (r->shadow_cube_pipeline) SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->shadow_cube_pipeline);
    if (r->shadow_mesh_pipeline) SDL_ReleaseGPUGraphicsPipeline(r->gpu, r->shadow_mesh_pipeline);
    if (r->shadow_map)        SDL_ReleaseGPUTexture(r->gpu, r->shadow_map);
    if (r->shadow_sampler)    SDL_ReleaseGPUSampler(r->gpu, r->shadow_sampler);
    *r = Renderer{};
}

namespace {

// view_proj = proj * view (both column-major).
void compose_view_proj(const float* view, const float* proj, float* out) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += proj[k*4 + j] * view[i*4 + k];
            out[i*4 + j] = s;
        }
    }
}

// Build the world -> light clip-space matrix for the shadow pass (#171).
// An orthographic, right-handed, zero-to-one-depth frustum looking from the
// sun toward `center`, sized to enclose a `radius` sphere. Column-major,
// stored as m[col*4 + row] to match compose_view_proj. Returns false if the
// sun direction is degenerate.
bool compute_light_vp(const float sun_dir[3], const float center[3],
                      float radius, float* out) {
    auto norm3 = [](float v[3]) {
        float l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        if (l > 1e-6f) { v[0]/=l; v[1]/=l; v[2]/=l; }
        return l;
    };
    auto cross3 = [](const float a[3], const float b[3], float o[3]) {
        o[0] = a[1]*b[2] - a[2]*b[1];
        o[1] = a[2]*b[0] - a[0]*b[2];
        o[2] = a[0]*b[1] - a[1]*b[0];
    };
    auto dot3 = [](const float a[3], const float b[3]) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };

    float L[3] = { sun_dir[0], sun_dir[1], sun_dir[2] };
    if (norm3(L) < 1e-5f || radius <= 0.0f) return false;

    const float margin = radius + 5.0f;
    // `far` is a legacy Windows macro (windef.h), so use far_plane.
    const float far_plane = 2.0f * margin;
    const float eye[3] = { center[0] + L[0]*margin,
                           center[1] + L[1]*margin,
                           center[2] + L[2]*margin };
    float f[3] = { -L[0], -L[1], -L[2] };                // forward = toward center
    norm3(f);
    float up[3] = { 0.0f, 0.0f, 1.0f };                  // world up, unless near-parallel
    if (std::fabs(f[2]) > 0.99f) { up[0]=0; up[1]=1; up[2]=0; }
    float s[3]; cross3(f, up, s); norm3(s);              // right
    float u[3]; cross3(s, f, u);                         // recomputed up

    // Right-handed look-at view (column-major, m[col*4+row]).
    float view[16] = {
        s[0], u[0], -f[0], 0.0f,
        s[1], u[1], -f[1], 0.0f,
        s[2], u[2], -f[2], 0.0f,
        -dot3(s, eye), -dot3(u, eye), dot3(f, eye), 1.0f,
    };

    // Orthographic RH, zero-to-one depth (near = 0, far = 2*margin), the
    // box half-extent = radius on x/y.
    const float inv_r = 1.0f / radius;
    float ortho[16] = {
        inv_r, 0.0f, 0.0f,            0.0f,
        0.0f,  inv_r, 0.0f,           0.0f,
        0.0f,  0.0f, -1.0f / far_plane, 0.0f,
        0.0f,  0.0f,  0.0f,           1.0f,
    };

    compose_view_proj(view, ortho, out);
    return true;
}

} // namespace

bool renderer_prepare(Renderer* r, SDL_GPUCommandBuffer* cmd,
                      const RenderCommands* cmds, const AssetCache* /*assets*/) {
    // Light-space matrix for this frame's shadow pass (#171). Disabled if
    // the device lacks a shadow map or the game didn't request one.
    r->shadows_enabled = (r->shadow_map != nullptr) &&
        compute_light_vp(cmds->sun_dir, cmds->shadow_center, cmds->shadow_radius, r->light_vp);

    // Tile instance buffer: concatenate every visible layer's positions into a
    // single GPU buffer; each layer's draw uses its own start_index/count.
    // (Uniform pushes happen in renderer_draw, after pipeline bind, per SDL_GPU rules.)
    uint32_t total_inst = 0;
    for (int i = 0; i < cmds->tile_layer_count; ++i) {
        total_inst += (uint32_t)cmds->tile_layers[i].count;
    }
    if (total_inst == 0) return true;
    if (!ensure_instance_buffer(r, total_inst)) return false;

    // 5 floats per instance: (x, y, z, brightness, rot_radians) -- #172.
    // The fifth slot is the per-instance Z rotation for SHAPE_TILED
    // autotiled draws; non-TILED layers upload 0 there.
    const uint32_t bytes = total_inst * sizeof(float) * 5;
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size  = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(r->gpu, &tci);
    if (!tb) return false;
    float* dst = (float*)SDL_MapGPUTransferBuffer(r->gpu, tb, true);
    uint32_t offset = 0;
    for (int i = 0; i < cmds->tile_layer_count; ++i) {
        const TileLayer& L = cmds->tile_layers[i];
        if (L.count > 0 && L.positions) {
            std::memcpy(dst + offset * 5, L.positions, L.count * 5 * sizeof(float));
            offset += (uint32_t)L.count;
        }
    }
    SDL_UnmapGPUTransferBuffer(r->gpu, tb);

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{ tb, 0 };
    SDL_GPUBufferRegion           rgn{ r->tile_instance_vbo, 0, bytes };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, true);
    SDL_EndGPUCopyPass(pass);
    SDL_ReleaseGPUTransferBuffer(r->gpu, tb);
    return true;
}

void renderer_draw(Renderer* r, SDL_GPUCommandBuffer* cmd,
                   SDL_GPURenderPass* pass,
                   const RenderCommands* cmds, const AssetCache* assets) {
    // SDL_GPU rule: uniforms must be pushed AFTER the pipeline is bound and
    // BEFORE the draw call. We bind the pipeline first per pass below, then
    // push the camera uniform once, then issue draws.
    float view_proj[16];
    compose_view_proj(cmds->view_matrix, cmds->proj_matrix, view_proj);

    // Fragment lighting uniform. Day/night tint (M3) multiplies the whole
    // scene; the directional sun + ambient (#171) light each fragment by
    // its surface normal; light_vp + shadow_params drive shadow sampling.
    // std140: 4 vec4 + mat4 + vec4 = 36 floats (144 bytes).
    const bool use_shadow = r->shadows_enabled;
    float frag_ubo[36] = {
        cmds->tint_rgb[0], cmds->tint_rgb[1], cmds->tint_rgb[2], 1.0f,
        cmds->sun_dir[0],  cmds->sun_dir[1],  cmds->sun_dir[2],  0.0f,
        cmds->sun_color[0],cmds->sun_color[1],cmds->sun_color[2],0.0f,
        cmds->ambient[0],  cmds->ambient[1],  cmds->ambient[2],  0.0f,
    };
    std::memcpy(&frag_ubo[16], r->light_vp, sizeof(r->light_vp));
    frag_ubo[32] = 0.002f;                                              // depth bias
    frag_ubo[33] = 1.0f / (float)(r->shadow_dim ? r->shadow_dim : 1);   // texel size
    frag_ubo[34] = use_shadow ? 1.0f : 0.0f;                            // enable
    frag_ubo[35] = 0.0f;

    // Shadow map + sampler bound at fragment sampler slot 1 on every draw.
    // When shadows are off the shaders ignore it (enable = 0), but a valid
    // texture must still be bound -- the per-draw tile/mesh texture stands
    // in as a harmless dummy.
    SDL_GPUTexture* sh_tex = use_shadow ? r->shadow_map     : nullptr;
    SDL_GPUSampler* sh_smp = use_shadow ? r->shadow_sampler : nullptr;

    // ---- Tile pass: one draw per floor layer ----------------------------
    if (cmds->tile_layer_count > 0) {
        uint32_t base_instance = 0;
        int bound_shape = -1;   // which (pipeline + geometry) is currently bound
        for (int i = 0; i < cmds->tile_layer_count; ++i) {
            const TileLayer& L = cmds->tile_layers[i];
            // Mirror renderer_prepare's packing condition: every layer it
            // packed must advance base_instance here, even if we skip the
            // draw, or instance offsets drift.
            if (L.count <= 0 || !L.positions) continue;
            const uint32_t first = base_instance;
            base_instance += (uint32_t)L.count;

            // Tile-type texture, if the type sets one. For QUAD / CUBE
            // this is required; for MESH it's an optional override of the
            // model's own base-color texture (#156).
            const Texture* tile_tex = (L.texture != 0) ? asset_get_texture(assets, L.texture) : nullptr;

            if (L.shape == SHAPE_MESH || L.shape == SHAPE_TILED) {
                // Instanced gltf prop (#152) / autotiled variant (#172).
                // Reuses the cube pipeline (slot 0 = mesh geometry
                // pos3+uv2, slot 1 = the shared per-tile instance
                // buffer). Geometry differs per mesh, so we (re)bind it
                // every layer. For TILED the per-instance rotation slot
                // composes a Z rotation in the vertex shader.
                const Mesh* mesh = (L.mesh != 0) ? asset_get_mesh(assets, L.mesh) : nullptr;
                if (!mesh) continue;   // gltf not loaded yet -> skip, offset kept
                // #156: explicit tile texture wins; otherwise the model's
                // own embedded base-color. Untextured models with no tile
                // override are skipped (offset already advanced).
                const Texture* tex = tile_tex ? tile_tex
                                              : asset_get_texture(assets, mesh->texture);
                if (!tex) continue;
                SDL_BindGPUGraphicsPipeline(pass, r->cube_pipeline);
                SDL_PushGPUVertexUniformData(cmd, 0, view_proj, sizeof(view_proj));
                SDL_PushGPUFragmentUniformData(cmd, 0, frag_ubo, sizeof(frag_ubo));
                // Uniform scale on all axes (L.height carries the mesh's
                // scale; see game_render). p = (zscale, xyscale); the
                // second vec4 carries per-type rotation (radians).
                const float sc = (L.height > 0.0f) ? L.height : 1.0f;
                const float mesh_params[12] = { sc, sc, 0, 0,
                                                L.rot_x, L.rot_y, L.rot_z, 0,
                                                L.off_x, L.off_y, L.off_z, 0 };
                SDL_PushGPUVertexUniformData(cmd, 1, mesh_params, sizeof(mesh_params));
                SDL_GPUBufferBinding vb[2] = {};
                vb[0].buffer = mesh->vbo;
                vb[1].buffer = r->tile_instance_vbo;
                SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
                SDL_GPUBufferBinding ib{}; ib.buffer = mesh->ibo;
                SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                SDL_GPUTextureSamplerBinding tsb[2] = {
                    { tex->gpu, assets->sampler },
                    { sh_tex ? sh_tex : tex->gpu, sh_smp ? sh_smp : assets->sampler },
                };
                SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                SDL_DrawGPUIndexedPrimitives(pass, mesh->index_count, (uint32_t)L.count, 0, 0, first);
                bound_shape = -1;   // geometry was per-mesh; force rebind for the next layer
            } else if (L.shape == SHAPE_CUBE) {
                if (!tile_tex) continue;   // cube needs the tile texture
                if (bound_shape != SHAPE_CUBE) {
                    SDL_BindGPUGraphicsPipeline(pass, r->cube_pipeline);
                    SDL_PushGPUVertexUniformData(cmd, 0, view_proj, sizeof(view_proj));
                    SDL_PushGPUFragmentUniformData(cmd, 0, frag_ubo, sizeof(frag_ubo));
                    SDL_GPUBufferBinding vb[2] = {};
                    vb[0].buffer = r->unit_cube_vbo;
                    vb[1].buffer = r->tile_instance_vbo;
                    SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
                    SDL_GPUBufferBinding ib{}; ib.buffer = r->unit_cube_ibo;
                    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                    bound_shape = SHAPE_CUBE;
                }
                // Cube: extrude z by height, leave x/y at tile size; the
                // second vec4 carries per-type rotation (radians).
                const float cube_params[12] = { L.height > 0.0f ? L.height : 1.0f, 1.0f, 0, 0,
                                                L.rot_x, L.rot_y, L.rot_z, 0,
                                                L.off_x, L.off_y, L.off_z, 0 };
                SDL_PushGPUVertexUniformData(cmd, 1, cube_params, sizeof(cube_params));
                SDL_GPUTextureSamplerBinding tsb[2] = {
                    { tile_tex->gpu, assets->sampler },
                    { sh_tex ? sh_tex : tile_tex->gpu, sh_smp ? sh_smp : assets->sampler },
                };
                SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                SDL_DrawGPUIndexedPrimitives(pass, 36, (uint32_t)L.count, 0, 0, first);
            } else if (L.shape == SHAPE_QUAD) {
                if (!tile_tex) continue;   // quad needs the tile texture
                if (bound_shape != SHAPE_QUAD) {
                    SDL_BindGPUGraphicsPipeline(pass, r->tile_pipeline);
                    SDL_PushGPUVertexUniformData(cmd, 0, view_proj, sizeof(view_proj));
                    SDL_PushGPUFragmentUniformData(cmd, 0, frag_ubo, sizeof(frag_ubo));
                    SDL_GPUBufferBinding vb[2] = {};
                    vb[0].buffer = r->unit_quad_vbo;
                    vb[1].buffer = r->tile_instance_vbo;
                    SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
                    SDL_GPUBufferBinding ib{}; ib.buffer = r->unit_quad_ibo;
                    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                    bound_shape = SHAPE_QUAD;
                }
                SDL_GPUTextureSamplerBinding tsb[2] = {
                    { tile_tex->gpu, assets->sampler },
                    { sh_tex ? sh_tex : tile_tex->gpu, sh_smp ? sh_smp : assets->sampler },
                };
                SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                SDL_DrawGPUIndexedPrimitives(pass, 6, (uint32_t)L.count, 0, 0, first);
            }
        }
    }

    // ---- Mesh pass ------------------------------------------------------
    if (cmds->mesh_draw_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, r->mesh_pipeline);
        SDL_PushGPUVertexUniformData(cmd, 0, view_proj, sizeof(view_proj));
        SDL_PushGPUFragmentUniformData(cmd, 0, frag_ubo, sizeof(frag_ubo));
        for (int i = 0; i < cmds->mesh_draw_count; ++i) {
            const MeshDraw& md = cmds->mesh_draws[i];
            const Mesh*    mesh = asset_get_mesh(assets, md.mesh);
            if (!mesh) continue;
            // #156: explicit MeshDraw texture wins; otherwise the model's
            // own embedded base-color (parity with the SHAPE_MESH tile path).
            // Untextured models with no override are skipped.
            const Texture* tex = (md.texture != 0)
                                 ? asset_get_texture(assets, md.texture)
                                 : asset_get_texture(assets, mesh->texture);
            if (!tex) continue;
            SDL_PushGPUVertexUniformData(cmd, 1, md.model_matrix, sizeof(md.model_matrix));
            SDL_GPUBufferBinding vb{};
            vb.buffer = mesh->vbo;
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUBufferBinding ib{};
            ib.buffer = mesh->ibo;
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_GPUTextureSamplerBinding tsb[2] = {
                { tex->gpu, assets->sampler },
                { sh_tex ? sh_tex : tex->gpu, sh_smp ? sh_smp : assets->sampler },
            };
            SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
            SDL_DrawGPUIndexedPrimitives(pass, mesh->index_count, 1, 0, 0, 0);
        }
    }
}

SDL_GPUTexture* renderer_shadow_map(Renderer* r) {
    return r->shadows_enabled ? r->shadow_map : nullptr;
}

void renderer_draw_shadow(Renderer* r, SDL_GPUCommandBuffer* cmd,
                          SDL_GPURenderPass* pass,
                          const RenderCommands* cmds, const AssetCache* assets) {
    if (!r->shadows_enabled || !r->shadow_cube_pipeline || !r->shadow_mesh_pipeline) return;
    const float* lvp = r->light_vp;

    // Caster tiles: cubes + instanced meshes. Floor quads don't cast. The
    // per-instance buffer and offsets match the main pass (filled in
    // renderer_prepare), so base_instance must advance for every packed
    // layer even when we skip drawing it (quads), or offsets drift.
    if (cmds->tile_layer_count > 0) {
        uint32_t base_instance = 0;
        bool cube_bound = false;
        for (int i = 0; i < cmds->tile_layer_count; ++i) {
            const TileLayer& L = cmds->tile_layers[i];
            if (L.count <= 0 || !L.positions) continue;
            const uint32_t first = base_instance;
            base_instance += (uint32_t)L.count;

            if (L.shape == SHAPE_MESH || L.shape == SHAPE_TILED) {
                const Mesh* mesh = (L.mesh != 0) ? asset_get_mesh(assets, L.mesh) : nullptr;
                if (!mesh) continue;
                SDL_BindGPUGraphicsPipeline(pass, r->shadow_cube_pipeline);
                SDL_PushGPUVertexUniformData(cmd, 0, lvp, sizeof(float) * 16);
                const float sc = (L.height > 0.0f) ? L.height : 1.0f;
                const float mesh_params[12] = { sc, sc, 0, 0,
                                                L.rot_x, L.rot_y, L.rot_z, 0,
                                                L.off_x, L.off_y, L.off_z, 0 };
                SDL_PushGPUVertexUniformData(cmd, 1, mesh_params, sizeof(mesh_params));
                SDL_GPUBufferBinding vb[2] = {};
                vb[0].buffer = mesh->vbo;
                vb[1].buffer = r->tile_instance_vbo;
                SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
                SDL_GPUBufferBinding ib{}; ib.buffer = mesh->ibo;
                SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                SDL_DrawGPUIndexedPrimitives(pass, mesh->index_count, (uint32_t)L.count, 0, 0, first);
                cube_bound = false;   // per-mesh geometry; rebind for the next layer
            } else if (L.shape == SHAPE_CUBE) {
                if (!cube_bound) {
                    SDL_BindGPUGraphicsPipeline(pass, r->shadow_cube_pipeline);
                    SDL_PushGPUVertexUniformData(cmd, 0, lvp, sizeof(float) * 16);
                    SDL_GPUBufferBinding vb[2] = {};
                    vb[0].buffer = r->unit_cube_vbo;
                    vb[1].buffer = r->tile_instance_vbo;
                    SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
                    SDL_GPUBufferBinding ib{}; ib.buffer = r->unit_cube_ibo;
                    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                    cube_bound = true;
                }
                const float cube_params[12] = { L.height > 0.0f ? L.height : 1.0f, 1.0f, 0, 0,
                                                L.rot_x, L.rot_y, L.rot_z, 0,
                                                L.off_x, L.off_y, L.off_z, 0 };
                SDL_PushGPUVertexUniformData(cmd, 1, cube_params, sizeof(cube_params));
                SDL_DrawGPUIndexedPrimitives(pass, 36, (uint32_t)L.count, 0, 0, first);
            }
            // SHAPE_QUAD / unknown: no shadow caster; base_instance already advanced.
        }
    }

    // Standalone meshes.
    if (cmds->mesh_draw_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, r->shadow_mesh_pipeline);
        SDL_PushGPUVertexUniformData(cmd, 0, lvp, sizeof(float) * 16);
        for (int i = 0; i < cmds->mesh_draw_count; ++i) {
            const MeshDraw& md = cmds->mesh_draws[i];
            const Mesh* mesh = asset_get_mesh(assets, md.mesh);
            if (!mesh) continue;
            SDL_PushGPUVertexUniformData(cmd, 1, md.model_matrix, sizeof(md.model_matrix));
            SDL_GPUBufferBinding vb{}; vb.buffer = mesh->vbo;
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUBufferBinding ib{}; ib.buffer = mesh->ibo;
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh->index_count, 1, 0, 0, 0);
        }
    }
}

} // namespace engine
