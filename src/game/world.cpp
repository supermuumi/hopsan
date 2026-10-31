// World storage: sparse chunked tile grid, rooms, and the text save format.
//
// Save format (assets/levels/world.txt), one block per room:
//
//   # hopsan world v1
//   room <x> <y> <w> <h> <name>
//   <h lines of exactly w characters, one per tile>
//
// Coordinates are in tiles. Tile characters are listed in kTileInfo
// (game.h). Tiles that are not inside any room are not saved.

namespace hop {

GameState* g_state = nullptr;   // set at the top of every export

void logf(LogLevel lvl, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_state && g_state->platform) g_state->platform->log(lvl, kTag, buf);
}

// ---- Chunks --------------------------------------------------------------

uint32_t chunk_slot(int cx, int cy) { return hash2(cx, cy) & (kChunkHash - 1); }

Chunk* chunk_find(ChunkMap& m, int cx, int cy) {
    uint32_t s = chunk_slot(cx, cy);
    for (int probe = 0; probe < kChunkHash; ++probe) {
        int32_t v = m.hash[s];
        if (v == 0) return nullptr;
        Chunk& c = m.chunks[v - 1];
        if (c.cx == cx && c.cy == cy) return &c;
        s = (s + 1) & (kChunkHash - 1);
    }
    return nullptr;
}

Chunk* chunk_get_or_create(ChunkMap& m, int cx, int cy) {
    uint32_t s = chunk_slot(cx, cy);
    for (int probe = 0; probe < kChunkHash; ++probe) {
        int32_t v = m.hash[s];
        if (v == 0) {
            if (m.count >= kMaxChunks) {
                logf(LOG_LEVEL_ERROR, "world full: out of tile chunks (%d)", kMaxChunks);
                return nullptr;
            }
            Chunk& c = m.chunks[m.count];
            c.cx = cx; c.cy = cy;
            std::memset(c.t, 0, sizeof(c.t));
            m.hash[s] = ++m.count;
            return &c;
        }
        Chunk& c = m.chunks[v - 1];
        if (c.cx == cx && c.cy == cy) return &c;
        s = (s + 1) & (kChunkHash - 1);
    }
    return nullptr;
}

uint8_t tile_get(const World& w, int tx, int ty) {
    Chunk* c = chunk_find(const_cast<ChunkMap&>(w.map), tx >> kChunkShift, ty >> kChunkShift);
    if (!c) return T_EMPTY;
    return c->t[(ty & (kChunk - 1)) * kChunk + (tx & (kChunk - 1))];
}

void tile_set(World& w, int tx, int ty, uint8_t t) {
    Chunk* c = (t == T_EMPTY)
        ? chunk_find(w.map, tx >> kChunkShift, ty >> kChunkShift)
        : chunk_get_or_create(w.map, tx >> kChunkShift, ty >> kChunkShift);
    if (!c) return;
    c->t[(ty & (kChunk - 1)) * kChunk + (tx & (kChunk - 1))] = t;
}

void world_clear(World& w) {
    std::memset(&w.map.hash, 0, sizeof(w.map.hash));
    w.map.count = 0;
    w.room_count = 0;
    std::memset(w.rooms, 0, sizeof(w.rooms));
}

// ---- Rooms ---------------------------------------------------------------

bool rects_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// Room containing tile (tx, ty), or -1.
int room_at_tile(const World& w, int tx, int ty) {
    for (int i = 0; i < w.room_count; ++i) {
        const Room& r = w.rooms[i];
        if (!r.used) continue;
        if (tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h) return i;
    }
    return -1;
}

int room_at_px(const World& w, int px, int py) {
    return room_at_tile(w, floordiv(px, kTile), floordiv(py, kTile));
}

// True if `r` overlaps any used room other than `ignore`.
bool room_overlaps_any(const World& w, const Room& r, int ignore) {
    for (int i = 0; i < w.room_count; ++i) {
        if (i == ignore || !w.rooms[i].used) continue;
        const Room& o = w.rooms[i];
        if (rects_overlap(r.x, r.y, r.w, r.h, o.x, o.y, o.w, o.h)) return true;
    }
    return false;
}

int room_alloc_slot(World& w) {
    for (int i = 0; i < w.room_count; ++i) if (!w.rooms[i].used) return i;
    if (w.room_count >= kMaxRooms) return -1;
    return w.room_count++;
}

// Index of the T_START tile's room + its position. Returns false if none.
bool find_start(const World& w, int* out_tx, int* out_ty) {
    for (int i = 0; i < w.room_count; ++i) {
        const Room& r = w.rooms[i];
        if (!r.used) continue;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                if (tile_get(w, x, y) == T_START) { *out_tx = x; *out_ty = y; return true; }
    }
    return false;
}

// ---- Save / load ---------------------------------------------------------

uint8_t tile_from_char(char ch) {
    for (int i = 0; i < T_COUNT; ++i) if (kTileInfo[i].ch == ch) return (uint8_t)i;
    return T_EMPTY;
}

bool world_save(const World& w, const char* path) {
    char tmp[256];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = std::fopen(tmp, "wb");
    if (!f) {
        logf(LOG_LEVEL_ERROR, "save: can't open %s for writing", tmp);
        return false;
    }
    std::fprintf(f, "# hopsan world v1\n");
    std::fprintf(f, "# room <x> <y> <w> <h> <name>, then h rows of w tiles.\n");
    std::fprintf(f, "# tiles:");
    for (int i = 0; i < T_COUNT; ++i) std::fprintf(f, " %c=%s", kTileInfo[i].ch, kTileInfo[i].name);
    std::fprintf(f, "\n\n");
    int saved = 0;
    static char line[kRoomMaxW + 2];
    for (int i = 0; i < w.room_count; ++i) {
        const Room& r = w.rooms[i];
        if (!r.used) continue;
        std::fprintf(f, "room %d %d %d %d %s\n", r.x, r.y, r.w, r.h, r.name[0] ? r.name : "room");
        for (int y = 0; y < r.h; ++y) {
            for (int x = 0; x < r.w; ++x) {
                uint8_t t = tile_get(w, r.x + x, r.y + y);
                line[x] = kTileInfo[t < T_COUNT ? t : 0].ch;
            }
            line[r.w] = '\n';
            line[r.w + 1] = 0;
            std::fputs(line, f);
        }
        std::fputc('\n', f);
        ++saved;
    }
    bool ok = std::ferror(f) == 0;
    std::fclose(f);
    if (!ok) {
        logf(LOG_LEVEL_ERROR, "save: write error on %s", tmp);
        return false;
    }
    // Keep one backup of the previous save, then swap the new file in.
    char bak[256];
    std::snprintf(bak, sizeof(bak), "%s.bak", path);
    std::remove(bak);
    std::rename(path, bak);
    if (std::rename(tmp, path) != 0) {
        logf(LOG_LEVEL_ERROR, "save: rename %s -> %s failed", tmp, path);
        return false;
    }
    logf(LOG_LEVEL_INFO, "saved %d room(s) to %s", saved, path);
    return true;
}

bool world_load(World& w, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        logf(LOG_LEVEL_WARN, "load: %s not found", path);
        return false;
    }
    world_clear(w);
    static char line[1024];
    int rooms = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "room ", 5) != 0) continue;
        Room r{};
        char name[64] = "room";
        int n = std::sscanf(line + 5, "%d %d %d %d %63s", &r.x, &r.y, &r.w, &r.h, name);
        if (n < 4 || r.w <= 0 || r.h <= 0 || r.w > kRoomMaxW || r.h > kRoomMaxH) {
            logf(LOG_LEVEL_WARN, "load: bad room line: %s", line);
            continue;
        }
        std::snprintf(r.name, sizeof(r.name), "%s", name);
        r.used = true;
        for (int y = 0; y < r.h; ++y) {
            if (!std::fgets(line, sizeof(line), f)) break;
            int len = (int)std::strlen(line);
            for (int x = 0; x < r.w; ++x) {
                char ch = x < len ? line[x] : '.';
                if (ch == '\n' || ch == '\r') ch = '.';
                uint8_t t = tile_from_char(ch);
                if (t != T_EMPTY) tile_set(w, r.x + x, r.y + y, t);
            }
        }
        int slot = room_alloc_slot(w);
        if (slot < 0) { logf(LOG_LEVEL_ERROR, "load: too many rooms"); break; }
        w.rooms[slot] = r;
        ++rooms;
    }
    std::fclose(f);
    logf(LOG_LEVEL_INFO, "loaded %d room(s) from %s", rooms, path);
    return true;
}

// Fallback world when there is no save file: one empty room with a floor.
void world_make_default(World& w) {
    world_clear(w);
    Room& r = w.rooms[0];
    r = Room{};
    r.x = 0; r.y = 0; r.w = kRoomDefW; r.h = kRoomDefH; r.used = true;
    std::snprintf(r.name, sizeof(r.name), "start");
    w.room_count = 1;
    for (int x = 0; x < r.w; ++x) {
        tile_set(w, x, r.h - 1, T_ROCK);
        tile_set(w, x, r.h - 2, T_ROCK);
    }
    for (int y = 0; y < r.h; ++y) {
        tile_set(w, 0, y, T_ROCK);
        tile_set(w, r.w - 1, y, T_ROCK);
    }
    tile_set(w, 4, r.h - 3, T_START);
}

} // namespace hop
