// ImGui core (no demo, no backends) for the game library. The game draws
// its tool windows (e.g. the editor's Flags window) into the *host's* ImGui
// context, which it receives through EnginePlatform; this copy of the core
// just has to be the same version as the host's, which it is (same files).
// Built once into game_imgui.{o,obj} so hot reloads stay fast.
#include "external/imgui/imgui.cpp"
#include "external/imgui/imgui_draw.cpp"
#include "external/imgui/imgui_tables.cpp"
#include "external/imgui/imgui_widgets.cpp"
