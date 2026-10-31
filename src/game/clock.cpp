// The golf-hole clock. The boss is out for 18 holes; every committed plan
// piece moves the round on (the last piece = the 18th hole). Tiles can be
// tagged in the editor to exist only from / until a hole, and hazards get
// faster as the round goes on.

namespace hop {

const char* kBossLines[kHoles + 1] = {
    "",
    "The boss tees off. Reassemble those plans before he's back!",
    "\"Hole two. My caddie fears me. As he should.\"",
    "\"Bogey. I blame the wind. And you, somehow.\"",
    "\"Birdie on the 4th! Is my laser on schedule?\"",
    "\"The henchmen are back from lunch soon. Look busy.\"",
    "\"Lost a ball in the shark tank. Don't ask.\"",
    "\"Security drones are on autopilot. Mind them.\"",
    "\"Told the caddie about the giga laser. He laughed. Once.\"",
    "\"Halfway round. Halfway to world domination.\"",
    "\"Back nine. Laser self-tests begin shortly.\"",
    "\"Why is my shredder making that noise?\"",
    "\"Hole in one! Somebody write that down.\"",
    "\"Alarm test at the lair. Totally routine. Probably.\"",
    "\"The lasers cycle faster near the end. That's normal. Ish.\"",
    "\"Three to go. Is the coffee machine still on fire?\"",
    "\"Putting for par. Plans ready, yes? YES?\"",
    "\"Last hole coming up. Polish my desk.\"",
    "\"18th green. Driving back now!\"",
};

// 0 at the first hole, 1 at the 18th.
float hecticness(const Play& g) {
    return clampf((float)(g.hole - 1) / (float)(kHoles - 1), 0, 1);
}

// Bring the lair up to date with the clock. Called on room entry and
// respawn (never mid-room).
void apply_hole(GameState& s) {
    Play& g = s.play;
    if (g.hole_target < 1) g.hole_target = 1;
    if (g.hole == g.hole_target) return;
    g.hole = g.hole_target;
    s.world.gate_hole = g.hole;
    g.hole_banner = 4.0f;
    if (g.hole >= kHoles && !g.finale_seen && g.star_total > 0 && g.star_count >= g.star_total) {
        g.finale_seen = true;
        g.finale_timer = 8.0f;
    }
}

void draw_clock_hud(GameState& s, int ox, int oy, int vw, int vh) {
    Play& g = s.play;
    char buf[96];
    // Hole indicator, top right.
    std::snprintf(buf, sizeof(buf), "hole %d/%d", g.hole, kHoles);
    float tw = (float)std::strlen(buf) * char_w();
    float x = (float)(ox + vw) - tw - 12 * g_ui, y = (float)oy + 18 * g_ui;
    srect(x - 14 * g_ui, y - 10 * g_ui, 2 * g_ui, 12 * g_ui, rgba(220, 220, 230));     // flag pole
    srect(x - 12 * g_ui, y - 10 * g_ui, 7 * g_ui, 5 * g_ui, rgba(230, 70, 80));       // flag
    text_sh(x, y, g.hole >= 13 ? rgba(255, 140, 140) : rgba(230, 230, 240), font_small(), buf);

    // Boss message after the hole changes.
    if (g.hole_banner > 0) {
        float a = clampf(g.hole_banner / 0.5f, 0, 1);
        std::snprintf(buf, sizeof(buf), "HOLE %d", g.hole);
        float cx = (float)ox + vw * 0.5f;
        float by = (float)(oy + vh) - 70 * g_ui;
        const char* line = kBossLines[clampi(g.hole, 1, kHoles)];
        float lw = (float)std::strlen(line) * char_w();
        srect(cx - lw * 0.5f - 12 * g_ui, by - 30 * g_ui, lw + 24 * g_ui, 44 * g_ui, rgba(10, 10, 20, (int)(190 * a)));
        text_sh(cx - (float)std::strlen(buf) * 6.5f * g_ui, by - 6 * g_ui, with_alpha(rgba(255, 220, 120), a), font_title(), buf);
        text_sh(cx - lw * 0.5f, by + 8 * g_ui, with_alpha(rgba(235, 235, 245), a), font_small(), line);
    }

    // Finale (placeholder until there's a real ending).
    if (g.finale_timer > 0) {
        float a = clampf(g.finale_timer / 1.0f, 0, 1);
        srect((float)ox, (float)oy, (float)vw, (float)vh, rgba(0, 0, 0, (int)(170 * a)));
        const char* l1 = "The boss strides in, golf bag over his shoulder...";
        const char* l2 = "The plans are complete. The giga laser is GO.";
        const char* l3 = "THE END (for now)";
        float cx = (float)ox + vw * 0.5f, cy = (float)oy + vh * 0.5f;
        text_sh(cx - (float)std::strlen(l1) * char_w() * 0.5f, cy - 20 * g_ui, with_alpha(rgba(235, 235, 245), a), font_small(), l1);
        text_sh(cx - (float)std::strlen(l2) * char_w() * 0.5f, cy, with_alpha(rgba(235, 235, 245), a), font_small(), l2);
        text_sh(cx - (float)std::strlen(l3) * 6.5f * g_ui, cy + 34 * g_ui, with_alpha(rgba(255, 220, 120), a), font_title(), l3);
    }
}

// Late-round alarm lighting: a slow red pulse that grows from hole 13 on.
void draw_alarm(GameState& s, int ox, int oy, int vw, int vh) {
    const Play& g = s.play;
    if (g.hole < 13) return;
    float k = (float)(g.hole - 12) / 6.0f;
    float pulse = 0.5f + 0.5f * std::sin(g.time * 3.0f);
    int a = (int)(10 + 35 * k * pulse);
    srect((float)ox, (float)oy, (float)vw, (float)vh, rgba(255, 20, 30, a));
}

} // namespace hop
