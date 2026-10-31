#pragma once

#include "shared/engine_api.h"

namespace engine {

// Opens engine.log next to the executable. Safe to call twice; second call is a no-op.
void log_init(const char* path = "engine.log");
void log_shutdown();

// Backing function for the LOG_* macros and the EnginePlatform::log pointer.
// Writes to stdout and engine.log. category is a free-form string (subsystem name).
void log_msg(LogLevel level, const char* category, const char* msg);

// printf-style helper. Capped at ~1 KB per message; longer messages get truncated.
void log_fmt(LogLevel level, const char* category, const char* fmt, ...);

} // namespace engine

#define LOG_INFO(cat, fmt, ...)  ::engine::log_fmt(LOG_LEVEL_INFO,  cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  ::engine::log_fmt(LOG_LEVEL_WARN,  cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) ::engine::log_fmt(LOG_LEVEL_ERROR, cat, fmt, ##__VA_ARGS__)
