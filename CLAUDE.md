# engine skeleton — agent notes

## What this is

**hopsan**: a 2D precision platformer (Celeste-like) on a cross-platform
engine skeleton extracted from
[ikimaa](https://github.com/supermuumi/ikimaa). The host owns the window,
input (keyboard, mouse, gamepad), dev console, asset cache, renderer, and a
hot-reloadable game DLL. The game DLL is `game.cpp`, which is a unity build
of `src/game/*`.

## Game design (decided so far)

- Premise: you're the PA of an evil mastermind and accidentally shredded
  his giga-laser plans. He's out for a round of golf; reassemble the plans
  (collectible "plan pieces", tile `$` / `T_STAR`) in his lair before he's
  back. Absurd, comedic tone. The mossy ruins are "the grounds".
- Golf-hole clock (`src/game/clock.cpp`): hole = 1 + committed pieces *
  17 / total, so the last piece is always hole 18 = he walks in (fixed
  finale; placeholder "THE END" overlay for now). A hole change applies on
  the next room entry / respawn, never mid-room, announced by a
  full-screen card (`start_card` / `card_tick` / `draw_card`) that pauses
  the sim; the finale card queues after the 18th-hole card.
  Tiles can be tagged "from hole N" / "until hole N" (per-chunk `from` /
  `until` arrays; `tile_get` hides tiles not present at `World::gate_hole`,
  `tile_raw` ignores tags; save file stores `hole x y from until` lines per
  room). Escalation is automatic too: pulse lasers up to 35% faster,
  drones / sharks faster, red alarm lighting from hole 13.
  Level-design rule: any tagged change must keep every room reachable at
  every hole (e.g. the sealed reception door is backed by the tank exit).
- Flags + switches + reactions (`src/game/react.cpp`): 31 world flags
  (`Play::flags`, saved in progress.txt). Per-tile `cond` (chunk array):
  1..31 = present while flag set, |0x80 = while NOT set; for switches /
  fuses it's the flag they set. Flags are named (`World::flag_names`,
  `flag N name` lines in world.txt; `flag_label` / `find_flag`); always
  refer to flags by name in UI / logs, and name every flag you add. Flag changes rebuild the current room's
  entities and "peek" the camera at the most-affected other room. Rooms
  can be dark (`Room::dark`: 255 always, N until flag N). Reactions are a
  10 Hz deterministic cellular sim over the current room (lava + water
  flow via `flow_pass`; poured lava cools, placed lava doesn't; quench to
  basalt; ice thaw chain -> water; fire spread; electrified water flood
  fill; cauldron emitters). Resting water never moves, so closed pools are
  stable. Ice is solid, slippery and unclimbable. The room's tiles are snapshotted on build and
  restored on death / leaving / entering the editor / saving.
  Destroying the last tile of a group tagged "NOT flag N" (burnt / thawed)
  sets flag N (`group_destroyed`), so solved rooms stay solved after the
  reset; tag the result tiles "flag N" (e.g. the Lab's bridge). The sim
  sees tiles hidden by tags as empty (`Cells::raw` uses `tile_get`).
  Level-design rules: plan pieces must never be destroyable; every room
  must stay reachable for every reachable combination of holes + flags;
  any reaction that opens the only way back must be made permanent this
  way (otherwise leaving + returning can trap the player).
- Graded endings (not implemented yet) come from a second, optional
  collectible (the boss's silly laser add-ons), never from missing
  required pieces.

- Tiles are 16 world units, view is 640x360 world units. Art is 32px per
  tile (`kArtScale` = 2 art px per world unit), so the view is 1280x720 art px.
- World = rooms (tile rects) placed on one shared sparse tile grid. Camera
  is clamped to the current room; leaving through an edge into another
  room triggers a short camera pan. Edges with no neighbour room are walls;
  falling out of the bottom kills you.
- Respawn: room entry point (moved to the first safe landing if you entered
  mid-air), overridden by touching a checkpoint. `respawn_mode checkpoint`
  switches to last-checkpoint-only. Room entities reset on death / entry.
- Moveset: run, variable jump, coyote + buffer, wall slide / jump, 8-way
  dash (1 charge, refills on ground / crystal / spring), climb with stamina.
  No attacks yet (deliberately).
- Player look: the PA, drawn procedurally in `draw_player` (bob + bun,
  glasses, navy blazer, white shirt, red tie, clipboard). Dash readiness is
  shown by papers on the clipboard (not hair colour; avoid Celeste look-alikes).
- Plan pieces (code: stars): collectibles, counted once per save. Pickups in a room commit
  on leaving the room or touching a checkpoint; death rolls them back.
  (Helper double jump / charged super jump were tried and shelved.)
- Level editor is in-game (Tab). Save file: `assets/levels/world.txt`.

## Target platforms

Primary platforms are **Windows** and **macOS** (Apple Silicon). Linux is
a "should just work" target but is not a priority. Compile-check only if
it's free.

## Layout

- `main.cpp` — host: window, input, frame loop, ImGui, hot reload, draw.
- `game.cpp` — game DLL. Exports `game_init`, `game_tick`, `game_render`;
  `#include`s the modules below (unity build, one TU).
- `src/game/game.h` — all game types; `GameState` is the whole arena.
  POD only, no pointers, so it survives hot reload. Bump `kStateVersion`
  when changing its layout.
- `src/game/world.cpp` — chunked tile grid, rooms, save / load.
- `src/game/progress.cpp` — stars, commit / rollback,
  `progress.txt` save file.
- `src/game/draw.cpp` — view transform, `Quad2D` / text output, atlas
  sprite helpers, tile art (atlas art with procedural fallback).
- `src/game/atlas_data.h` — GENERATED by `tools/build_atlas.py`; never edit.
- `assets/art/src/` — source art (AI-generated, magenta = transparent).
  `tools/build_atlas.py` turns it into `assets/art/atlas.png` + the header.
- `src/game/clock.cpp` — golf-hole clock: hole formula, boss messages,
  `apply_hole`, hecticness, HUD + finale + alarm overlay.
- `src/game/hazards.cpp` — lasers (constant / pulse A / pulse B,
  auto-aimed away from their mounting wall), water + swimming, sharks,
  conveyors, drones.
- `src/game/react.cpp` — flags, switches, camera peek, dark rooms, and the
  reaction sim (lava, fire, basalt, cauldrons, cables, fuses).
- `src/game/play.cpp` — fixed-60Hz sim: player physics + tuning constants,
  entities, hazards, room transitions, respawn, camera, play renderer.
- `src/game/editor.cpp` — level editor (tile + room modes, undo / redo).
- `assets/levels/world.txt` — the world (one ASCII block per room).
- `src/shared/engine_api.h` — host ↔ DLL ABI. POD only.
- `src/engine/` — engine modules compiled into the host:
  - `log` / `console` — dev console + file log.
  - `game_loader` — DLL hot reload (poll mtime, dlopen / LoadLibrary).
  - `asset_cache` — texture + mesh loader keyed by path.
  - `renderer` — SDL_GPU tile / cube / mesh / shadow passes.
  - `ui_renderer` — native screen-space rects, textured quads (batched per
    texture, `sprite.frag`) + text via stb_truetype.
  - `hud` — project HudBar world coords to screen + queue panels / text.
- `external/imgui` — vendored ImGui (with SDL3 + SDL_GPU backends).
- `ext/sdl3` — Windows SDL3 import lib (macOS / Linux use system / brew).
- `assets/shaders/` — GLSL + Metal sources; compiled into `compiled/`.
- `assets/fonts/` — ProggyClean (HUD font 0), Nova Round (font 1).

## Building

- macOS / Linux: `./build.sh` (needs `brew install sdl3 shaderc` on Mac).
- Windows: `build.bat` from an x64 Native Tools prompt for VS 2022.
- Targets: `host`, `game`, `shaders`, `all`, `clean`.

The engine binary lives in `build/<platform>/`; the build script
symlinks `assets/` into that directory so cwd-relative asset paths work.

## DLL hot reload

`./build.sh game` while the engine is running re-links the game DLL;
the host notices the mtime bump within ~250 ms and reloads. Game state
in the `kGameArenaBytes` arena survives the reload, so you can iterate
on game logic without losing your place.

## Branch naming

Use plain slug branch names tied to the task — e.g. `add-audio`,
`tilemap-editor`, `physics-2d`. No random alphanumeric suffixes.
