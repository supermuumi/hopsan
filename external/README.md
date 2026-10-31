Third-party single-header libraries vendored into the source tree.

| File              | Version  | License             | Source |
|-------------------|----------|---------------------|--------|
| cgltf.h           | 1.15     | MIT                 | https://github.com/jkuhlmann/cgltf |
| stb_image.h       | 2.30     | public domain / MIT | https://github.com/nothings/stb |
| HandmadeMath.h    | 2.0.0    | public domain / MIT | https://github.com/HandmadeMath/HandmadeMath |
| imgui/            | 1.92.9 WIP (docking) | MIT       | https://github.com/ocornut/imgui (see imgui/README.md) |

Each library is consumed by including the header from exactly one .cpp
(its implementation TU) with the required `#define ..._IMPLEMENTATION`
macro, and including it without the define elsewhere. Implementation
TUs live in `src/engine/asset_cache.cpp` (cgltf + stb_image) and the
host's `main.cpp` (HandmadeMath is header-only and needs no impl flag).

To update: download the new header, replace the file, bump the version
in this table. Do not patch the files; if local config is needed do it
via the library's `#define` knobs in our impl TU.
