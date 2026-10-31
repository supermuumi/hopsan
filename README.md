# hopsan

A 2D precision platformer in the spirit of Celeste / Hollow Knight, built on
a small SDL3 engine skeleton extracted from
[ikimaa](https://github.com/supermuumi/ikimaa). The world is a set of
linked rooms you can traverse freely, and there's a built-in level editor
(press **Tab**).

## Quick start

**macOS** (Apple Silicon):
```sh
brew install sdl3 shaderc
./build.sh
./build/darwin/engine
```

**Linux**:
```sh
sudo apt install libsdl3-dev glslc   # or your distro's equivalent
./build.sh
./build/linux/engine
```

**Windows**: open an *x64 Native Tools Command Prompt for VS 2022*, then:
```cmd
build.bat
build\win\engine.exe
```
SDL3 headers + import lib are expected under `ext/sdl3` (or set
`SDL3_DIR` to wherever you put SDL3-devel-VC).

## Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move | WASD / arrows | left stick / d-pad |
| Jump | Space / C / K | A (south) / Y |
| Dash (8 directions) | X / J / Left Shift | X / B |
| Grab / climb | Z / L / Right Shift | LB / RB / triggers |
| Drop through platform | Down + Jump | |
| Level editor | Tab | |
| Dev console | F1 | |

Movement: variable-height jumps, coyote time, jump buffering, wall slide,
wall jump, an air dash that refills on landing, and wall climbing with
stamina (you flash white when it's about to run out).

Wall jump has two styles, switched with the `wall_jump` console command (F1):
- `wall_jump celeste` (default): kicks you away from the wall. If you're
  holding a direction your input is locked away from the wall for a moment;
  a neutral wall jump keeps full control. Climbing one wall needs grab.
- `wall_jump hk`: Hollow Knight-style. A short kick (about one tile) with
  full air control, so holding toward the wall and tapping jump climbs it.
  Slower wall slide.

## Stars

Stars (`$` in the editor palette, key 9) are collectibles, counted once per
save. A star you pick up only counts once you **leave the room or touch a
checkpoint**; die before that and it goes back (like Celeste's
strawberries). The HUD star count is yellow while a pickup is uncommitted.
Collected stars show as faint ghosts. Progress is saved to `progress.txt`
next to the engine binary; `progress_reset` in the console forgets it.

## The world

- The world is made of **rooms**: rectangles of 16px tiles placed on one
  shared grid. The camera stays inside the current room and pans to the
  next room when you walk, jump or fall out through an edge into it.
  Room edges with no neighbouring room act as walls. Falling out of the
  bottom kills you.
- **Death**: by default you respawn where you entered the room. If you
  entered mid-air, that point moves to the first safe ground you land on.
  Touching a checkpoint flag overrides it until you leave the room.
  Crumble blocks, crystals and enemies reset. Run `respawn_mode checkpoint`
  in the console (F1) for Hollow Knight-style respawning at the last
  checkpoint only, and `respawn_mode room` to switch back.
- The world lives in `assets/levels/world.txt`, a readable text format with
  one block of ASCII rows per room, so it diffs nicely in git.

Tiles: rock, brick, backdrop (decorative), one-way platform, spikes (lethal
when you move into them), crumble block, spring, dash crystal, checkpoint,
walker enemy, the start marker, and decorations (grass, fern, mushrooms,
rubble, vines, chain, torch, pillar). Decorations aren't solid; in the
editor reach them with Q / E (keys 1-0 cover the first ten brushes).

## Art

All art lives in one atlas, `assets/art/atlas.png`, built from the source
images in `assets/art/src/`:

| Source | Used for |
| --- | --- |
| `rock_fill.webp` | seamless rock texture: rock + brick (tinted) tiles |
| `back_wall.webp` | seamless backdrop wall texture |
| `moss_top.png` | seamless moss strip along exposed rock tops |
| `props_sheet.webp` | spikes, spring, crystal, flag, plank, crumble stone, beetle enemy, decorations |

After changing any source, rebuild with:
```sh
pip install pillow numpy
python3 tools/build_atlas.py
```
It snaps each image to a clean pixel grid, removes the magenta background,
reduces colours, packs the atlas and regenerates `src/game/atlas_data.h`.
Both outputs are committed. Prop crop rectangles are listed in `PROPS` at
the top of the script.

Art scale: a tile is 32x32 art pixels (16 world units, so 2 art px per world
unit). Terrain edges are built at runtime from the textures: an outline +
shading on exposed sides and moss on exposed tops, so any shape of terrain
works without hand-made edge tiles. If the atlas is missing the game falls
back to the built-in placeholder art. The player is still placeholder art.

## Level editor (Tab)

| | |
| --- | --- |
| Pan | WASD / arrows (Shift = faster), middle-mouse drag |
| Zoom | mouse wheel (zooms around the cursor) |
| Paint / erase | left / right mouse |
| Fill / erase rectangle | Shift + drag |
| Pick tile under cursor | Alt + click |
| Choose brush | 1-9, 0, Q / E, or click the palette |
| Spike direction | T (auto-orients against walls by default) |
| Room mode | R. Drag on empty space to create a room, drag inside a room to move it (tiles come with it), drag an edge or corner to resize, Del / X to delete |
| New one-screen room at cursor | N |
| Frame room / whole world | F |
| Play from the cursor | P |
| Undo / redo | Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) |
| Save | Ctrl+S |
| Grid / help | G / H |

On macOS, Cmd works in place of Ctrl. You can only paint inside rooms,
because tiles outside rooms aren't saved. Console commands: `save`,
`load`, `restart`, `edit`, `respawn_mode`, `wall_jump`, `progress_reset`,
and `room_name <name>` (renames
the room under the cursor).

## Engine notes

- Hot-reloadable game DLL: run `./build.sh game` while the game is running
  and it reloads in place. Game state survives as long as the `GameState`
  layout is unchanged (`kStateVersion` in `src/game/game.h`).
- The game renders 2D by emitting screen-space `Quad2D` rects (solid or
  textured from an atlas) plus `HudText` through `RenderCommands` (see
  `src/shared/engine_api.h`). Textured quads use `sprite.frag`, so run
  `./build.sh shaders` (or `./build.sh`) once after pulling.
- The view is 640x360 world units = 1280x720 art pixels, scaled and
  letterboxed. An even scale (art pixels exactly square, e.g. 1280x720 or
  2560x1440 windows) is preferred; otherwise the nearest integer scale is
  used (e.g. 1080p: art pixels at 1.5x).
