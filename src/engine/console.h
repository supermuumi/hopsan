#pragma once

#include "shared/engine_api.h"

namespace engine {

// Overlay with command history. Built on ImGui. Toggled by the host (F1).
struct Console;

Console* console_create();
void     console_destroy(Console*);

// Show/hide. Tilde key handler in main calls this.
void console_toggle(Console*);
bool console_is_open(const Console*);

// Draw the ImGui window if open. Call once per frame between
// ImGui::NewFrame and ImGui::Render.
void console_draw(Console*);

// Register a command (host or game). `user` is opaque, passed back on invoke.
// Safe to call multiple times with the same name; later registrations override.
void console_register(Console*, const char* name, ConsoleCommandFn fn,
                      void* user, const char* help);

// Re-register clears all commands whose `user` matches the given pointer.
// Host calls this before reloading the game DLL to drop the game's commands.
void console_clear_owner(Console*, void* user);

// Print into the console scrollback (also goes through engine::log).
void console_print(Console*, const char* msg);

// Built-in: prints help text listing all registered commands.
void console_help(Console*);

} // namespace engine
