#include "engine/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace engine {

namespace {
    FILE* g_logfile = nullptr;

    const char* level_tag(LogLevel l) {
        switch (l) {
            case LOG_LEVEL_INFO:  return "INFO";
            case LOG_LEVEL_WARN:  return "WARN";
            case LOG_LEVEL_ERROR: return "ERR ";
        }
        return "????";
    }
}

void log_init(const char* path) {
    if (g_logfile) return;
    g_logfile = std::fopen(path, "w");
    // No fatal if it fails; we still log to stdout.
}

void log_shutdown() {
    if (g_logfile) { std::fclose(g_logfile); g_logfile = nullptr; }
}

void log_msg(LogLevel level, const char* category, const char* msg) {
    if (!category) category = "?";
    if (!msg) msg = "";
    FILE* out = (level == LOG_LEVEL_ERROR) ? stderr : stdout;
    std::fprintf(out, "[%s] %s: %s\n", level_tag(level), category, msg);
    std::fflush(out);
    if (g_logfile) {
        std::fprintf(g_logfile, "[%s] %s: %s\n", level_tag(level), category, msg);
        std::fflush(g_logfile);
    }
}

void log_fmt(LogLevel level, const char* category, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_msg(level, category, buf);
}

} // namespace engine
