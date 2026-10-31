// Engine host. Owns the window, the SDL3 + ImGui setup, the dev console,
// the renderer, the UI renderer (native HUD), and hot-reloads a game DLL
// out of `game.cpp` / libgame.so / game.dll / libgame.dylib. The game
// receives a fixed memory arena, an EnginePlatform with logging /
// console / asset services, and a FrameInput each frame; it returns a
// RenderCommands the host draws.

#include "src/engine/log.cpp"
#include "src/engine/console.cpp"
#include "src/engine/game_loader.cpp"
#include "src/engine/asset_cache.cpp"
#include "src/engine/renderer.cpp"
#include "src/engine/hud.cpp"

#include "shared/engine_api.h"
#include "engine/log.h"
#include "engine/console.h"
#include "engine/game_loader.h"
#include "engine/asset_cache.h"
#include "engine/renderer.h"
#include "engine/ui_renderer.h"
#include "engine/hud.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
    #include <direct.h>
    #define skel_chdir(p) _chdir(p)
#else
    #include <unistd.h>
    #define skel_chdir(p) chdir(p)
#endif

#if defined(_WIN32)
    #define GAME_LIB_NAME "game.dll"
#elif defined(__APPLE__)
    #define GAME_LIB_NAME "libgame.dylib"
#else
    #define GAME_LIB_NAME "libgame.so"
#endif

namespace {

constexpr size_t kGameArenaBytes = 64ull * 1024ull * 1024ull;
constexpr const char* kHostTag = "host";

void anchor_to_basepath() {
    const char* base = SDL_GetBasePath();
    if (base) skel_chdir(base);
}

struct HostState {
    bool                want_quit        = false;
    engine::Console*    console          = nullptr;
    engine::GameLoader* loader           = nullptr;
    engine::AssetCache* assets           = nullptr;
    void*               game_memory      = nullptr;
    size_t              game_memory_size = 0;
    EnginePlatform*     platform         = nullptr;
};

HostState* g_host = nullptr;

void platform_log(LogLevel level, const char* category, const char* msg) {
    engine::log_msg(level, category, msg);
}
void platform_console_register(const char* name, ConsoleCommandFn fn, const char* help) {
    if (g_host && g_host->console) {
        engine::console_register(g_host->console, name, fn, g_host->game_memory, help);
    }
}
AssetHandle platform_asset_load(AssetKindAPI kind, const char* path) {
    if (!g_host || !g_host->assets) return 0;
    switch (kind) {
        case ASSET_KIND_TEXTURE: return engine::asset_load_texture(g_host->assets, path);
        case ASSET_KIND_MESH:    return engine::asset_load_mesh(g_host->assets, path);
    }
    return 0;
}

void cmd_quit(int, const char**, void*) { if (g_host) g_host->want_quit = true; }
void cmd_reload_game(int, const char**, void*) {
    if (!g_host || !g_host->loader) return;
    LOG_INFO(kHostTag, "reload_game requested");
    if (engine::game_loader_force_reload(g_host->loader)) {
        engine::console_clear_owner(g_host->console, g_host->game_memory);
        g_host->loader->init(g_host->game_memory, g_host->game_memory_size, g_host->platform);
    }
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    anchor_to_basepath();

    engine::log_init("engine.log");
    LOG_INFO(kHostTag, "engine starting");

    SDL_Window* window = SDL_CreateWindow("hopsan", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { LOG_ERROR(kHostTag, "SDL_CreateWindow: %s", SDL_GetError()); return 1; }

    SDL_GPUDevice* gpu = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
        true, nullptr);
    if (!gpu) { LOG_ERROR(kHostTag, "SDL_CreateGPUDevice: %s", SDL_GetError()); return 1; }
    LOG_INFO(kHostTag, "GPU driver: %s", SDL_GetGPUDeviceDriver(gpu));

    if (!SDL_ClaimWindowForGPUDevice(gpu, window)) {
        LOG_ERROR(kHostTag, "SDL_ClaimWindowForGPUDevice: %s", SDL_GetError()); return 1;
    }
    const SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(gpu, window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device            = gpu;
    init_info.ColorTargetFormat = swap_fmt;
    init_info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init_info);

    engine::AssetCache assets;
    if (!engine::asset_cache_init(&assets, gpu)) { LOG_ERROR(kHostTag, "asset_cache_init"); return 1; }

    engine::Renderer renderer;
    if (!engine::renderer_init(&renderer, gpu, swap_fmt, "assets/shaders/compiled")) {
        LOG_ERROR(kHostTag, "renderer_init failed. Did you run `./build.sh shaders`?");
        return 1;
    }

    const engine::UiFontSpec ui_fonts[] = {
        { "assets/fonts/ProggyClean.ttf",        13.0f },
        { "assets/fonts/NovaRound-Regular.ttf",  22.0f },
        { "assets/fonts/ProggyClean.ttf",        26.0f },
        { "assets/fonts/NovaRound-Regular.ttf",  44.0f },
    };
    engine::UiRenderer* ui = engine::ui_renderer_create(
        gpu, swap_fmt, ui_fonts, (int)(sizeof(ui_fonts)/sizeof(ui_fonts[0])),
        "assets/shaders/compiled");
    if (!ui) { LOG_ERROR(kHostTag, "ui_renderer_create failed"); return 1; }

    void* game_memory = std::calloc(1, kGameArenaBytes);
    if (!game_memory) { LOG_ERROR(kHostTag, "failed to allocate game arena"); return 1; }

    engine::Console* console = engine::console_create();
    engine::GameLoader loader;

    EnginePlatform platform{};
    platform.log              = platform_log;
    platform.console_register = platform_console_register;
    platform.asset_load       = platform_asset_load;

    HostState host;
    host.console = console;
    host.loader  = &loader;
    host.assets  = &assets;
    host.game_memory      = game_memory;
    host.game_memory_size = kGameArenaBytes;
    host.platform         = &platform;
    g_host = &host;

    engine::console_register(console, "quit",        cmd_quit,        nullptr, "exit the engine");
    engine::console_register(console, "reload_game", cmd_reload_game, nullptr, "force-reload game library");

    if (engine::game_loader_init(&loader, GAME_LIB_NAME)) {
        loader.init(game_memory, kGameArenaBytes, &platform);
    } else {
        char* cwd = SDL_GetCurrentDirectory();
        LOG_WARN(kHostTag, "no game library yet; expected %s in %s -- running host-only",
                 GAME_LIB_NAME, cwd ? cwd : "(unknown)");
        if (cwd) SDL_free(cwd);
    }

    FrameInput input{};
    uint64_t prev_ticks = SDL_GetTicksNS();
    SDL_Gamepad* pad = nullptr;

    while (!host.want_quit) {
        const uint64_t now_ticks = SDL_GetTicksNS();
        input.dt = float((now_ticks - prev_ticks) / 1e9);
        prev_ticks = now_ticks;
        ++input.frame;

        std::memset(input.keys_pressed,  0, sizeof(input.keys_pressed));
        std::memset(input.mouse_pressed, 0, sizeof(input.mouse_pressed));
        std::memset(input.pad_pressed,   0, sizeof(input.pad_pressed));
        input.mouse_wheel_y = 0.0f;
        input.quit_requested = false;

        SDL_GetWindowSizeInPixels(window, &input.window_w, &input.window_h);
        // Mouse comes back in window points; scale to pixels so it matches
        // window_w / window_h on hi-dpi (Retina) displays.
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        const float density = SDL_GetWindowPixelDensity(window);
        input.mouse_x = mx * (density > 0.0f ? density : 1.0f);
        input.mouse_y = my * (density > 0.0f ? density : 1.0f);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    host.want_quit = true;
                    input.quit_requested = true;
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    int s = (int)ev.key.scancode;
                    if (s >= 0 && s < 512) {
                        if (!input.keys_held[s]) input.keys_pressed[s] = true;
                        input.keys_held[s] = true;
                    }
                    if (ev.key.scancode == SDL_SCANCODE_F1 && !ev.key.repeat) {
                        engine::console_toggle(console);
                    } else if (!engine::console_is_open(console)) {
                        const SDL_Keymod mod = ev.key.mod;
                        const bool fs_alt_enter =
                            ev.key.scancode == SDL_SCANCODE_RETURN
                            && (mod & SDL_KMOD_ALT)
                            && !ev.key.repeat;
                        const bool fs_cmd_ctrl_f =
                            ev.key.scancode == SDL_SCANCODE_F
                            && (mod & SDL_KMOD_GUI)
                            && (mod & SDL_KMOD_CTRL)
                            && !ev.key.repeat;
                        if (fs_alt_enter || fs_cmd_ctrl_f) {
                            const bool now_fs = !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
                            SDL_SetWindowFullscreen(window, now_fs);
                            LOG_INFO(kHostTag, "fullscreen %s", now_fs ? "on" : "off");
                        }
                    }
                    break;
                }
                case SDL_EVENT_KEY_UP: {
                    int s = (int)ev.key.scancode;
                    if (s >= 0 && s < 512) input.keys_held[s] = false;
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    int b = ev.button.button - 1;
                    if (b >= 0 && b < 3) {
                        if (!input.mouse_down[b]) input.mouse_pressed[b] = true;
                        input.mouse_down[b] = true;
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    int b = ev.button.button - 1;
                    if (b >= 0 && b < 3) input.mouse_down[b] = false;
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL:
                    input.mouse_wheel_y += ev.wheel.y;
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (!pad) {
                        pad = SDL_OpenGamepad(ev.gdevice.which);
                        if (pad) LOG_INFO(kHostTag, "gamepad: %s", SDL_GetGamepadName(pad));
                    }
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (pad && SDL_GetGamepadID(pad) == ev.gdevice.which) {
                        SDL_CloseGamepad(pad);
                        pad = nullptr;
                        std::memset(input.pad_held, 0, sizeof(input.pad_held));
                        LOG_INFO(kHostTag, "gamepad removed");
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                    int b = (int)ev.gbutton.button;
                    if (pad && ev.gbutton.which == SDL_GetGamepadID(pad) && b >= 0 && b < 32) {
                        if (!input.pad_held[b]) input.pad_pressed[b] = true;
                        input.pad_held[b] = true;
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                    int b = (int)ev.gbutton.button;
                    if (pad && ev.gbutton.which == SDL_GetGamepadID(pad) && b >= 0 && b < 32)
                        input.pad_held[b] = false;
                    break;
                }
            }
        }

        input.pad_connected = (pad != nullptr);
        for (int a = 0; a < 6; ++a) {
            input.pad_axes[a] = pad
                ? (float)SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a) / 32767.0f
                : 0.0f;
        }

        if (engine::game_loader_poll(&loader)) {
            engine::console_clear_owner(console, game_memory);
            loader.init(game_memory, kGameArenaBytes, &platform);
        }

        const bool ui_wants_keys  = io.WantCaptureKeyboard || engine::console_is_open(console);
        const bool ui_wants_mouse = io.WantCaptureMouse    || engine::console_is_open(console);
        if (ui_wants_keys) {
            std::memset(input.keys_pressed, 0, sizeof(input.keys_pressed));
            std::memset(input.keys_held,    0, sizeof(input.keys_held));
        }
        if (ui_wants_mouse) {
            std::memset(input.mouse_pressed, 0, sizeof(input.mouse_pressed));
            std::memset(input.mouse_down,    0, sizeof(input.mouse_down));
            input.mouse_wheel_y = 0.0f;
        }

        if (loader.loaded) loader.tick(game_memory, &input);

        RenderCommands cmds{};
        cmds.clear_color[0] = 0.05f;
        cmds.clear_color[1] = 0.07f;
        cmds.clear_color[2] = 0.10f;
        cmds.clear_color[3] = 1.0f;
        if (loader.loaded) loader.render(game_memory, &cmds);

        engine::ui_renderer_begin_frame(ui, input.window_w, input.window_h);
        engine::hud_queue(&cmds, ui, &assets, input.window_w, input.window_h);

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        engine::console_draw(console);

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();

        SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(gpu);
        if (!cb) { LOG_ERROR(kHostTag, "AcquireGPUCommandBuffer: %s", SDL_GetError()); break; }

        SDL_GPUTexture* swap = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cb, window, &swap, nullptr, nullptr)) {
            SDL_SubmitGPUCommandBuffer(cb);
            continue;
        }
        if (swap) {
            engine::renderer_prepare(&renderer, cb, &cmds, &assets);
            engine::ui_renderer_prepare(ui, cb);
            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cb);

            int win_px_w = 0, win_px_h = 0;
            SDL_GetWindowSizeInPixels(window, &win_px_w, &win_px_h);
            SDL_GPUTexture* depth = engine::renderer_acquire_depth(
                &renderer, (uint32_t)win_px_w, (uint32_t)win_px_h);

            SDL_GPUTexture* shadow = engine::renderer_shadow_map(&renderer);
            if (shadow) {
                SDL_GPUDepthStencilTargetInfo sd{};
                sd.texture          = shadow;
                sd.clear_depth      = 1.0f;
                sd.load_op          = SDL_GPU_LOADOP_CLEAR;
                sd.store_op         = SDL_GPU_STOREOP_STORE;
                sd.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
                sd.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                sd.cycle            = true;
                SDL_GPURenderPass* sp = SDL_BeginGPURenderPass(cb, nullptr, 0, &sd);
                engine::renderer_draw_shadow(&renderer, cb, sp, &cmds, &assets);
                SDL_EndGPURenderPass(sp);
            }

            SDL_GPUColorTargetInfo target{};
            target.texture = swap;
            target.clear_color = { cmds.clear_color[0], cmds.clear_color[1],
                                   cmds.clear_color[2], cmds.clear_color[3] };
            target.load_op  = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPUDepthStencilTargetInfo depth_target{};
            const bool have_depth = (depth != nullptr);
            if (have_depth) {
                depth_target.texture          = depth;
                depth_target.clear_depth       = 1.0f;
                depth_target.load_op           = SDL_GPU_LOADOP_CLEAR;
                depth_target.store_op          = SDL_GPU_STOREOP_DONT_CARE;
                depth_target.stencil_load_op   = SDL_GPU_LOADOP_DONT_CARE;
                depth_target.stencil_store_op  = SDL_GPU_STOREOP_DONT_CARE;
                depth_target.cycle             = true;
            }
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
                cb, &target, 1, have_depth ? &depth_target : nullptr);
            engine::renderer_draw(&renderer, cb, pass, &cmds, &assets);
            SDL_EndGPURenderPass(pass);

            target.load_op  = SDL_GPU_LOADOP_LOAD;
            SDL_GPURenderPass* ui_pass = SDL_BeginGPURenderPass(cb, &target, 1, nullptr);
            engine::ui_renderer_draw(ui, cb, ui_pass);
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cb, ui_pass);
            SDL_EndGPURenderPass(ui_pass);
        }
        SDL_SubmitGPUCommandBuffer(cb);

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    LOG_INFO(kHostTag, "shutting down");

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    engine::ui_renderer_destroy(ui);
    engine::renderer_shutdown(&renderer);
    engine::asset_cache_shutdown(&assets);
    engine::game_loader_shutdown(&loader);
    if (pad) SDL_CloseGamepad(pad);
    engine::console_destroy(console);
    std::free(game_memory);

    SDL_ReleaseWindowFromGPUDevice(gpu, window);
    SDL_DestroyGPUDevice(gpu);
    SDL_DestroyWindow(window);
    SDL_Quit();

    engine::log_shutdown();
    return 0;
}
