// Game DLL stub. The host loads this as game.dll / libgame.dylib /
// libgame.so and calls game_init once, then game_tick + game_render
// every frame. The host hot-reloads the DLL when its mtime changes,
// so editing this file + ./build.sh game reloads in-place.

#include "shared/engine_api.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kTag = "game";

struct GameState {
    EnginePlatform* platform;
    uint64_t        frame;
    float           uptime;
};

void cmd_hello(int /*argc*/, const char** /*argv*/, void* user) {
    auto* s = static_cast<GameState*>(user);
    if (!s || !s->platform) return;
    s->platform->log(LOG_LEVEL_INFO, kTag, "hello from the game DLL");
}

} // namespace

GAME_EXPORT void game_init(void* memory, size_t size, EnginePlatform* platform) {
    if (size < sizeof(GameState)) {
        if (platform) platform->log(LOG_LEVEL_ERROR, kTag, "game arena too small");
        return;
    }
    auto* s = static_cast<GameState*>(memory);
    std::memset(s, 0, sizeof(*s));
    s->platform = platform;

    if (platform) {
        platform->console_register("hello", cmd_hello, "log a greeting");
        platform->log(LOG_LEVEL_INFO, kTag, "game initialised");
    }
}

GAME_EXPORT void game_tick(void* memory, const FrameInput* input) {
    auto* s = static_cast<GameState*>(memory);
    if (!s || !input) return;
    s->frame   = input->frame;
    s->uptime += input->dt;
}

GAME_EXPORT void game_render(void* memory, RenderCommands* out) {
    auto* s = static_cast<GameState*>(memory);
    if (!s || !out) return;

    out->clear_color[0] = 0.08f;
    out->clear_color[1] = 0.10f;
    out->clear_color[2] = 0.14f;
    out->clear_color[3] = 1.0f;

    // One screen-space text label, font 1 (Nova Round), white.
    if (out->hud_text_count < kMaxHudTexts) {
        HudText& t = out->hud_texts[out->hud_text_count++];
        t.x = 24.0f;
        t.y = 48.0f;
        t.color = 0xFFFFFFFFu;
        std::snprintf(t.text, sizeof(t.text),
                      "hello world  frame=%llu  t=%.1fs",
                      (unsigned long long)s->frame, (double)s->uptime);
    }
}
