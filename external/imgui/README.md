ImGui vendored copy
===================

Source:  https://github.com/ocornut/imgui (docking branch)
Version: 1.92.9 WIP
Commit:  5a76f2adf1b0403b86a45010121fb32a6bff8680 (2026-05-15)
License: MIT (see LICENSE.txt)

Files vendored:
  imgui.h imgui.cpp imgui_internal.h
  imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp
  imstb_rectpack.h imstb_textedit.h imstb_truetype.h
  imconfig.h
  backends/imgui_impl_sdl3.{h,cpp}
  backends/imgui_impl_sdlgpu3.{h,cpp} backends/imgui_impl_sdlgpu3_shaders.h

To update: re-clone the docking branch, replace these files, update the
commit hash and version above. We do not patch imgui itself; if we need
build-time configuration, set it in imconfig.h.
