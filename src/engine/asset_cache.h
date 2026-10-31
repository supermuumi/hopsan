#pragma once

#include "shared/engine_api.h"

#include <SDL3/SDL.h>

namespace engine {

constexpr int kMaxAssets = 256;

enum AssetKind : uint32_t {
    ASSET_TEXTURE = 1,
    ASSET_MESH    = 2,
};

struct Texture {
    SDL_GPUTexture* gpu = nullptr;
    uint32_t        w   = 0;
    uint32_t        h   = 0;
};

struct Mesh {
    SDL_GPUBuffer* vbo = nullptr;     // float32: [pos.x pos.y pos.z uv.u uv.v] interleaved
    SDL_GPUBuffer* ibo = nullptr;     // uint16
    uint32_t       vertex_count = 0;
    uint32_t       index_count  = 0;
    AssetHandle    texture      = 0;  // #156: model's own base-color texture; 0 = none
};

struct AssetCache {
    SDL_GPUDevice* gpu     = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    int             count  = 0;
    char            paths[kMaxAssets][192];
    AssetKind       kinds[kMaxAssets];
    Texture         textures[kMaxAssets]; // valid if kinds[i] == ASSET_TEXTURE
    Mesh            meshes[kMaxAssets];   // valid if kinds[i] == ASSET_MESH
};

bool        asset_cache_init(AssetCache* c, SDL_GPUDevice* gpu);
void        asset_cache_shutdown(AssetCache* c);

// Loads on first request, returns cached handle thereafter. Returns 0 on
// failure (file missing, decode error, etc.); 0 is reserved as the
// "invalid" handle. Game code is expected to call these once at init.
AssetHandle asset_load_texture(AssetCache* c, const char* path);
AssetHandle asset_load_mesh(AssetCache* c, const char* path);

const Texture* asset_get_texture(const AssetCache* c, AssetHandle h);
const Mesh*    asset_get_mesh(const AssetCache* c, AssetHandle h);

} // namespace engine
