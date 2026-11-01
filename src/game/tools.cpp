// Game-side ImGui tool windows (drawn into the host's ImGui context via the
// optional game_imgui export). Currently: the Flags window (F4).
//
// Also the shared flag helpers used by both this window and the console
// commands in game.cpp.

namespace hop {

// Take the first flag number that's unnamed and unused, name it. Returns
// the flag id, or 0 (name taken / invalid / all in use).
int flag_create(GameState& s, const char* name) {
    if (!name || !name[0] || find_flag(s.world, name)) return 0;
    for (int f = 1; f <= kMaxFlags; ++f) {
        if (s.world.flag_names[f][0]) continue;
        bool used = false;
        for (int r = 0; r < s.world.room_count && !used; ++r)
            if (s.world.rooms[r].used && room_flag_refs(s, r, f) > 0) used = true;
        if (used) continue;
        std::snprintf(s.world.flag_names[f], sizeof(s.world.flag_names[f]), "%s", name);
        s.ed.dirty = true;
        return f;
    }
    return 0;
}

void flag_clear(GameState& s, int f) {
    if (f <= 0 || f > kMaxFlags) return;
    s.play.flags &= ~(1u << f);
    s.world.gate_flags = s.play.flags;
    progress_save(s);
    if (s.play.room >= 0) build_entities(s, s.play.room);
}

// "Lab (sets), Boiler_Room" -- rooms that use flag f, marking those that set it.
// Returns false if nothing references it.
bool flag_usage(const GameState& s, int f, char* out, size_t cap) {
    out[0] = 0;
    for (int r = 0; r < s.world.room_count; ++r) {
        const Room& rm = s.world.rooms[r];
        if (!rm.used) continue;
        bool sets = false, uses = room_flag_refs(s, r, f) > 0;
        for (int y = rm.y; y < rm.y + rm.h && !sets; ++y)
            for (int x = rm.x; x < rm.x + rm.w && !sets; ++x)
                if (cond_is_target(tile_raw(s.world, x, y)) && cond_flag(tile_cond(s.world, x, y)) == f) sets = true;
        if (!sets && !uses) continue;
        size_t n = std::strlen(out);
        std::snprintf(out + n, cap - n, "%s%s%s", n ? ", " : "", rm.name, sets ? " (sets)" : "");
    }
    return out[0] != 0;
}

// Flag names are single words in the save file.
int flag_name_filter(ImGuiInputTextCallbackData* d) {
    ImWchar c = d->EventChar;
    return (c == ' ' || c == '\t') ? 1 : 0;
}

void draw_flags_window(GameState& s) {
    Editor& e = s.ed;
    if (!e.flags_window) return;
    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 90), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Flags (F4)", &e.flags_window)) { ImGui::End(); return; }

    // Brush.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Brush flag");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##brush", e.flag_brush ? flag_label(s.world, e.flag_brush) : "none")) {
        if (ImGui::Selectable("none", e.flag_brush == 0)) e.flag_brush = 0;
        char usage[8];
        for (int f = 1; f <= kMaxFlags; ++f) {
            if (!s.world.flag_names[f][0] && !flag_usage(s, f, usage, sizeof(usage))) continue;
            if (ImGui::Selectable(flag_label(s.world, f), e.flag_brush == f)) e.flag_brush = (uint8_t)f;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("NOT (present while unset)", &e.flag_not);
    ImGui::TextDisabled("Painted tiles get this condition; switches / fuses set this flag.");

    // New flag.
    ImGui::Separator();
    ImGui::SetNextItemWidth(220);
    bool enter = ImGui::InputTextWithHint("##newflag", "new_flag_name", e.new_flag_name, sizeof(e.new_flag_name),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                                          flag_name_filter);
    ImGui::SameLine();
    if ((ImGui::Button("Add flag") || enter) && e.new_flag_name[0]) {
        int f = flag_create(s, e.new_flag_name);
        if (f) { e.flag_brush = (uint8_t)f; e.new_flag_name[0] = 0; set_status(e, "new flag %s", flag_label(s.world, f)); }
        else set_status(e, "can't add '%s' (name taken or no free flags)", e.new_flag_name);
    }
    ImGui::SameLine();
    ImGui::Checkbox("show unused numbers", &e.flags_show_all);

    // Table.
    if (ImGui::BeginTable("flags", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Set", ImGuiTableColumnFlags_WidthFixed, 32);
        ImGui::TableSetupColumn("Used in", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 48);
        ImGui::TableHeadersRow();
        char usage[160];
        for (int f = 1; f <= kMaxFlags; ++f) {
            bool used = flag_usage(s, f, usage, sizeof(usage));
            if (!s.world.flag_names[f][0] && !used && !e.flags_show_all) continue;
            ImGui::PushID(f);
            ImGui::TableNextRow();
            if (f == e.flag_brush)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(60, 110, 70, 120));
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", f);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##name", "(unnamed)", s.world.flag_names[f], sizeof(s.world.flag_names[f]),
                                         ImGuiInputTextFlags_CallbackCharFilter, flag_name_filter))
                e.dirty = true;
            ImGui::TableSetColumnIndex(2);
            bool on = flag_is_set(s.play, f);
            if (ImGui::Checkbox("##set", &on)) { if (on) flag_set(s, f); else flag_clear(s, f); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set / clear in your progress (for testing)");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", used ? usage : "-");
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton(f == e.flag_brush ? "brush" : "use")) e.flag_brush = (uint8_t)f;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace hop
