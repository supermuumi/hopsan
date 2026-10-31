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
                if (tile_raw(w, x, y) == T_STAR) ++n;
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

// Hole for a number of committed pieces: the last piece is always hole 18.
int hole_for(int committed, int total) {
    if (total <= 0) return 1;
    return clampi(1 + committed * (kHoles - 1) / total, 1, kHoles);
}

void update_hole_target(GameState& s) {
    s.play.hole_target = hole_for(s.play.star_count, s.play.star_total);
}

void progress_commit(GameState& s) {
    Play& g = s.play;
    if (g.pending_count == 0) return;
    for (int i = 0; i < g.pending_count && g.star_count < kMaxStars; ++i)
        g.stars[g.star_count++] = g.pending[i];
    g.pending_count = 0;
    update_hole_target(s);
    progress_save(s);
}

void progress_rollback(GameState& s) {
    s.play.pending_count = 0;
}

void progress_reset(GameState& s) {
    Play& g = s.play;
    g.star_count = g.pending_count = 0;
    g.hole = g.hole_target = 1;
    g.finale_seen = false;
    progress_save(s);
}

} // namespace hop
