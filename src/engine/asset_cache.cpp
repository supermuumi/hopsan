#include "engine/asset_cache.h"
#include "engine/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <cstring>
#include <cstdlib>
#include <cmath>

namespace engine {

namespace {

constexpr const char* kAssetsTag = "assets";

// Look up an existing entry by path. Returns 1-based index (the handle), or 0.
AssetHandle find_existing(const AssetCache* c, const char* path, AssetKind kind) {
    for (int i = 0; i < c->count; ++i) {
        if (c->kinds[i] != kind) continue;
        if (std::strcmp(c->paths[i], path) == 0) return (AssetHandle)(i + 1);
    }
    return 0;
}

// Reserve a new slot, copy the path in, set kind. Returns the new 1-based handle
// or 0 if the cache is full.
int reserve_slot(AssetCache* c, const char* path, AssetKind kind) {
    if (c->count >= kMaxAssets) {
        LOG_ERROR(kAssetsTag, "cache full (%d entries) while adding %s", c->count, path);
        return 0;
    }
    int i = c->count++;
    std::strncpy(c->paths[i], path, sizeof(c->paths[i]) - 1);
    c->paths[i][sizeof(c->paths[i]) - 1] = 0;
    c->kinds[i] = kind;
    return i;
}

// Upload a buffer to the GPU via a transfer buffer + copy pass.
SDL_GPUBuffer* upload_buffer(SDL_GPUDevice* gpu, const void* data, uint32_t size,
                             SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size  = size;
    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(gpu, &bci);
    if (!buf) return nullptr;

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size  = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tci);
    void* dst = SDL_MapGPUTransferBuffer(gpu, tb, false);
    std::memcpy(dst, data, size);
    SDL_UnmapGPUTransferBuffer(gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{ tb, 0 };
    SDL_GPUBufferRegion           rgn{ buf, 0, size };
    SDL_UploadToGPUBuffer(pass, &src, &rgn, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpu, tb);
    return buf;
}

SDL_GPUTexture* upload_texture(SDL_GPUDevice* gpu, const uint8_t* rgba,
                               uint32_t w, uint32_t h) {
    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width                = w;
    tci.height               = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(gpu, &tci);
    if (!tex) return nullptr;

    const uint32_t bytes = w * h * 4;
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size  = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tbci);
    void* dst = SDL_MapGPUTransferBuffer(gpu, tb, false);
    std::memcpy(dst, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    SDL_GPUTextureRegion dst_rgn{};
    dst_rgn.texture = tex;
    dst_rgn.w = w; dst_rgn.h = h; dst_rgn.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst_rgn, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpu, tb);
    return tex;
}

// #156: decode a glTF material's base-color image into a cached texture.
// Handles embedded buffer-view images (typical for .glb) and external
// file URIs (relative to the model, for .gltf). base64 `data:` URIs are
// not handled yet -- they log + return 0, falling back to the tile
// texture. Returns the texture handle, or 0 if absent / unsupported.
AssetHandle load_gltf_base_color(AssetCache* c, const cgltf_image* img,
                                 const char* gltf_path) {
    if (!img) return 0;

    // External file URI: resolve relative to the model and reuse the
    // normal texture loader (which dedupes on resolved path).
    if (img->uri && img->uri[0] && std::strncmp(img->uri, "data:", 5) != 0) {
        char resolved[256];
        const char* slash = std::strrchr(gltf_path, '/');
        if (slash) {
            const int dirlen = (int)(slash - gltf_path) + 1;
            std::snprintf(resolved, sizeof(resolved), "%.*s%s", dirlen, gltf_path, img->uri);
        } else {
            std::snprintf(resolved, sizeof(resolved), "%s", img->uri);
        }
        return asset_load_texture(c, resolved);
    }

    // Embedded buffer view (typical for .glb). Key the cache on the
    // model path + image pointer so two meshes referencing the same
    // embedded image don't double-upload.
    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
        char key[256];
        std::snprintf(key, sizeof(key), "%s#img%p", gltf_path, (const void*)img);
        if (AssetHandle h = find_existing(c, key, ASSET_TEXTURE)) return h;

        const cgltf_buffer_view* bv = img->buffer_view;
        // Use cgltf's accessor: it folds in the offset and returns the
        // decoded view->data when an extension (meshopt/Draco) put the
        // bytes there instead of the raw buffer.
        const uint8_t* bytes = cgltf_buffer_view_data(bv);
        if (!bytes) return 0;
        int w = 0, h = 0, ch = 0;
        uint8_t* px = stbi_load_from_memory(bytes, (int)bv->size, &w, &h, &ch, 4);
        if (!px) {
            LOG_WARN(kAssetsTag, "%s: embedded base-color decode failed: %s",
                     gltf_path, stbi_failure_reason());
            return 0;
        }
        SDL_GPUTexture* tex = upload_texture(c->gpu, px, (uint32_t)w, (uint32_t)h);
        stbi_image_free(px);
        if (!tex) {
            LOG_ERROR(kAssetsTag, "%s: embedded base-color upload failed", gltf_path);
            return 0;
        }
        const int idx = reserve_slot(c, key, ASSET_TEXTURE);
        c->textures[idx].gpu = tex;
        c->textures[idx].w   = (uint32_t)w;
        c->textures[idx].h   = (uint32_t)h;
        LOG_INFO(kAssetsTag, "loaded embedded base-color from %s (%dx%d)", gltf_path, w, h);
        return (AssetHandle)(idx + 1);
    }

    return 0;
}

} // namespace

bool asset_cache_init(AssetCache* c, SDL_GPUDevice* gpu) {
    c->gpu = gpu;
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter        = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter        = SDL_GPU_FILTER_NEAREST;
    sci.mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    c->sampler = SDL_CreateGPUSampler(gpu, &sci);
    return c->sampler != nullptr;
}

void asset_cache_shutdown(AssetCache* c) {
    for (int i = 0; i < c->count; ++i) {
        if (c->kinds[i] == ASSET_TEXTURE && c->textures[i].gpu) {
            SDL_ReleaseGPUTexture(c->gpu, c->textures[i].gpu);
        } else if (c->kinds[i] == ASSET_MESH) {
            if (c->meshes[i].vbo) SDL_ReleaseGPUBuffer(c->gpu, c->meshes[i].vbo);
            if (c->meshes[i].ibo) SDL_ReleaseGPUBuffer(c->gpu, c->meshes[i].ibo);
        }
    }
    if (c->sampler) SDL_ReleaseGPUSampler(c->gpu, c->sampler);
    c->count = 0;
    c->sampler = nullptr;
}

AssetHandle asset_load_texture(AssetCache* c, const char* path) {
    if (AssetHandle h = find_existing(c, path, ASSET_TEXTURE)) return h;
    int w = 0, h = 0, ch = 0;
    uint8_t* px = stbi_load(path, &w, &h, &ch, 4);
    if (!px) {
        LOG_ERROR(kAssetsTag, "stbi_load %s failed: %s", path, stbi_failure_reason());
        return 0;
    }
    SDL_GPUTexture* tex = upload_texture(c->gpu, px, (uint32_t)w, (uint32_t)h);
    stbi_image_free(px);
    if (!tex) {
        LOG_ERROR(kAssetsTag, "GPU upload failed for %s", path);
        return 0;
    }
    int idx = reserve_slot(c, path, ASSET_TEXTURE);
    c->textures[idx].gpu = tex;
    c->textures[idx].w   = (uint32_t)w;
    c->textures[idx].h   = (uint32_t)h;
    LOG_INFO(kAssetsTag, "loaded texture %s (%dx%d)", path, w, h);
    return (AssetHandle)(idx + 1);
}

AssetHandle asset_load_mesh(AssetCache* c, const char* path) {
    if (AssetHandle h = find_existing(c, path, ASSET_MESH)) return h;

    cgltf_options opts{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opts, path, &data);
    if (r != cgltf_result_success) {
        LOG_ERROR(kAssetsTag, "cgltf_parse_file %s failed (%d)", path, (int)r);
        return 0;
    }
    r = cgltf_load_buffers(&opts, data, path);
    if (r != cgltf_result_success) {
        LOG_ERROR(kAssetsTag, "cgltf_load_buffers %s failed (%d)", path, (int)r);
        cgltf_free(data);
        return 0;
    }
    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
        LOG_ERROR(kAssetsTag, "%s has no primitives", path);
        cgltf_free(data);
        return 0;
    }
    const cgltf_primitive& prim = data->meshes[0].primitives[0];

    // Find POSITION, TEXCOORD_0 and NORMAL accessors.
    cgltf_accessor* pos_acc = nullptr;
    cgltf_accessor* uv_acc  = nullptr;
    cgltf_accessor* nrm_acc = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == cgltf_attribute_type_position)  pos_acc = prim.attributes[i].data;
        else if (prim.attributes[i].type == cgltf_attribute_type_texcoord) uv_acc = prim.attributes[i].data;
        else if (prim.attributes[i].type == cgltf_attribute_type_normal)   nrm_acc = prim.attributes[i].data;
    }
    if (!pos_acc || !prim.indices) {
        LOG_ERROR(kAssetsTag, "%s missing POSITION or indices", path);
        cgltf_free(data);
        return 0;
    }
    const cgltf_size vcount = pos_acc->count;
    const cgltf_size icount = prim.indices->count;

    // Interleave [pos.xyz, uv.uv, normal.xyz] as float32 (#171: normals
    // drive directional lighting). UVs default to (0,0); normals are read
    // from the model or, if absent, derived flat from the faces below.
    const cgltf_size stride_floats = 8;
    float* verts = (float*)std::calloc(vcount * stride_floats, sizeof(float));
    for (cgltf_size i = 0; i < vcount; ++i) {
        float p[3] = {0,0,0};
        cgltf_accessor_read_float(pos_acc, i, p, 3);
        verts[i*stride_floats + 0] = p[0];
        verts[i*stride_floats + 1] = p[1];
        verts[i*stride_floats + 2] = p[2];
        float uv[2] = {0,0};
        if (uv_acc) cgltf_accessor_read_float(uv_acc, i, uv, 2);
        verts[i*stride_floats + 3] = uv[0];
        verts[i*stride_floats + 4] = uv[1];
        if (nrm_acc) {
            float n[3] = {0,0,1};
            cgltf_accessor_read_float(nrm_acc, i, n, 3);
            verts[i*stride_floats + 5] = n[0];
            verts[i*stride_floats + 6] = n[1];
            verts[i*stride_floats + 7] = n[2];
        }
    }
    uint16_t* indices = (uint16_t*)std::malloc(icount * sizeof(uint16_t));
    for (cgltf_size i = 0; i < icount; ++i) {
        indices[i] = (uint16_t)cgltf_accessor_read_index(prim.indices, i);
    }

    // #171: no NORMAL attribute -> derive per-vertex normals by summing
    // each adjacent triangle's (area-weighted) face normal, then
    // normalizing. Smooth meshes come out smooth, faceted ones faceted.
    if (!nrm_acc) {
        for (cgltf_size t = 0; t + 2 < icount; t += 3) {
            const uint16_t ia = indices[t], ib = indices[t+1], ic = indices[t+2];
            const float* pa = &verts[ia*stride_floats];
            const float* pb = &verts[ib*stride_floats];
            const float* pc = &verts[ic*stride_floats];
            const float e1[3] = { pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2] };
            const float e2[3] = { pc[0]-pa[0], pc[1]-pa[1], pc[2]-pa[2] };
            const float fn[3] = { e1[1]*e2[2] - e1[2]*e2[1],
                                  e1[2]*e2[0] - e1[0]*e2[2],
                                  e1[0]*e2[1] - e1[1]*e2[0] };
            for (uint16_t v : { ia, ib, ic }) {
                verts[v*stride_floats + 5] += fn[0];
                verts[v*stride_floats + 6] += fn[1];
                verts[v*stride_floats + 7] += fn[2];
            }
        }
        for (cgltf_size i = 0; i < vcount; ++i) {
            float* n = &verts[i*stride_floats + 5];
            const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
            if (len > 1e-8f) { n[0]/=len; n[1]/=len; n[2]/=len; }
            else             { n[0]=0; n[1]=0; n[2]=1; }
        }
    }

    SDL_GPUBuffer* vbo = upload_buffer(c->gpu, verts, (uint32_t)(vcount * stride_floats * sizeof(float)),
                                       SDL_GPU_BUFFERUSAGE_VERTEX);
    SDL_GPUBuffer* ibo = upload_buffer(c->gpu, indices, (uint32_t)(icount * sizeof(uint16_t)),
                                       SDL_GPU_BUFFERUSAGE_INDEX);
    std::free(verts);
    std::free(indices);

    // #156: pull the primitive's base-color texture, if the material has
    // one. First base-color image only for now (one texture per mesh);
    // per-primitive materials are a later refinement. Must happen before
    // cgltf_free -- embedded image bytes live in the freed buffers.
    AssetHandle mesh_tex = 0;
    if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const cgltf_texture* bct = prim.material->pbr_metallic_roughness.base_color_texture.texture;
        if (bct && bct->image) {
            mesh_tex = load_gltf_base_color(c, bct->image, path);
        }
    }

    cgltf_free(data);

    if (!vbo || !ibo) {
        if (vbo) SDL_ReleaseGPUBuffer(c->gpu, vbo);
        if (ibo) SDL_ReleaseGPUBuffer(c->gpu, ibo);
        LOG_ERROR(kAssetsTag, "buffer upload failed for %s", path);
        return 0;
    }
    int idx = reserve_slot(c, path, ASSET_MESH);
    c->meshes[idx].vbo          = vbo;
    c->meshes[idx].ibo          = ibo;
    c->meshes[idx].vertex_count = (uint32_t)vcount;
    c->meshes[idx].index_count  = (uint32_t)icount;
    c->meshes[idx].texture      = mesh_tex;
    LOG_INFO(kAssetsTag, "loaded mesh %s (%zu verts, %zu indices, base-color %s)",
             path, vcount, icount, mesh_tex ? "embedded" : "none");
    return (AssetHandle)(idx + 1);
}

const Texture* asset_get_texture(const AssetCache* c, AssetHandle h) {
    if (h == 0 || (int)h > c->count) return nullptr;
    int i = (int)h - 1;
    if (c->kinds[i] != ASSET_TEXTURE) return nullptr;
    return &c->textures[i];
}

const Mesh* asset_get_mesh(const AssetCache* c, AssetHandle h) {
    if (h == 0 || (int)h > c->count) return nullptr;
    int i = (int)h - 1;
    if (c->kinds[i] != ASSET_MESH) return nullptr;
    return &c->meshes[i];
}

} // namespace engine
