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

## Premise

You're the PA of an evil mastermind and you've accidentally shredded the
plans for his giga laser. He's popped out for a round of golf. Find every
piece of the plans in his lair before he gets back.

## Plan pieces

Plan pieces (`$` in the editor palette, key 9; called stars in the code)
are the collectibles, counted once per save. A piece you pick up only counts once you **leave the room or touch a
checkpoint**; die before that and it goes back (like Celeste's
strawberries). The HUD count is yellow while a pickup is uncommitted. Collected pieces
show as faint ghosts. Progress is saved to `progress.txt`
next to the engine binary; `progress_reset` in the console forgets it.

## The golf clock

The boss is playing 18 holes. Every plan piece you commit moves his round
on; the last piece is always the 18th hole, when he walks back in. The
HUD shows the current hole (top right). A new hole takes effect when you
next enter a room or respawn, never mid-room. It's announced with a
full-screen card that pauses the game: the course at dusk, "HOLE N"
slamming in, and the boss's line typing out. Press jump (or Enter / Esc)
once to finish the typing and again to continue; it also moves on by
itself after 8 seconds. The final piece gets its own "HE'S BACK!" card.

As the round goes on the lair gets more hectic: pulsing lasers cycle up to
35% faster, drones and sharks speed up, and from hole 13 the alarm lights
come on. Level designers can also tag any tile in the editor:

- `[` / `]` sets the brush's "appears from hole N" (green corner marker).
- `Shift+[` / `Shift+]` sets "gone from hole N" (red corner marker).
- `,` / `.` previews the lair at a given hole (0 = show everything).
- Alt+click picks up a tile's tags along with its type.

Use tags for henchmen arriving, lasers switching on, doors sealing and
bridges collapsing. Every room must stay reachable at every hole. In the
starter world a henchman turns up in Your Desk from hole 4, the Boiler
Room gains a laser at 5, Reception a second drone at 7, the Server Farm a
laser at 10, and from hole 13 the door from Reception to the Server Farm
seals (go round through the Shark Tank, which now has a way back up).

Console: `hole <n>` jumps the clock for testing.

## Switches, flags and dark rooms

The world has 31 **flags**, each with a name stored in the world file
(`flag 3 lab_barricade_burnt`). A **switch** (lever) sets its flag when
you touch it, and a burning **fuse** sets its flag when it burns out. Flags
are permanent and saved with your progress (`progress_reset` clears them).

Any tile can be tagged to be present only while a flag is set, or only
while it's *not* set. Use that for doors, walls, lasers, cables and so on.
When a flag changes something in another room, the camera pans over to
show it, then comes back.

A room can be **dark**, always or until a flag is set. You see a pool of
light around you; lava, fire, cables, switches, lasers and enemies still
show.

**Flags window (F4)**, in the editor or while playing: pick the brush flag
(and NOT), add a named flag, rename flags, tick a flag on / off for
testing, and see which rooms use each flag ("(sets)" marks the room with
its switch or fuse). Names are single words, saved with the level.

Console: `flag_new <name>` takes the first unused flag, names it and
selects it as the brush flag; `flag_name <n|name> <new name>` renames one;
`flags` lists every flag with whether it's set and which rooms use or set
it; `flag <n|name> [on|off]` inspects or forces one for testing. Names are
saved with the level (Ctrl+S).

In the editor: `-` / `=` pick the brush's flag (0 = none), with a panel
listing the named flags, `\` toggles
"present while NOT set". For switches and fuses the brush flag is the flag
they set. Blue corner markers show flag conditions, yellow ones show
switch / fuse targets. In room mode, `L` cycles the hovered room between
lit, always dark, and dark until the brush flag.

## Reactions

A small simulation runs over the current room at 10 Hz, the same way every
time, and resets when you die or leave:

- **Lava** (`&`) falls, then runs in its flow direction, pours over edges,
  and (if it was poured from a cauldron) cools to **basalt** (`o`, solid)
  after resting for 3 s. Lava placed in the editor stays lava. Touching
  water or ice turns it to basalt straight away. It ignites anything
  flammable. Lethal.
- **Water** (`w`) flows too: it falls, spreads sideways when there's water
  piled above it, pours over edges, and levels out across the surface. A
  full, closed pool stays perfectly still until something breaks it, e.g.
  burn a wooden wall holding it back and it floods out. Moving water puts
  out fire, quenches lava and carries electricity from live cables.
- **Ice** (`I`): solid, slippery (low friction) and too slippery to climb.
  Lava or fire touching it starts a thaw that spreads through the whole
  connected block, and every ice tile becomes water, which then flows.
- **Fire** burns through wood crates (`W`), wooden platforms, fuses, cables
  and plants, then leaves a gap. Water puts it out. Lethal.
- **Cauldron** (`U`): push into it for a moment, or dash into it, to tip it
  over. It pours 30 cells of lava the way you pushed.
- **Live cable** (`z`): lethal to touch, and it electrifies any body of water
  it touches (sparks show it). It burns.
- **Fuse** (`i`): a wick that burns fast and sets its flag when it's gone.

Plan pieces are never destroyed by reactions.

### Making a result permanent

Rooms reset when you leave, so a reaction's result is gone when you come
back. To keep it, use flags:

- Tag the tiles that should be destroyed **"present while flag N is NOT
  set"** (brush flag N + `\`). When the last tile of that group in the room
  burns or melts away, flag N is set automatically, so the group stays
  gone for good.
- Tag anything the solution should leave behind **"present while flag N
  is set"**. It appears once the puzzle is solved and stays.

The Lab does this: its barricade is tagged NOT flag 3, and a basalt bridge
over the pit is tagged flag 3. Once you've burnt the barricade, you can
leave and come back and the way through stays open. Design rule: if a
reaction opens the only way back from somewhere, make it permanent like
this, or you can trap the player.

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
walker enemy, the start marker, plan pieces, decorations (grass, fern,
mushrooms, rubble, vines, chain, torch, pillar), and lair hazards:

- **Lasers** (`L` always on, `j` / `J` pulsing in alternating phases A / B:
  1.4s on, a flicker warning, then off). An emitter aims away from the wall
  it's mounted on, and the beam stops at the first solid tile.
- **Water** (`w`): swim in 8 directions, you slowly float up, jump to leave
  at the surface. Dash refills underwater.
- **Sharks** (`K`, placed in water): patrol, and drift toward you when
  you're swimming nearby.
- **Conveyor belts** (`{` left, `}` right): solid, carry you at 60 px/s while
  standing. Jumping or walking off keeps the belt's momentum, which fades
  slowly in the air.
- **Security drones** (`D` horizontal, `d` vertical): patrol until they hit a
  wall, spikes or the room edge. They fly through one-way platforms.

The starter world: the four grounds rooms plus the lair (Your Desk,
Reception, The Shark Tank, Boiler Room, Server Farm, The Giga Laser, The
Lab, The Walk-in Freezer). **The Lab** (left of the Shark Tank) is a chain-reaction puzzle: tip
the cauldron, and the lava builds a basalt bridge over the electrified pit,
burns the fuse and the barricade, and opens the way to the plan piece. The
fuse turns on the lights in the Boiler Room, which is dark until then. The
Boiler Room's main breaker (top right) switches off the Server Farm's rack
lasers. Behind the Lab's alcove is **The Walk-in Freezer**: tip its cauldron
onto the ice wall, the thaw spreads through it, and the meltwater floods
the corridor and turns the lava strip in front of the plan piece to stone. Decorations aren't solid; in the
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
back to the built-in placeholder art. The player (the PA: glasses, bun, navy
blazer, red tie) is drawn in code; the papers on their clipboard show
whether the dash is ready.

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
| Golf-hole tags on the brush | `[` `]` (from), Shift+`[` `]` (until) |
| Preview the lair at a hole | `,` `.` |
| Brush flag / NOT | `-` `=` / `\` |
| Flags window | `F4` |
| Room dark / dark until flag (room mode) | `L` |

On macOS, Cmd works in place of Ctrl. You can only paint inside rooms,
because tiles outside rooms aren't saved. Console commands: `save`,
`load`, `restart`, `edit`, `respawn_mode`, `wall_jump`, `progress_reset`,
`hole`, and `room_name <name>` (renames
the room under the cursor).

## Engine notes

- The game can draw ImGui tool windows: the host passes its ImGui context
  through `EnginePlatform`, the game library links its own copy of the
  ImGui core (`src/build/imgui_core_unity.cpp`, built once and cached),
  and the host calls the optional `game_imgui` export each frame.
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
