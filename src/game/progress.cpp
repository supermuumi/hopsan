// Player progress: collected stars, plus the progress.txt save file.
//
// Rule: stars picked up in the current room are provisional.
// progress_commit() (on leaving a room or touching a checkpoint) makes them
// permanent and saves; progress_rollback() (on death) puts them back.

namespace hop {

constexpr const char* kProgressPath = "progress.txt";   // next to the engine binary

bool star_collected(const Play& g, int tx, int ty) {
    for (int i = 0; i < g.star_count; ++i)
        if (g.stars[i].x == tx && g.stars[i].y == ty) return true;
    return false;
}

int count_world_stars(const World& w) {
    int n = 0;
    for (int i = 0; i < w.room_count; ++i) {
        const Room& r = w.rooms[i];
        if (!r.used) continue;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                if (tile_get(w, x, y) == T_STAR) ++n;
    }
    return n;
}

void progress_save(const GameState& s) {
    const Play& g = s.play;
    FILE* f = std::fopen(kProgressPath, "wb");
    if (!f) { logf(LOG_LEVEL_ERROR, "can't write %s", kProgressPath); return; }
    std::fprintf(f, "hopsan progress v1\n");
    for (int i = 0; i < g.star_count; ++i) std::fprintf(f, "star %d %d\n", g.stars[i].x, g.stars[i].y);
    std::fclose(f);
}

void progress_load(GameState& s) {
    Play& g = s.play;
    g.star_count = 0;
    g.pending_count = 0;
    FILE* f = std::fopen(kProgressPath, "rb");
    if (!f) return;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        int a = 0, b = 0;
        if (std::sscanf(line, "star %d %d", &a, &b) == 2 && g.star_count < kMaxStars)
            g.stars[g.star_count++] = StarPos{ a, b };
    }
    std::fclose(f);
    logf(LOG_LEVEL_INFO, "progress: %d star(s)", g.star_count);
}

void progress_commit(GameState& s) {
    Play& g = s.play;
    if (g.pending_count == 0) return;
    for (int i = 0; i < g.pending_count && g.star_count < kMaxStars; ++i)
        g.stars[g.star_count++] = g.pending[i];
    g.pending_count = 0;
    progress_save(s);
}

void progress_rollback(GameState& s) {
    s.play.pending_count = 0;
}

void progress_reset(GameState& s) {
    Play& g = s.play;
    g.star_count = g.pending_count = 0;
    progress_save(s);
}

} // namespace hop
