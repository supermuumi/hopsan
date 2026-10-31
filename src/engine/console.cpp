#include "engine/console.h"
#include "engine/log.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace engine {

namespace {

struct Command {
    std::string      name;
    std::string      help;
    ConsoleCommandFn fn   = nullptr;
    void*            user = nullptr;
};

constexpr int kMaxHistory  = 64;
constexpr int kMaxScrollLines = 512;

} // namespace

struct Console {
    bool                     open = false;
    bool                     scroll_to_bottom = false;
    bool                     focus_input_next = false;
    char                     input[256] = {};
    std::vector<Command>     commands;
    std::vector<std::string> scrollback;
    std::vector<std::string> history;
    int                      history_pos = -1; // -1 == not browsing
};

Console* console_create() { return new Console(); }
void     console_destroy(Console* c) { delete c; }

void console_toggle(Console* c) {
    c->open = !c->open;
    if (c->open) c->focus_input_next = true;
}
bool console_is_open(const Console* c) { return c->open; }

void console_register(Console* c, const char* name, ConsoleCommandFn fn,
                      void* user, const char* help) {
    for (auto& cmd : c->commands) {
        if (cmd.name == name) {
            cmd.fn = fn; cmd.user = user;
            if (help) cmd.help = help;
            return;
        }
    }
    Command cmd;
    cmd.name = name;
    cmd.help = help ? help : "";
    cmd.fn   = fn;
    cmd.user = user;
    c->commands.push_back(std::move(cmd));
}

void console_clear_owner(Console* c, void* user) {
    c->commands.erase(
        std::remove_if(c->commands.begin(), c->commands.end(),
                       [user](const Command& cmd){ return cmd.user == user; }),
        c->commands.end());
}

void console_print(Console* c, const char* msg) {
    if (!msg) return;
    if ((int)c->scrollback.size() >= kMaxScrollLines) {
        c->scrollback.erase(c->scrollback.begin(),
                            c->scrollback.begin() + (kMaxScrollLines / 4));
    }
    c->scrollback.emplace_back(msg);
    c->scroll_to_bottom = true;
}

void console_help(Console* c) {
    console_print(c, "Commands:");
    for (const auto& cmd : c->commands) {
        char line[512];
        std::snprintf(line, sizeof(line), "  %-20s %s",
                      cmd.name.c_str(), cmd.help.c_str());
        console_print(c, line);
    }
}

namespace {

// Splits on whitespace into argv-style pointers into a mutable buffer.
int tokenize(char* buf, const char** argv, int max_argv) {
    int argc = 0;
    char* p = buf;
    while (*p && argc < max_argv) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) { *p = 0; ++p; }
    }
    return argc;
}

void execute(Console* c, const char* line) {
    while (*line == ' ') ++line;
    if (!*line) return;

    {
        char shown[270];
        std::snprintf(shown, sizeof(shown), "> %s", line);
        console_print(c, shown);
    }

    // History (deduped against the last entry).
    if (c->history.empty() || c->history.back() != line) {
        c->history.emplace_back(line);
        if ((int)c->history.size() > kMaxHistory)
            c->history.erase(c->history.begin());
    }
    c->history_pos = -1;

    char buf[256];
    std::strncpy(buf, line, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;

    const char* argv[16];
    int argc = tokenize(buf, argv, 16);
    if (argc == 0) return;

    if (std::strcmp(argv[0], "help") == 0) { console_help(c); return; }
    if (std::strcmp(argv[0], "clear") == 0) { c->scrollback.clear(); return; }

    for (const auto& cmd : c->commands) {
        if (cmd.name == argv[0]) {
            cmd.fn(argc, argv, cmd.user);
            return;
        }
    }

    char msg[280];
    std::snprintf(msg, sizeof(msg), "unknown command: %s", argv[0]);
    console_print(c, msg);
}

int input_text_callback(ImGuiInputTextCallbackData* data) {
    auto* c = static_cast<Console*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (c->history.empty()) return 0;
        const int prev = c->history_pos;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (c->history_pos == -1) c->history_pos = (int)c->history.size() - 1;
            else if (c->history_pos > 0) --c->history_pos;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (c->history_pos != -1) {
                ++c->history_pos;
                if (c->history_pos >= (int)c->history.size()) c->history_pos = -1;
            }
        }
        if (prev != c->history_pos) {
            const char* s = (c->history_pos == -1) ? "" : c->history[c->history_pos].c_str();
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, s);
        }
    }
    return 0;
}

} // namespace

void console_draw(Console* c) {
    if (!c->open) return;

    // Pin the dev console to the main viewport (issue #69). Without this,
    // multi-viewport (#20) lets the user drag it out into a separate OS
    // window, which doesn't make sense for a debug overlay anchored to
    // the engine window. Position + size must come from the main viewport
    // too -- with viewports enabled, io.DisplaySize is the *virtual*
    // desktop spanning all monitors and (0, 0) is its top-left corner
    // (possibly off-screen relative to the engine window).
    ImGuiViewport* mvp = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(mvp->ID);
    ImGui::SetNextWindowPos(mvp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(mvp->Size.x, mvp->Size.y * 0.45f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##dev_console", &c->open, flags)) { ImGui::End(); return; }

    const float footer_h = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##scroll", ImVec2(0, -footer_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& s : c->scrollback) {
        ImGui::TextUnformatted(s.c_str());
    }
    if (c->scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        c->scroll_to_bottom = false;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGuiInputTextFlags itf = ImGuiInputTextFlags_EnterReturnsTrue
                            | ImGuiInputTextFlags_CallbackHistory
                            | ImGuiInputTextFlags_EscapeClearsAll;
    if (c->focus_input_next) {
        ImGui::SetKeyboardFocusHere();
        c->focus_input_next = false;
    }
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##input", c->input, sizeof(c->input), itf,
                         input_text_callback, c)) {
        execute(c, c->input);
        c->input[0] = 0;
        c->focus_input_next = true;
    }
    ImGui::PopItemWidth();

    ImGui::End();
}

} // namespace engine
