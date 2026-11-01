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

enum CardKind : uint8_t { CARD_NONE = 0, CARD_HOLE, CARD_FINALE };

// Card timeline (seconds).
constexpr float kCardFadeOut  = 0.30f;  // game -> black
constexpr float kCardFadeIn   = 0.35f;  // black -> card
constexpr float kCardMinShow  = 1.2f;   // can't dismiss before this
constexpr float kCardAutoShow = 8.0f;   // dismisses itself after this
constexpr float kCardTypeRate = 38.0f;  // typewriter characters per second
constexpr float kCardTextAt   = 1.0f;   // typing starts here

void start_card(GameState& s, uint8_t kind) {
    Play& g = s.play;
    if (g.card_kind != CARD_NONE) { g.card_next = kind; return; }
    g.card_kind = kind;
    g.card_hole = g.hole;
    g.card_t = 0;
    g.card_exit_t = -1;
    s.act.confirm_latched = false;
}

const char* card_text(const Play& g) {
    if (g.card_kind == CARD_FINALE)
        return "The boss strides in, golf bag over his shoulder. "
               "\"The plans! All of them! The giga laser is GO!\"";
    return kBossLines[clampi(g.card_hole, 1, kHoles)];
}

float card_typed_end(const Play& g) {
    return kCardTextAt + (float)std::strlen(card_text(g)) / kCardTypeRate;
}

void card_tick(GameState& s, float dt) {
    Play& g = s.play;
    Actions& a = s.act;
    bool press = a.confirm_latched;
    a.confirm_latched = a.jump_latched = a.dash_latched = false;
    if (g.card_exit_t >= 0) {
        g.card_exit_t += dt;
        if (g.card_exit_t >= kCardFadeOut + kCardFadeIn) {
            g.card_kind = CARD_NONE;
            if (g.card_next) { uint8_t k = g.card_next; g.card_next = 0; start_card(s, k); }
        }
        return;
    }
    g.card_t += dt;
    float shown = g.card_t - kCardFadeOut - kCardFadeIn;
    if (press && shown > 0) {
        if (g.card_t < card_typed_end(g)) g.card_t = card_typed_end(g);   // first press: finish typing
        else if (shown >= kCardMinShow) g.card_exit_t = 0;                // then: continue
    }
    if (shown >= kCardAutoShow) g.card_exit_t = 0;
}

// Bring the lair up to date with the clock. Called on room entry and
// respawn (never mid-room).
void apply_hole(GameState& s) {
    Play& g = s.play;
    if (g.hole_target < 1) g.hole_target = 1;
    if (g.hole == g.hole_target) return;
    g.hole = g.hole_target;
    s.world.gate_hole = g.hole;
    start_card(s, CARD_HOLE);
    if (g.hole >= kHoles && !g.finale_seen && g.star_total > 0 && g.star_count >= g.star_total) {
        g.finale_seen = true;
        start_card(s, CARD_FINALE);
    }
}

void draw_clock_hud(GameState& s, int ox, int oy, int vw, int vh) {
    Play& g = s.play;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "hole %d/%d", g.hole, kHoles);
    float tw = (float)std::strlen(buf) * char_w();
    float x = (float)(ox + vw) - tw - 12 * g_ui, y = (float)oy + 18 * g_ui;
    srect(x - 14 * g_ui, y - 10 * g_ui, 2 * g_ui, 12 * g_ui, rgba(220, 220, 230));     // flag pole
    srect(x - 12 * g_ui, y - 10 * g_ui, 7 * g_ui, 5 * g_ui, rgba(230, 70, 80));       // flag
    text_sh(x, y, g.hole >= 13 ? rgba(255, 140, 140) : rgba(230, 230, 240), font_small(), buf);
}

// ---- Card drawing --------------------------------------------------------

// Chunky 5x7 pixel font for the big title lettering.
const char* big_glyph(char c) {
    switch (c) {
        case 'A': return " ### #   ##   #######   ##   ##   #";
        case 'B': return "#### #   ##   ##### #   ##   ##### ";
        case 'C': return " ### #   ##    #    #    #   # ### ";
        case 'E': return "######    #    #### #    #    #####";
        case 'H': return "#   ##   ##   #######   ##   ##   #";
        case 'K': return "#   ##  # # #  ##   # #  #  # #   #";
        case 'L': return "#    #    #    #    #    #    #####";
        case 'O': return " ### #   ##   ##   ##   ##   # ### ";
        case 'S': return " #####    #     ###     #    ##### ";
        case 'T': return "#####  #    #    #    #    #    #  ";
        case '!': return "  #    #    #    #    #         #  ";
        case '\'': return "  #    #   #                       ";
        case '0': return " ### #   ##  ### # ###  ##   # ### ";
        case '1': return "  #   ##    #    #    #    #   ### ";
        case '2': return " ### #   #    #   #   #   #   #####";
        case '3': return "####     #    # ###     #    ##### ";
        case '4': return "#   ##   ##   ######    #    #    #";
        case '5': return "######    ####     #    #    ##### ";
        case '6': return " ### #    #    #### #   ##   # ### ";
        case '7': return "#####    #   #   #    #    #    #  ";
        case '8': return " ### #   ##   # ### #   ##   # ### ";
        case '9': return " ### #   ##   # ####    #    # ### ";
        default:  return nullptr;
    }
}

float big_text_width(const char* t, float bs) { return (float)std::strlen(t) * 6.0f * bs - bs; }

void big_text(float x, float y, float bs, const char* t, uint32_t color, uint32_t shadow) {
    for (int pass = 0; pass < 2; ++pass) {
        float ox = pass == 0 ? bs * 0.6f : 0, oy = pass == 0 ? bs * 0.6f : 0;
        uint32_t c = pass == 0 ? shadow : color;
        float cx = x;
        for (const char* p = t; *p; ++p, cx += 6.0f * bs) {
            const char* gph = big_glyph(*p);
            if (!gph) continue;
            for (int r = 0; r < 7; ++r)
                for (int col = 0; col < 5; ++col)
                    if (gph[r * 5 + col] == '#')
                        srect(cx + col * bs + ox, y + r * bs + oy, bs, bs, c);
        }
    }
}

float ease_out_back(float t) {
    t = clampf(t, 0, 1);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// The golf course at dusk: sky bands, sun, rolling fairway, the green with
// a waving flag, and a ball arcing in from the tee.
void draw_course(float x0, float y0, float W, float H, float t, int hole, bool panic) {
    float k = H / 360.0f;   // one "world unit" on the card
    // Sky: 10 bands from deep blue to orange (red when the lair panics).
    for (int i = 0; i < 10; ++i) {
        float f = (float)i / 9.0f;
        int r = panic ? (int)(60 + 170 * f) : (int)(30 + 200 * f);
        int g = panic ? (int)(10 + 40 * f) : (int)(30 + 110 * f);
        int b = panic ? (int)(30 + 20 * f) : (int)(80 - 10 * f);
        srect(x0, y0 + H * 0.062f * (float)i, W, H * 0.063f, rgba(r, g, b));
    }
    // Sun.
    float sx = x0 + W * 0.22f, sy = y0 + H * 0.40f;
    for (int i = -6; i <= 6; ++i) {
        float hw = std::sqrt(36.0f - (float)(i * i)) * 4 * k;
        srect(sx - hw, sy + i * 4 * k, hw * 2, 4 * k, panic ? rgba(255, 120, 90) : rgba(255, 210, 120));
    }
    // Rolling hills, two layers, drifting slowly.
    for (int layer = 0; layer < 2; ++layer) {
        uint32_t c = layer == 0 ? rgba(40, 90, 60) : rgba(56, 128, 70);
        float base = y0 + H * (layer == 0 ? 0.46f : 0.54f);
        float col = 6 * k;
        for (float xx = 0; xx < W; xx += col) {
            float u = (xx / W) * 6.2831f * (layer == 0 ? 1.5f : 2.3f) + t * (layer == 0 ? 0.05f : 0.12f);
            float hgt = (layer == 0 ? 22 : 14) * k * (1.0f + std::sin(u) * 0.6f + std::sin(u * 2.7f) * 0.25f);
            srect(x0 + xx, base - hgt, col + 1, y0 + H - (base - hgt), c);
        }
    }
    // Fairway stripes.
    float fy = y0 + H * 0.60f;
    for (int i = 0; i < 9; ++i)
        srect(x0, fy + i * H * 0.047f, W, H * 0.047f, (i & 1) ? rgba(78, 160, 84) : rgba(70, 148, 78));
    // The green + cup + flag.
    float gx = x0 + W * 0.74f, gy = fy + H * 0.02f;
    srect(gx - 70 * k, gy, 140 * k, 10 * k, rgba(110, 196, 104));
    srect(gx - 50 * k, gy - 3 * k, 100 * k, 3 * k, rgba(110, 196, 104));
    srect(gx - 4 * k, gy + 1 * k, 8 * k, 3 * k, rgba(20, 30, 20));                 // cup
    srect(gx - 1 * k, gy - 70 * k, 2 * k, 71 * k, rgba(235, 235, 240));            // pole
    for (int i = 0; i < 12; ++i) {                                                  // waving flag
        float wave = std::sin(t * 6.0f - (float)i * 0.6f) * 2.0f * k;
        float hgt = (14.0f - (float)i * 0.6f) * k;
        srect(gx + 1 * k + i * 3 * k, gy - 70 * k + wave + (14 * k - hgt) * 0.5f, 3 * k, hgt,
              panic ? rgba(255, 60, 60) : rgba(230, 60, 70));
    }
    char num[8];
    std::snprintf(num, sizeof(num), "%d", hole);
    big_text(gx + 8 * k, gy - 66 * k, 1.2f * k, num, rgba(255, 255, 255), rgba(120, 20, 30));
    // Ball: arcs in from the tee, bounces, rolls up to the cup.
    float tee_x = x0 + W * 0.08f;
    float bx, by;
    float u = (t - 0.6f) / 1.1f;
    if (u < 0) { bx = tee_x; by = fy - 3 * k; }
    else if (u < 1) {
        bx = tee_x + (gx - 40 * k - tee_x) * u;
        by = fy - 3 * k - std::sin(u * 3.14159f) * H * 0.45f;
    } else {
        float r = clampf((u - 1.0f) * 1.6f, 0, 1);
        float ease = 1.0f - (1.0f - r) * (1.0f - r);
        bx = gx - 40 * k + 36 * k * ease;
        float bounce = std::fabs(std::sin(r * 3.14159f * 2.0f)) * (1.0f - r) * 12 * k;
        by = gy - 3 * k - bounce;
    }
    srect(bx - 3 * k, by - 3 * k, 6 * k, 6 * k, rgba(250, 250, 250));
    srect(bx - 3 * k, by + 2 * k, 6 * k, 1 * k, rgba(180, 180, 190));
}

void draw_card(GameState& s, int ox, int oy, int vw, int vh) {
    Play& g = s.play;
    if (g.card_kind == CARD_NONE) return;
    float W = (float)vw, H = (float)vh, X = (float)ox, Y = (float)oy;
    float k = H / 360.0f;
    float t = g.card_t;

    // Black level: game -> black, black -> card ... card -> black, black -> game.
    float black;
    bool show_card;
    if (g.card_exit_t >= 0) {
        float e = g.card_exit_t;
        show_card = e < kCardFadeOut;
        black = show_card ? e / kCardFadeOut : 1.0f - (e - kCardFadeOut) / kCardFadeIn;
    } else {
        show_card = t >= kCardFadeOut;
        black = show_card ? 1.0f - (t - kCardFadeOut) / kCardFadeIn : t / kCardFadeOut;
    }
    black = clampf(black, 0, 1);
    float ct = t - kCardFadeOut;   // card-local time
    float ta = 1.0f - black;       // text alpha: quads fade via the overlay, text can't

    if (show_card) {
        bool finale = g.card_kind == CARD_FINALE;
        bool panic = g.card_hole >= 13 || finale;
        draw_course(X, Y, W, H, ct, finale ? kHoles : g.card_hole, panic);

        // Cinematic bars slide in.
        float bar = H * 0.11f * clampf(ct / 0.5f, 0, 1);
        srect(X, Y, W, bar, rgba(0, 0, 0));
        srect(X, Y + H - bar, W, bar, rgba(0, 0, 0));

        // Title slams in from the left with an overshoot.
        char title[24];
        if (finale) std::snprintf(title, sizeof(title), "HE'S BACK!");
        else std::snprintf(title, sizeof(title), "HOLE %d", g.card_hole);
        float bs = 7.0f * k;
        float tw = big_text_width(title, bs);
        float slide = ease_out_back((ct - 0.35f) / 0.55f);
        float tx = X + (W - tw) * 0.5f - (1.0f - slide) * (W * 0.8f);
        float ty = Y + H * 0.18f;
        // Little impact shake when it lands.
        float since = ct - 0.9f;
        if (since > 0 && since < 0.25f) ty += std::sin(since * 60.0f) * (0.25f - since) * 16 * k;
        if (ct > 0.35f)
            big_text(tx, ty, bs, title, finale ? rgba(255, 90, 90) : rgba(255, 220, 110), rgba(40, 20, 10));
        if (!finale && ct > 0.9f) {
            const char* sub = g.card_hole == 1 ? "the boss tees off" : (g.card_hole >= 13 ? "the lair is in a panic" : "the boss plays on");
            float sw = (float)std::strlen(sub) * char_w();
            text_sh(X + (W - sw) * 0.5f, ty + 7 * bs + 22 * k, with_alpha(rgba(240, 240, 250), ta), font_small(), sub);
        }

        // The boss's line, typed out, in a speech panel at the bottom.
        const char* line = card_text(g);
        int n = (int)std::strlen(line);
        int shown_chars = clampi((int)((t - kCardTextAt) * kCardTypeRate), 0, n);
        if (t >= kCardTextAt - 0.2f) {
            float pw = fminf_(W - 40 * k, (float)n * 11.0f * g_ui + 40 * k);
            float px = X + (W - pw) * 0.5f, py = Y + H - H * 0.11f - 60 * k;
            srect(px, py, pw, 46 * k, rgba(12, 12, 24, 225));
            sframe(px, py, pw, 46 * k, 2, rgba(255, 220, 110, 200));
            text_sh(px + 10 * k, py + 12 * k, with_alpha(rgba(255, 220, 110), ta), font_small(),
                    finale ? "THE LAIR" : "THE BOSS, on the course:");
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.*s", shown_chars, line);
            // Long lines wrap once at a space near the middle.
            const char* l1 = buf;
            char second[128] = "";
            if ((float)std::strlen(buf) * 11.0f * g_ui > pw - 20 * k) {
                int cut = (int)std::strlen(buf) / 2;
                while (cut > 0 && buf[cut] != ' ') --cut;
                if (cut > 0) { std::snprintf(second, sizeof(second), "%s", buf + cut + 1); buf[cut] = 0; }
            }
            text_sh(px + 10 * k, py + 30 * k, with_alpha(rgba(245, 245, 250), ta), font_title(), l1);
            if (second[0]) text_sh(px + 10 * k, py + 30 * k + line_h() * 1.6f, with_alpha(rgba(245, 245, 250), ta), font_title(), second);
        }

        // Prompt once it can be dismissed.
        float shown = t - kCardFadeOut - kCardFadeIn;
        if (g.card_exit_t < 0 && shown >= kCardMinShow && t >= card_typed_end(g) && ((int)(s.real_time * 2.5f) & 1)) {
            const char* pr = "press jump to continue";
            float w = (float)std::strlen(pr) * char_w();
            text_sh(X + W - w - 16 * k, Y + H - H * 0.11f * 0.4f, with_alpha(rgba(220, 220, 230), ta), font_small(), pr);
        }
        if (finale && ct > 2.5f) {
            const char* end = "THE END";
            big_text(X + (W - big_text_width(end, 3 * k)) * 0.5f, Y + H * 0.42f, 3 * k, end,
                     rgba(255, 255, 255), rgba(0, 0, 0));
        }
    }
    if (black > 0) srect(X, Y, W, H, rgba(0, 0, 0, (int)(255 * black)));
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
