#pragma once

#include "shared/engine_api.h"

namespace engine {

struct UiRenderer;
struct AssetCache;

// Queues every HUD primitive in `cmds` into `ui` (which the caller has
// already begin_frame'd this frame). World-space HudBars get projected via
// view * proj first; screen-space panels + text are queued directly.
//
// The host's render path then calls ui_renderer_prepare + ui_renderer_draw
// to actually emit GPU commands -- this function only fills the queue.
void hud_queue(const RenderCommands* cmds, UiRenderer* ui, const AssetCache* assets,
               int window_w, int window_h);

} // namespace engine
