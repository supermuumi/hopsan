// Unity TU bundling all of ImGui's implementation (#242). Compiled once
// into `imgui.{o,obj}`; linked alongside main.obj, worldgen.obj, and
// editor.obj. The whole point of pulling these out of main.cpp is that
// MSVC's frontend is single-threaded and re-parsing 80K lines of ImGui
// every time main.cpp changes was costing 30+ seconds on a clean build.
// ImGui itself supports the unity-include pattern -- it's how the
// upstream backends are usually integrated.

#include "external/imgui/imgui.cpp"
#include "external/imgui/imgui_draw.cpp"
#include "external/imgui/imgui_tables.cpp"
#include "external/imgui/imgui_widgets.cpp"
#include "external/imgui/imgui_demo.cpp"
#include "external/imgui/backends/imgui_impl_sdl3.cpp"
#include "external/imgui/backends/imgui_impl_sdlgpu3.cpp"
