#pragma once

#include "engine/asset_cache.h"
#include "shared/engine_api.h"

#include <SDL3/SDL.h>

namespace engine {

struct Renderer {
    SDL_GPUDevice*           gpu              = nullptr;
    SDL_GPUGraphicsPipeline* tile_pipeline    = nullptr;
    SDL_GPUGraphicsPipeline* mesh_pipeline    = nullptr;
    SDL_GPUGraphicsPipeline* cube_pipeline    = nullptr; // #152: extruded box tiles
    SDL_GPUBuffer*           unit_quad_vbo    = nullptr; // 4 verts, [pos.xyz uv.uv]
    SDL_GPUBuffer*           unit_quad_ibo    = nullptr; // 6 uint16 indices
    SDL_GPUBuffer*           unit_cube_vbo    = nullptr; // 24 verts, [pos.xyz uv.uv]
    SDL_GPUBuffer*           unit_cube_ibo    = nullptr; // 36 uint16 indices
    SDL_GPUBuffer*           tile_instance_vbo = nullptr; // (x,y,z,brightness) per instance
    uint32_t                 tile_instance_capacity = 0;  // in instances (4 floats each)

    // Depth buffer (#152). 3D geometry with real height (cube/mesh tile
    // shapes, stacked floors) needs depth testing; the flat-quad floors
    // didn't. Recreated to match the swapchain size on resize.
    SDL_GPUTexture*          depth_tex        = nullptr;
    uint32_t                 depth_w          = 0;
    uint32_t                 depth_h          = 0;
    SDL_GPUTextureFormat     depth_fmt        = SDL_GPU_TEXTUREFORMAT_INVALID;

    // Shadow map (#171). A single directional-light depth map rendered from
    // the sun each frame; the main pass samples it to shade occluded
    // fragments. Cube + mesh tiles and standalone meshes cast; flat floor
    // quads only receive. The caster pipelines reuse the cube/mesh vertex
    // shaders fed the light-space matrix as their "camera", with a
    // depth-only fragment stage.
    SDL_GPUGraphicsPipeline* shadow_cube_pipeline = nullptr; // instanced cube/mesh tiles
    SDL_GPUGraphicsPipeline* shadow_mesh_pipeline = nullptr; // standalone meshes
    SDL_GPUTexture*          shadow_map         = nullptr;
    SDL_GPUSampler*          shadow_sampler     = nullptr;
    uint32_t                 shadow_dim         = 0;
    SDL_GPUTextureFormat     shadow_fmt         = SDL_GPU_TEXTUREFORMAT_INVALID;
    float                    light_vp[16]       = {};   // computed per frame in renderer_prepare
    bool                     shadows_enabled    = false; // this frame requests shadows
};

// (Re)create the depth texture to (w, h) if needed and return it. Used by
// the host to attach a depth-stencil target to the 3D render pass.
SDL_GPUTexture* renderer_acquire_depth(Renderer* r, uint32_t w, uint32_t h);

// The directional-light shadow map (#171), or nullptr if shadows are
// unavailable / not requested this frame. The host attaches it as the
// depth target of a color-less render pass and calls renderer_draw_shadow
// before the main pass. Valid only after renderer_prepare for the frame.
SDL_GPUTexture* renderer_shadow_map(Renderer* r);

// Render the shadow casters (cube + mesh tiles, standalone meshes) into the
// shadow map from the sun's point of view. Call inside a depth-only render
// pass, before the main pass. No-op if shadows are disabled this frame.
void renderer_draw_shadow(Renderer* r,
                          SDL_GPUCommandBuffer* cmd,
                          SDL_GPURenderPass* pass,
                          const RenderCommands* cmds,
                          const AssetCache* assets);

// Initializes pipelines and the static unit quad. Loads SPIR-V from
// `shader_dir` (typically "assets/shaders/compiled").
bool renderer_init(Renderer* r, SDL_GPUDevice* gpu, SDL_GPUTextureFormat swap_format,
                   const char* shader_dir);
void renderer_shutdown(Renderer* r);

// SDL_GPU forbids copy passes inside render passes, so renderer work splits
// into two phases the host invokes around its Begin/EndRenderPass pair:
//
//   renderer_prepare(r, cmd, cmds, assets);   // copy passes, uniform pushes
//   SDL_BeginGPURenderPass(...);
//   renderer_draw(r, cmd, pass, cmds, assets); // binds and draws
//   SDL_EndGPURenderPass(...);
//
// `cmd` is the same command buffer in both calls. Caller owns submission.
bool renderer_prepare(Renderer* r,
                      SDL_GPUCommandBuffer* cmd,
                      const RenderCommands* cmds,
                      const AssetCache* assets);

void renderer_draw(Renderer* r,
                   SDL_GPUCommandBuffer* cmd,
                   SDL_GPURenderPass* pass,
                   const RenderCommands* cmds,
                   const AssetCache* assets);

} // namespace engine
