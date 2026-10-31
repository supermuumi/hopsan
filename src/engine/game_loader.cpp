#include "engine/game_loader.h"
#include "engine/log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    typedef HMODULE LibHandle;
    #define LIB_OPEN(p)   LoadLibraryA(p)
    #define LIB_SYM(h, n) (void*)GetProcAddress((h), (n))
    #define LIB_CLOSE(h)  FreeLibrary(h)
#else
    #include <dlfcn.h>
    #include <unistd.h>
    typedef void* LibHandle;
    #define LIB_OPEN(p)   dlopen((p), RTLD_NOW | RTLD_LOCAL)
    #define LIB_SYM(h, n) dlsym((h), (n))
    #define LIB_CLOSE(h)  dlclose(h)
#endif

namespace engine {

namespace {

constexpr const char* kLoaderTag = "loader";

struct Impl {
    std::string  source_path;     // what the build script writes
    std::string  live_path_base;  // e.g. "./libgame_loaded" (no extension)
    std::string  live_path_ext;   // e.g. ".dylib"
    std::string  live_path;       // current copy actually loaded (with counter)
    LibHandle    handle = nullptr;

    // Counter increments on each reload so we never overwrite a currently-
    // mapped library file. macOS in particular crashes if you rewrite a
    // mapped Mach-O in place; a fresh path each time sidesteps it.
    int          counter = 0;

    // mtime of the source file the last time we loaded.
    int64_t      loaded_mtime = 0;

    // If we see a change but the file is still being written, wait one
    // poll where mtime hasn't changed before swapping.
    int64_t      pending_mtime = 0;
};

int64_t mtime_of(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(__APPLE__)
    return (int64_t)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#elif defined(_WIN32)
    return (int64_t)st.st_mtime * 1000;
#else
    return (int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
}

bool copy_file(const char* src, const char* dst) {
    FILE* in = std::fopen(src, "rb");
    if (!in) return false;
    FILE* out = std::fopen(dst, "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[64 * 1024];
    bool ok = true;
    for (;;) {
        size_t n = std::fread(buf, 1, sizeof(buf), in);
        if (n == 0) break;
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    std::fclose(in);
    std::fclose(out);
    return ok;
}

// Splits the source path into a `_loaded` base + extension. The actual live
// path is base + "_<counter>" + ext, regenerated per reload so we never
// rewrite a currently-mapped library (see Impl::counter).
void split_live_path(const std::string& src, std::string* base_out, std::string* ext_out) {
    size_t dot = src.find_last_of('.');
    size_t slash = src.find_last_of("/\\");
    std::string base, ext;
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        base = src + "_loaded";
        ext  = "";
    } else {
        base = src.substr(0, dot) + "_loaded";
        ext  = src.substr(dot);
    }
    // dlopen() with a path that contains no slash searches system library
    // dirs, NOT the cwd, so anchor relative paths to "./" explicitly.
    if (base.find('/') == std::string::npos && base.find('\\') == std::string::npos) {
        base = "./" + base;
    }
    *base_out = base;
    *ext_out  = ext;
}

bool resolve(LibHandle h, GameLoader* gl) {
    gl->init   = (GameInitFn)  LIB_SYM(h, "game_init");
    gl->tick   = (GameTickFn)  LIB_SYM(h, "game_tick");
    gl->render = (GameRenderFn)LIB_SYM(h, "game_render");
    return gl->init && gl->tick && gl->render;
}

bool load_now(GameLoader* gl, Impl* impl, int64_t source_mtime) {
    // Mark this mtime as seen even on failure, so we don't retry the same
    // broken build every frame. Next mtime bump will retry.
    impl->loaded_mtime = source_mtime;

    // Generate a fresh live path so we never overwrite the currently-mapped
    // file. On macOS rewriting a mapped Mach-O corrupts the live pages and
    // crashes the process; on Linux it's tolerated but still wasteful.
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_%03d", ++impl->counter);
    std::string new_live = impl->live_path_base + suffix + impl->live_path_ext;

    if (!copy_file(impl->source_path.c_str(), new_live.c_str())) {
        LOG_ERROR(kLoaderTag, "copy %s -> %s failed",
                  impl->source_path.c_str(), new_live.c_str());
        return false;
    }
    LibHandle next = LIB_OPEN(new_live.c_str());
    if (!next) {
#if defined(_WIN32)
        LOG_ERROR(kLoaderTag, "LoadLibrary %s failed (err %lu)",
                  new_live.c_str(), GetLastError());
#else
        LOG_ERROR(kLoaderTag, "dlopen %s failed: %s", new_live.c_str(), dlerror());
#endif
        std::remove(new_live.c_str());
        return false;
    }
    GameLoader staged{};
    if (!resolve(next, &staged)) {
        LOG_ERROR(kLoaderTag, "missing one of game_init/game_tick/game_render in %s",
                  new_live.c_str());
        LIB_CLOSE(next);
        std::remove(new_live.c_str());
        return false;
    }
    if (impl->handle) LIB_CLOSE(impl->handle);
    // Best-effort delete the previous live copy. If dlclose didn't fully
    // unmap it (likely on macOS), the OS will keep the inode alive until
    // its references drop, and the directory entry is gone immediately.
    if (!impl->live_path.empty()) std::remove(impl->live_path.c_str());

    impl->handle    = next;
    impl->live_path = new_live;
    gl->init   = staged.init;
    gl->tick   = staged.tick;
    gl->render = staged.render;
    gl->loaded = true;
    return true;
}

} // namespace

bool game_loader_init(GameLoader* gl, const char* source_path) {
    auto* impl = new Impl();
    impl->source_path = source_path;
    split_live_path(impl->source_path, &impl->live_path_base, &impl->live_path_ext);
    gl->impl = impl;

    int64_t mt = mtime_of(source_path);
    if (mt == 0) {
        LOG_WARN(kLoaderTag, "game library not found at %s; will keep polling", source_path);
        return false;
    }
    return load_now(gl, impl, mt);
}

void game_loader_shutdown(GameLoader* gl) {
    auto* impl = static_cast<Impl*>(gl->impl);
    if (!impl) return;
    if (impl->handle) LIB_CLOSE(impl->handle);
    if (!impl->live_path.empty()) std::remove(impl->live_path.c_str());
    delete impl;
    gl->impl = nullptr;
    gl->init = nullptr; gl->tick = nullptr; gl->render = nullptr;
    gl->loaded = false;
}

void* game_loader_resolve(GameLoader* gl, const char* symbol) {
    auto* impl = static_cast<Impl*>(gl->impl);
    if (!impl || !impl->handle) return nullptr;
    return LIB_SYM(impl->handle, symbol);
}

bool game_loader_force_reload(GameLoader* gl) {
    auto* impl = static_cast<Impl*>(gl->impl);
    if (!impl) return false;
    int64_t mt = mtime_of(impl->source_path.c_str());
    if (mt == 0) {
        LOG_WARN(kLoaderTag, "force_reload: %s missing", impl->source_path.c_str());
        return false;
    }
    return load_now(gl, impl, mt);
}

bool game_loader_poll(GameLoader* gl) {
    auto* impl = static_cast<Impl*>(gl->impl);
    if (!impl) return false;
    int64_t mt = mtime_of(impl->source_path.c_str());
    if (mt == 0) return false;
    if (mt == impl->loaded_mtime) { impl->pending_mtime = 0; return false; }
    if (mt != impl->pending_mtime) {
        // First time we've seen this new mtime; wait one more poll for the
        // file to stop changing before loading it.
        impl->pending_mtime = mt;
        return false;
    }
    // mtime has been stable for one poll. Load.
    LOG_INFO(kLoaderTag, "reloading game library (mtime=%lld)", (long long)mt);
    impl->pending_mtime = 0;
    return load_now(gl, impl, mt);
}

} // namespace engine
