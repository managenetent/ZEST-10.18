# muchi-verse grand architecture — RPG-Maker-style events, the shared
# editor, and the ASCII/GL "mirror"

Status: PLANNING DOCUMENT, grounded in direct research against the real
1.TPMOS source (`/home/no/Downloads/1.TPMOS_c_+rmmp.0100.0110/`) - every
claim below cites an actual file. Written per direct request, before
touching `muchi-civ-pal/` or `muchipal-editor/` (both currently empty)
or mutaclsym. `2.muchi-verse/wussup-vs?.txt` is the user's own raw
brainstorm this formalizes a *slice* of - it covers far more (chat
apps, a DAW, blockchain micro-services, a Godot-like engine) that this
document does NOT attempt to architect. Read that file for the fuller
vision; read this one for the part that's actionable next.


## GOVERNING CONSTRAINT — read this before anything else in this doc

`prisc+x` is being built toward a long-term goal of compiling this
whole ecosystem's code directly to RISC-V (it's already shaped like "a
RISC-V++ compiler," per direct instruction) - so **we do not write
direct GL/OpenGL primitive calls ourselves, anywhere, ever, not even
for our own chrome/widgets.** Portable CPU C that computes pixel values
is something that vision can eventually target; calls into a live
OpenGL driver context are not - they're a hard dependency on real GPU
API surface with no RISC-V-compilable equivalent.

This is WHY §0 below recommends porting the `piececraft-wraith`/
`wraith_rgb_daemon.c` pattern and explicitly NOT `gl_desktop.c`'s own
immediate-mode chrome, even though both are real code in the same
1.TPMOS tree: `wraith_gl.c` itself only ever does ONE thing with real
GL calls - read a raw RGBA framebuffer file and blit it as a single
textured quad (`wraith_gl.c:567-588`). That one tiny, already-written,
already-proven "RGB reader" is ALL the GL surface this ecosystem
needs, full stop - not "simpler GL calls," literally none of our own.
Every project's actual job is only ever "compute correct RGB pixels in
portable C and write them to a file" (exactly what
`wraith_rgb_daemon.c`'s software voxel ray-marcher already does - see
§0). Reuse or port that minimal reader unmodified (or close to it);
never write new `glTranslatef`/`glRotatef`/`gluPerspective`-style code
of our own, even for menus/UI chrome, regardless of what
`gl_desktop.c` itself does elsewhere in 1.TPMOS. GLUT/freeglut is only
ever present as that one reader's own dependency, not something
project code calls into directly. Zero shader files anywhere either
way (no `.glsl`/`.vert`/`.frag`, no `glCreateShader`) - hand-rolled
portable C throughout, no engine, no new dependency class beyond that
single existing reader binary.

**Status as of this writing: no GL work has actually started anywhere
in muchi-verse** - only this planning document and the (pure-ASCII,
zero-GL) `muchipal-editor` scaffolding exist so far.


## 0. The core discovery: "mirror" already means something specific

Before assuming "2D/3D mirror, switchable ASCII/GL" needed inventing,
I had it researched against the real source. It didn't need inventing
- it's an existing, named convention:

> `!.gem-flashlite--yolo/#.docs/.../README.md:72` —
> **"Mirror:** The state.txt runtime representation."

The documented 12-step pipeline (same README, lines 186-194) has every
renderer, ASCII or GL, downstream of the SAME mirror: step 7 syncs
state to `state.txt`, step 8 has `render_map.+x` read that mirror and
write `view.txt`, step 12 has "Renderer (ASCII or GL)" print it. So the
real convention is: **one source of truth (a plain-text state file),
multiple renderers reading it, each free to interpret it differently.**
This is not a new architecture to design - it's the SAME thing
mutaclsym already does (`compose_frame.c` reads `hero/state.txt` +
`map.txt`/`furniture.txt` and writes `current_frame.txt`; a GL renderer
would just be a second thing reading those same files and writing
pixels instead of text).

Two real, DIFFERENT-fidelity implementations of this exist in 1.TPMOS -
know both, they solve different problems:

1. **Semantic re-render (`gltpm_app` / `.gltpm` layouts /
   `gltpm_parser.c`)** - the smart one. `gltpm_load_scene()`
   (`pieces/apps/gl_os/plugins/gltpm_parser.c:476-613`) reads a
   project's `session/state.txt`, `manager/gui_state.txt`, each piece's
   `.pdl`, and critically the same `view.txt` the ASCII pipeline
   writes (line 588). A `.gltpm` layout embeds `${game_map}` with the
   comment "will be converted to 3D tiles using assets/tiles/
   registry.txt". **Superseded below - this path is unfinished/dead
   (its `unicode=` field is parsed and never used, per §0a), and the
   user redirected research away from it toward a better, actually-
   working precedent.**
2. **Literal texture rasterization (`wraith_rgb_daemon` +
   `wraith_gl`)** - reads semantic frame data and rasterizes it into a
   `.rgba32` texture `wraith_gl.c` displays via OpenGL. Still relevant
   as the SHARED PRESENTER layer - see below, this is the same daemon
   the real working precedent uses, just aimed at a different kind of
   scene data than I first assumed.

**The real, working, correctly-scoped precedent: `piececraft-wraith`**
(`projects/wraith-alpha/wraith-projects/piececraft-wraith/`), found
after the user specifically redirected research here instead of
`gltpm`. Everything below corrects/replaces the earlier gltpm-based
guess - this is proven, running code, not a dead field:

- **What it is**: a small (20x10, one file per z-level), explicitly
  scoped-down "Wraith validation project" - a cursor walks an ASCII
  tile grid, toggles 2D top-down / 3D perspective preview with key `8`.
  No building/mining (that's its sibling `piececraft-3d`, the
  original non-wraith prototype it deliberately reuses the map/tile
  file FORMAT from - `session/state.txt`'s own
  `reference_project=projects/piececraft-3d` says so directly).
- **One text file IS the mirror for both renderers, proven, not
  aspirational.** `maps/map_01_z0.txt` (plain ASCII glyphs, keyed by
  `assets/tiles/registry.txt`) is read directly by BOTH paths: the
  ASCII path (`ops/src/+x/render_map_wraith.c`, prints it into
  `session/state.txt`'s `game_map=` field) and the GL path (see next
  point) via `fopen(obj->source_ref, "r")` - the exact same file, no
  conversion step, no duplicate data.
- **GL rendering is a SHARED, desktop-wide DAEMON, not a per-project
  binary - this changes the §0 "same project, new window" answer.**
  piececraft-wraith itself contains **zero** GL/GLUT calls (confirmed
  by grep across its whole tree). Its own per-keystroke op
  (`ops/src/wraith_project_input.c:344-351`) just writes a scene
  descriptor line into `session/scene.objects.pdl` - e.g.
  `OBJECT tag=model id=piececraft_map_01 role=tile_zmap source=maps/
  map_01_z0.txt registry=assets/tiles/registry.txt camera=...`. The
  actual 3D work is done by wraith-alpha's ALREADY-SHARED
  `wraith_rgb_daemon.c`, which special-cases `role=="tile_zmap"`
  (line 3153), opens that map file straight off disk, and runs a CPU
  2D-DDA voxel ray-marcher per screen pixel against each tile's height
  (from `assets/tiles/*.tile.txt`'s `extrude=` field) - own comment:
  *"our world is a small regular grid, same shape of problem Minecraft
  solves, and DDA voxel traversal is correct-by-construction"*
  (`wraith_rgb_daemon.c:1606-1739`). The result lands in a shared
  RGBA32 framebuffer file; `wraith_gl.c` then does the ONLY GL work in
  the whole pipeline - one `glBegin(GL_QUADS)` textured quad blit +
  `glutSwapBuffers()` (`wraith_gl.c:567-588`). GL here is a *presenter*
  for a CPU-rendered image, not a scene-graph renderer.

**Corrected resolution, replacing the earlier one**: don't build
mutaclsym (or muchi-civ-pal, or the eventual Minecraft clone) its own
GL binary at all. Port a SHARED rendering service modeled directly on
`wraith_rgb_daemon.c` + `wraith_gl.c` - one daemon, launched once,
that any pal-family project can register a scene with by writing the
exact `scene.objects.pdl` descriptor format piececraft-wraith already
proves works, pointing at that project's own map file + a
mutaclsym-style glyph registry (already the right shape, per the
original point 1 above - registries don't need to change, only the
renderer target does). This is LESS new code than "each project gets
its own GL app": one shared raymarcher+presenter to build/maintain
once, every pal project gets a GL mirror for free just by writing a
scene descriptor, exactly matching how piececraft-wraith itself needed
zero GL code of its own. Directly answers "wraith conventions for gl
gui" for the future Minecraft clone too - it would be another scene
descriptor into the SAME shared daemon, not a fourth bespoke renderer.

**2D vs. 3D**: still one shared presenter, camera-driven, not two
render paths - piececraft-wraith's own key `8` toggle (2D top-down vs
3D perspective preview against the identical scene descriptor) is
already the proof this works with zero extra machinery per mode.

**Tech stack: see the GOVERNING CONSTRAINT section at the very top of
this document** - no direct GL primitive calls of our own, ever;
`wraith_gl.c`'s existing minimal RGB-reader is the only GL surface this
ecosystem needs, reused/ported as-is. That section supersedes anything
that reads otherwise below.


## 0b. mutaclsym GL/RGB mirror proof-of-concept — DONE, verified without visual access

Per direct instruction ("rewrite or reuse that wraith stuff here and
prove that we can run 2d/3d mutaclysm as well asap") and the follow-up
("do you see how wraith-gl creates receipts? you should do the same"),
mutaclsym now has a working GL mirror, proven correct entirely via text
receipts - no screenshot, no visual confirmation, matching the
constraint that this agent has no eyes on the screen.

**What was built** (mutaclsym/, not this doc's project):
- `pieces/registry/{terrain,furniture}/*_types.txt` extended with a
  5th `rgb_top(R,G,B)` field, reusing piececraft-wraith's own
  `*.tile.txt` wall/grass/tree values where the tile concept matches.
- `ops/compose_rgb_frame.c` - pure CPU C, zero GL calls. Reads the
  exact same state `compose_frame.c` reads (map/furniture/items/
  monsters/hero, byte-for-byte the same camera-clamp formula), writes
  a flat RGBA32 framebuffer (one `TILE_PX`=16px solid-color block per
  glyph) to `pieces/display/rgb_frame.raw`, plus a
  `rgb_frame.receipt.txt` (dimensions, byte count, FNV-1a-64 checksum
  - same algorithm as wraith_gl.c's `checksum_buffer()`).
- `pieces/registry/fonts/ascii/<32-126>/glyph.txt` - the pre-generated
  8x16 `#`/`.` bitmap font, copied VERBATIM from real 1.TPMOS's
  `wraith-alpha/assets/fonts/ascii/` (itself produced once, offline,
  by `ops/font-gen-op.c` via FreeType - not re-run here, just reused,
  same "copy the pre-extracted asset" precedent as piececraft-wraith's
  voxel CSVs). `compose_rgb_frame.c` ports `wraith_rgb_daemon.c`'s
  `load_glyphs()`/`blit_char()`/`blit_text()` (lines ~401-513 there)
  to stamp a header line (`map: turn:`) and footer line (HP/Hunger/
  Thirst/Stamina) directly into the RGBA buffer as plain pixel writes
  - answers the direct follow-up ("wraith-gl also has a way to render
  fonts/nav that we should also have"). Text is CPU-rasterized nav/HUD
  content, never a GL text API - there isn't one anywhere in this
  codebase, by the GOVERNING CONSTRAINT.
- `system/gl_mirror.c` - the only file allowed to call GL/GLUT, ported
  from `wraith_gl.c` (read in full, 799 lines) stripped to what a
  read-only mirror needs: `load_texture()`/`display()` (one
  `glBegin(GL_QUADS)` textured quad + `glutSwapBuffers()`, identical
  in shape to the original), `timer()` (polls `rgb_frame.raw`'s own
  `stat()` every 16ms - simpler than wraith_gl.c's separate trigger
  file, mutaclsym has no such pulse-file convention yet), `reshape()`
  (aspect-correct viewport math, no GL state beyond `glViewport`).
  Dropped: all wraith-alpha-desktop-specific mouse hit-testing/window-
  drag/multi-window calibration - mutaclsym has no window manager and
  no semantic click targets. **Kept, byte-for-byte in shape: the
  receipt-writing pattern** (`write_gl_display_receipt()`, called from
  the same three sites - texture_upload/display_swap/reshape - writing
  to `pieces/display/gl_display.receipt.txt`).
- `pal/main_loop.pal` calls `compose_rgb_frame` alongside `compose_frame`
  every tick (both bootstrap and the main gameplay loop) - one state,
  two renderers updated in lockstep, per the "mirror" definition in
  §0. `default_op.txt`/`scripts/build.sh`/`button.sh` all updated
  (`build.sh` builds `gl_mirror` best-effort so a machine without GLUT
  dev libs still builds everything else; `button.sh gl` launches it as
  an explicitly OPTIONAL 4th process, separate from plain `run`).

**How this was verified without looking at a window** (the actual
point of the receipt pattern): ran `compose_rgb_frame.+x` against the
real, live (idle, no session running) mutaclsym state; independently
reimplemented the exact same glyph-lookup/camera/rasterization/font-
blit logic in a throwaway Python script reading the SAME source files
(registries, map, hero/items/monsters state, the same glyph.txt bitmap
assets); compared all 184,320 pixels (640x288 RGBA) of the actual
output file byte-for-byte against that independent model -
**zero mismatches**. Separately recomputed the FNV-1a-64 checksum in
Python from the raw bytes - matched the C op's own receipt exactly.
Then ran `system/gl_mirror` for real against `DISPLAY=:0` (confirmed
present in this environment) for several seconds - it stayed up (no
crash), and its own `gl_display.receipt.txt` reported
`loaded_rgba_bytes` exactly matching `expected_rgba_bytes` (a full,
non-partial read) and `loaded_rgba_checksum_fnv1a64` **exactly matching**
both the writer's receipt and the independent Python recompute. Also
confirmed the live-update path: with `gl_mirror` already running,
moved the hero and regenerated the frame - the running process's
receipt updated to the new checksum, matching the new writer receipt,
proving the `timer()` poll-and-reload loop actually works. Finally ran
the whole thing inside the real `prisc+x` VM (not standalone) via
`button.sh`-equivalent commands, confirming `compose_rgb_frame` fires
correctly as a wired-in pal opcode during real gameplay, not just when
invoked directly.

Three independent checksums (C op's own receipt, Python reimplementation,
GL reader's receipt) agreeing exactly, on both the tile-color content
and the font/text content, is the proof standard this whole GL/RGB
mirror effort is held to going forward - not "it looked right in a
screenshot."

**Not yet done**: message-log tail and the numbered choice-footer
(compose_frame.c's ASCII output has both; the RGB mirror's v0 text
scope was deliberately kept to header+stats only, to prove the font
pipeline works before matching the full HUD). Applying this same
proven pattern to muchipal-editor and muchi-civ-pal is still open, per
§4.

**Mirrored input (ASCII terminal + GL window, live at once) - the real
1.TPMOS answer, found and ported**: per direct instruction to look at
how real wraith-gl handles this rather than invent it fresh.
`system/gl_mirror.c`'s `keyboard()`/`special_keyboard()` now forward
every keypress into `pieces/apps/player_app/history.txt` - the exact
same file/format `system/keyboard_input.c` already writes (bare
decimal keycode per line; arrow keys use the same 1000-1003 sentinel
values both `move_player.c` and real `wraith_gl.c`'s own
`map_special_key()` already use, not a fresh convention). That makes
the GL window a second live input source, usable simultaneously with
the terminal - which immediately raises the real question the user
asked: how does 1.TPMOS stop the same keypress double-firing into a
shared history file from two independent processes?

**Answer, confirmed by reading `pieces/joystick/plugins/
joystick_input.c` (a real, separate 201-line process, not part of any
GL file) and `pieces/keyboard/plugins/keyboard_input.c`**: NOT OS
window-manager focus routing (the assumption this doc's mutaclsym port
started from, and got corrected on). Both processes explicitly check a
shared lock file (`gl_os_has_focus()` there, reading
`pieces/apps/gl_os/session/input_focus.lock`) before writing, and back
off entirely while it exists - because `gl_desktop.c` genuinely has its
own `glutJoystickFunc(joystick, 50)` (confirmed, line 3982) and owns
keyboard directly via its own GLUT callbacks while it holds the lock, so
a joystick device (which has no window-focus concept at all - it's a
raw `/dev/input/js0` read, not routed by any window manager) still needs
an explicit signal to stop double-writing. mutaclsym's `gl_mirror.c` now
does its half of this: writes `pieces/system/gl_focus.lock` on startup,
removes it via `atexit()`. `system/keyboard_input.c` now checks the same
lock (`gl_has_focus()`) before every `append_key()` and silently skips
writing while GL owns it. Verified directly: launched `gl_mirror`, confirmed
the lock file's exact contents, killed it, confirmed the lock file was
removed - the acquire/release lifecycle works.

**Joystick itself**: real 1.TPMOS's `joystick_input.c` reads
`/dev/input/js0` via Linux's `linux/joystick.h`, converts button
presses to `2000 + button_number` and axis threshold-crossings (with
hysteresis to avoid spamming) to `2100 + axis*2 [+0/+1]`, appends those
into the SAME shared history file as any other key - joystick input is
just another producer into one input stream, not a separate pipeline.
Not yet ported for mutaclsym - real hardware would be needed to test
against, unlike the keyboard path (fully verified) or the RGB/font
pipeline (verified via receipts and independent recompute, no hardware
dependency). Tracked as later work; the porting shape is now fully
understood and documented here so it isn't reinvented differently later.

**A real bug the receipt/checksum method alone could not catch, and
what caught it instead.** Per direct instruction ("use stb image write
... get it to create u a jpg/png u can view whenever u need"),
mutaclsym now has `ops/dump_rgb_png.c` - a DEBUG-ONLY op (never wired
into `pal/main_loop.pal` or `default_op.txt`), using
`libraries/stb_image_write.h` (public domain, vendored from real
1.TPMOS's own `libraries/` rather than fetched fresh - the same header
`pieces/system/emoji_extract/` already uses there) to dump whatever
`compose_rgb_frame.c` last wrote as a real PNG, readable directly. The
FIRST time this ran, the PNG showed every wall tile rendered bright
magenta - `glyph_to_rgb()`'s "unmapped glyph" fallback color - instead
of the dark gray `terrain_types.txt` actually specifies. Root cause:
`glyph_rgb_top()`'s comment-line skip (`line[0] == '#'`) was treating
the wall glyph's OWN registry row (`#|t_wall|Wall|0|90,90,100` - `#` is
the wall's glyph, not a comment marker there) as a comment line, so it
was never found. **This exact bug already existed in `move_player.c`
and `tick_monsters.c`'s own copies of `glyph_walkable()`, predating
this session's GL work entirely** - invisible until now purely by
coincidence, because the miss-fallback there (`result = 0`, "not
walkable") happens to be the correct answer for walls specifically.
Fixed in all three files: the comment test is now `line[0]=='#' &&
line[1]!='|'` (a real data row is always exactly one glyph char before
its first pipe, so `line[1]=='|'` unambiguously means "data row," even
when that glyph happens to be `#`). Re-verified with the same
independent-Python-reimplementation-plus-checksum method as before -
zero mismatches across all 184,320 pixels, now with the walls actually
gray in both the raw bytes and the PNG.

**Why this matters beyond the one bug**: the checksum/receipt method
proves internal self-consistency (the C code did what it always does,
deterministically) - it does NOT prove the output is actually correct,
because an independent verification script that makes the SAME mistake
as the code it's checking will agree with it perfectly while both are
wrong (exactly what happened here - the first Python verification pass
copied the identical `line.startswith("#")` comment-skip and so
"confirmed" the bug). A human-viewable image is what actually catches a
class of bug that self-consistent checksums structurally cannot. Going
forward, `dump_rgb_png.+x` should be run and actually looked at after
any change to `compose_rgb_frame.c` or the registries it reads, not
just checksum-verified.


## 0c. piececraft-3d-pal foundation — the shared-daemon bet, actually tested on a second project

`2.muchi-verse/piececraft-3d-pal` (empty dir the user had already
created) is now scaffolded as a real pal/prisc+x project - `system/`
copied verbatim from mutaclsym (proven project-agnostic), its own
`ops/compose_rgb_frame.c` (v0: flat top-down, one `rgb_top` color per
tile, no camera/raymarch yet), and **`system/gl_mirror.c` copied from
mutaclsym completely unmodified except for the `WIDTH`/`HEIGHT`
constants and two string literals** (window title, the focus-lock's
`project=` field). Per user direction, map/3D editing itself belongs in
`muchipal-editor`, not here - this project is content + a mirror, not
an editor.

Content: real piececraft-wraith assets, copied in as-is rather than
invented fresh (`pieces/registry/tiles/*.tile.txt` + `registry.txt`,
`pieces/world_01/map_01/map.txt` = the real `map_01_z0.txt`) - two
genuinely different real 1.TPMOS file formats from mutaclsym's own
pipe-delimited registries (`glyph=id` and per-tile `key=value` files),
read as-is rather than translated into mutaclsym's convention.

**Verified exactly like mutaclsym's mirror**: `compose_rgb_frame.+x`
run against the real map data, its receipt checksum matched by
`gl_mirror`'s own receipt after a real GLUT run against `DISPLAY=:0`
(`0x4EEDEEE13CA7C34C`, byte-for-byte, non-partial). This is the actual
test of §0's "one shared rendering service" bet, not just an
assertion - the exact same `gl_mirror.c` binary-shape now genuinely
serves two different projects.

**A second real bug, same root cause, caught by the SAME debug-PNG
method within minutes of fixing the first one**: `compose_rgb_frame.c`'s
`load_tile_types()` had the identical `line[0]=='#'` comment-skip bug
as mutaclsym's `glyph_walkable()` (see §0b) - `registry.txt`'s own wall
row is `#=wall`, so it was being skipped as a comment, and every wall
tile rendered magenta in the first debug PNG. Fixed the same way
(`line[1]=='='` is the real "is this a data row" test, not
`line[0]=='#'`). Worth naming plainly: this is exactly the failure mode
§0b warned about - an agent with no visual access can write the same
systematic bug twice in a row across two different files in the same
session, and only a human-viewable image catches it. `dump_rgb_png.+x`
was ported here too, unmodified in shape.

**Explicitly NOT done**: the real voxel DDA raymarch (true 3D
perspective, using each tile's `extrude` height -
`wraith_rgb_daemon.c:1606-1739`'s actual algorithm) - deliberately
deferred rather than rushed alongside everything else proven this
session. Also open: hero/movement/camera (map_01 currently renders
whole and fixed, no player piece at all yet).

**Follow-up: real perspective 3D + working first/third-person/free
camera, now the default.** Per direct instruction ("piececraft should
default to 3d... wraith-gl piececraft 3d has guidance on how to do
this, along with camera controls and pov... check it out and bild a
bit more there"), researched real `projects/piececraft-3d` (the
ORIGINAL, not the wraith validation clone) and found
`wraith_rgb_daemon.c`'s actual tile_zmap 3D path is NOT a raymarch at
all - it's proper perspective-projected extruded boxes with
Sutherland-Hodgman near-plane clipping and a real depth buffer
(`world_to_camera_space`/`project_world_point_ex`/`clip_poly_near`/
`fill_poly_px`/`draw_box`, ~950-1461 there). Ported that pipeline
faithfully into `compose_rgb_frame.c` (scoped: no `voxel_source`
sub-voxel grids, no alpha pass - nothing in this map uses either yet),
plus the ground wireframe grid. `render_mode` in the receipt now reads
`3d_box_v1` by default; `8` toggles to the flat top-down v0 path.

The reference's own `camera_mode` 1/2/3 presets were meant to be
first-person/third-person/free-camera but "wasn't working yet" (direct
user confirmation) - confirmed concretely: preset 2's hardcoded
(pitch=45, cam_y=10, cam_z=-12) projects entirely off-frame at this
project's scale (verified via the debug PNG, not assumed - see
`ops/compose_rgb_frame.c`'s git history/comments for the exact math).
Built the real thing instead of porting the broken presets: yaw now
rotates the world around the CAMERA'S OWN position (not a fixed
map-center pivot the reference used for its orbit-style presets) -
verified algebraically that setting `pivot=camera` in the same
`world_to_camera_space()` formula collapses to standard look-direction
rotation. `ops/camera_input.c` drives a single yaw-relative rig
position (W/S forward-back, A/D strafe, Z/X vertical, Q/E turn, R/F
look up/down - all relative to current facing, not fixed world axes,
which a fixed-axis pan scheme can't express once turning exists).
`camera_mode` only changes camera PLACEMENT relative to that rig: 1 =
camera is the rig (eye height 1.6); 2 = pulled back 6u + up 3u from the
rig with a downward pitch bias (chase-cam); 3 (default) = the rig
itself, started pulled back 8u for a wide unobstructed view. All three
verified visually as genuinely distinct (first-person shows a close
ground-plane horizon with whatever's directly ahead; third-person shows
an elevated angled view; free-camera a wide establishing view), and
turning (Q/E) + walking into a box (W) were each confirmed to produce
the correct, different resulting frame - not just "it compiles."


## 0a. Emoji/ASCII parity

Real precedent exists, but it is NOT a finished thing to copy. Flagged
by direct instruction as important; researched against op-ed/fuzz-op/
fuzz-op-gl specifically, not assumed.

**The toggle itself is real and works.** `op-ed_manager.c:1079` (key
`'4'`) and `fuzz-op_manager.c:694-700`/`fuzz-op-gl_manager.c:572-578`
(key `'6'`) all flip a boolean `emoji_mode`, persisted per-project to
`pieces/emoji_mode.txt` (`emoji_mode=0`/`1`). So "switch between emojis
or ascii in the same ASCII-mode renderer" is a proven, real,
copyable pattern - a single toggled flag, not per-tile state.

**The glyph↔emoji mapping is NOT one registry - it's two hardcoded C
tables that actively disagree with each other**, which is the mistake
to avoid repeating, not a pattern to copy:
- `op-ed_manager.c:113-114` - parallel arrays used only by the tile
  *picker*: `"T"→🏰`, `"R"→🌲`.
- `pieces/apps/playrm/ops/src/render_map.c:47-63` - a separate
  `GlyphMap` struct array used to convert *placed* glyphs on render:
  `"T"→🎯`, `"R"→🪨` - same ASCII glyphs, different emoji, genuinely
  inconsistent within the same project family.
- `fuzz-op-gl/assets/tiles/*.tile.txt` (e.g. `tree.tile.txt`:
  `ascii=R` / `unicode=🌲`) is the one place this is genuinely
  registry-shaped (like a `terrain_types.txt` row) - but it's scoped to
  that one project's tile assets, not shared with the other two tables.

**Correction after the user redirected research to `piececraft-wraith`
instead of `gltpm`: the GL tie-in DOES exist as complete, working
code - just not in the project I checked first.** `gltpm_parser.c`'s
`unicode=` field really is dead (confirmed above, that finding stands
for THAT project). But `piececraft-wraith`'s own tile registry
(`wraith-projects/piececraft-wraith/assets/tiles/*.tile.txt`) is a
different, more complete format that closes the exact loop the user
described. Three of its eight tiles are explicitly emoji-backed:

```
tile_id=rock_emoji
ascii=k
unicode=🪨
voxel_source=assets/emoji/rock_voxels_8.csv
```
(same shape for `tree_emoji`/🌲 and `dog_emoji`/🐶). The referenced
CSVs genuinely exist on disk
(`.../piececraft-wraith/assets/emoji/{rock,tree,dog}_voxels_8.csv`,
each an 8x8 grid - `# resolution=8` header - matching `emoji_xtract.c`'s
NxN-grid output shape exactly) and `voxel_source` is GENUINELY parsed
and consumed by `wraith_rgb_daemon.c`, confirmed by direct grep, not
inferred: parsed at line 1334, sampled during the real 3D raymarch at
lines 1760/1765 (`sample_voxel_pixel`), and ALSO used for a 2D
thumbnail preview at lines 2237/2243
(`draw_voxel_grid_2d_thumbnail`) - i.e. both the 2D and 3D presentation
modes read the same emoji-derived voxel data, not just the 3D one.

**So the full pipeline is real and provably working, end to end,
inside `piececraft-wraith` specifically**: an ASCII glyph (`k`) has a
registry row with an emoji (`unicode=🪨`) and a `voxel_source` pointing
at real voxel data rasterized from that same emoji (almost certainly
via the `emoji_gen_atlas.c`→`emoji_xtract.c` FreeType pipeline egg-pals
already mirrors, given the identical `voxels_N.csv` naming and NxN grid
shape - I did not find an explicit "generated by" comment inside the
CSVs themselves, so treat the tool-chain link as very likely, not
100% proven by a citation, while the parse/consume link IS 100%
proven). This is the actual reference implementation to port for
muchi-verse projects, not something to design from scratch: extend
each project's glyph registry with `unicode=`/`voxel_source=` fields
in exactly this shape, generate the voxel CSVs with the same FreeType
pipeline egg-pals' build already has, and feed them to the SAME shared
`wraith_rgb_daemon.c`-style presenter this doc's §0 correction already
recommends reusing wholesale.

**What this means for the muchi-verse family - port `piececraft-wraith`'s
format, don't repeat op-ed/fuzz-op-gl's unfinished version of it:**
1. One unified glyph registry per project, extending the shape
   mutaclsym's registries already use
   (`glyph|id|name|walkable` -> add `unicode=`/`voxel_source=` fields
   per row, matching `piececraft-wraith`'s `*.tile.txt` shape exactly,
   not op-ed's two disagreeing hardcoded tables).
2. ASCII renderer picks plain glyph or emoji per a toggle flag (real,
   proven precedent from op-ed/fuzz-op - one key, one boolean,
   persisted to a small state file, same shape as `emoji_mode.txt`).
3. Voxel/tile data for each emoji-backed entry generated once via the
   FreeType pipeline egg-pals' build already mirrors
   (`emoji_gen_atlas.c`→`emoji_xtract.c`), in the same `voxels_N.csv`
   shape `piececraft-wraith`'s own assets use.
4. The shared presenter daemon (§0's corrected resolution - a
   `wraith_rgb_daemon.c`-style port, not a per-project GL binary) reads
   `voxel_source` off the registry row exactly the way the real
   `wraith_rgb_daemon.c` already does (parse: line 1334, 3D raymarch
   sample: 1760/1765, 2D thumbnail: 2237/2243) - this part is a direct
   port of proven code, not new design.
Not implemented anywhere in the full muchi-verse GL-mirror-daemon sense
described above (the `wraith_rgb_daemon.c`-style shared presenter that
serves MULTIPLE projects at once is still real future work, recorded
here so it isn't lost) - **but a real, working, per-project version now
exists in mutaclsym**, built 2026-07-16/17 (see mutaclsym/dox/
00-HANDOFF.md's own checklist entry for the full writeup). First pass
(2026-07-16) under-scoped this: reasoned mutaclsym's GL renderer has no
font/voxel pipeline of any kind, so genuine emoji rasterization
"couldn't apply there," and shipped a themed flat-color swap instead
(`rgb_top_emoji`). **That reasoning was wrong, corrected 2026-07-17
after direct instruction to check wraith-alpha specifically** - it
turns out wraith-alpha DOES have the real emoji-gen_atlas.c/
emoji_xtract.c FreeType pipeline this section already describes above
(confirmed live on this machine: NotoColorEmoji.ttf present, both
tools already built, genuinely rasterize real color emoji glyphs) -
mutaclsym was simply never wired to call it. Now is: real
`voxels_16.csv` assets (N=16, not the 3D pipeline's raymarch-oriented
N, but the exact same CSV format and extraction tools) were generated
once per registry entry into `pieces/registry/emoji_assets/<id>/`, and
`ops/compose_rgb_frame.c` blits those real pixels on top of the flat
base color every frame (no live FreeType calls in the per-frame path,
matching wraith_rgb_daemon.c's own documented reason for NOT doing
that). So step 3 of the plan above (FreeType voxel extraction) is now
genuinely done for mutaclsym, just consumed as flat 2D per-tile pixels
rather than through a 3D raymarch - step 4 (a SHARED presenter serving
multiple projects) remains the real, larger future work. mutaclsym's
`unicode=` registry fields are still in the right shape to extend with
`voxel_source=` if/when that shared presenter gets built.


## 1. The bigger discovery: op-ed ALREADY has the RPG-Maker event system

This is not a feature to invent - port it. `projects/op-ed/README.md:4`
literally: *"OP-ED is a 'Thin Engine' RPG Maker for TPMOS... design
worlds, pieces, and events that are immediately playable via the PAL
runtime."* And roadmap item (line 16): *"Slice 3: Event Editor (In
Progress). Moving from raw PAL editing to high-level 'Block'
building."*

The implementation is `projects/op-ed/manager/pal_editor_module.c`. Its
`available_ops[]` table (lines 64-70) IS the RPG Maker event-command
palette, verbatim:

```c
{"Show Text",    "SET_RESPONSE \"%s\"", "Message", "", "RPG: Display message"},
{"Move Entity",  "CALL_OP \"move_entity\" \"%s\" \"%s\" \"${project_id}\"", "Piece ID", "Dir (w/a/s/d)", "Physics: Move entity"},
{"Change Map",   "TRANSITION \"projects/op-ed/games/${project_id}/maps/%s.txt\"", "Map Name", "", "World: Teleport"},
{"Wait",         "SLEEP %s", "Millis", "", "Logic: Pause execution"},
{"Give Item",    "CALL_OP \"inventory_op\" \"%s\" \"add\" \"%s\"", "Piece ID", "Item ID", "RPG: Add to inventory"},
{"Set Variable", "SET_STATE \"${pal_editor_piece}\" \"%s\" \"%s\"", "Key", "Value", "Logic: Set piece state"},
{"Halt",         "HALT", "", "", "System: Stop script"},
```

Each row is: a friendly RPG-Maker-flavored name, a PAL/op-call template
string with `%s` fill-ins, and up to two labeled parameter prompts. A
user builds an `Instruction instructions[MAX_INSTRUCTIONS]` list per
`(piece, event_name)` pair (e.g. piece `npc_bob`, event `on_interact`),
shown as a numbered clickable list (`write_editor_state()`, line 357:
`"║ %2d: " ... onClick="SET_SELECT_INSTR:%d"`), with insert/delete/
reorder around line 278. `save_script()` (lines 160-178) writes the
assembled sequence to `projects/%s/pieces/%s/events/%s.asm` - and
that's a REAL pal script, not editor-internal data: `op-ed_manager.c`'s
`trigger_event()` (~line 933-939) runs it through the actual VM
(`'prisc+x' '%s'`) when the handler is a `.asm` path, and
`bind_event(piece_id, event, handler)` (lines 945-951) attaches it via
`piece_manager.+x add-method` - **the same "add a METHOD row to this
piece's table" operation mutaclsym's own `piece.pdl` METHOD table
already models**, just invoked through a shared helper op instead of a
hand-edited text file.

A concrete, small sample of what gets generated -
`projects/op-ed/games/test-game-01/scripts/talk.asm`:
```
SET_RESPONSE "Hello! I am a test NPC."
HALT
```

**Known real limitation, not something to solve as a prerequisite**:
`available_ops[]` has no "Conditional Branch" row. Op-ed's own event
system is linear-only today - no if/else, no loops. mutaclsym's own
`prisc+x` VM DOES have `beq` (exact-equality branching, already used
extensively - see `nav-refactor-2.txt`), so muchipal-editor could
actually exceed op-ed's own event system here once it wires a
"Conditional" block type in, rather than being capped by op-ed's
current ceiling. Don't treat "no branching" as something blocking a v1
- op-ed itself ships without it.

**"Plugin system" - already answered, don't build a new one.** A broad
grep for plugin manifests/registries/hot-loading/command-palette
infrastructure across the entire 1.TPMOS tree found nothing beyond the
`pieces/apps/*/plugins/` DIRECTORY NAMING convention (`README.md:219`
confirms it's just where compiled op binaries live). There is no
elaborate plugin system to model - **the ops themselves already ARE
the plugin system**, in exactly the sense op-ed's `available_ops[]`
uses them: each row calls a real, independently-existing op binary by
name (`move_entity`, `inventory_op`) with positional args. "Use the
plugin system" and "use existing ops, callable by name with args" are
the same instruction here, not two different things to reconcile.


## 2. What this means concretely for muchipal-editor

Model `muchipal-editor` directly on `pal_editor_module.c`, not from
scratch:

1. A command-palette table (mutaclsym/civ-clone-flavored, same shape
   as `available_ops[]`): friendly name, op-call template, 0-2 labeled
   param prompts, a category tag ("RPG:"/"Physics:"/"World:"/"Logic:"/
   "System:" in op-ed's own examples - keep that convention, it reads
   well in a picker UI). Seed it with op-ed's own generic rows (Show
   Text, Wait, Set Variable, Halt translate directly) PLUS mutaclsym-
   specific ones once §3 below identifies which existing ops are
   already call-by-name-with-args friendly.
2. Per-piece, per-event instruction lists, numbered/clickable, same
   interaction shape as `write_editor_state()` - and this project
   family already has the exact right UI primitive built and PROVEN
   for this: the bracket-cursor + multi-digit-accumulator + Enter-
   commit numbered list from `nav-refactor-2.txt` (mutaclsym's outer
   action bar AND its craft/inventory overlay panels, egg-pals' whole
   menu system). Don't reinvent op-ed's raw chtpm-button list style -
   reuse the already-verified pal-native numbered-list mechanism this
   family already has, matching the user's own instruction elsewhere
   this session to reuse working machinery rather than duplicate it.
3. `save_script()`'s output target - a `.asm`/pal script file per
   `(piece, event)` - maps directly onto mutaclsym's own
   `pieces/world_01/.../<piece>/piece.pdl` METHOD rows (just needs a
   `bind_event`-equivalent op that appends/rewrites a METHOD line,
   mirroring `piece_manager.+x add-method`'s role - mutaclsym doesn't
   have a `piece_manager` op yet, only hand-edited `piece.pdl` files;
   this is a real, small, concrete new op to build when the editor
   actually needs to write bindings programmatically instead of by
   hand).
4. Save/level folder structure: already covered by the OTHER half of
   op-ed's precedent already ported into mutaclsym this session (see
   `mutaclsym/dox/01-cdda-architecture.md`'s Phase 7 entry) - one
   folder per game/level under a `games/`-equivalent dir, `cp -r` to
   save, `rm -rf`+`cp -r` to load. muchipal-editor's own "project"
   folders should follow the identical shape, since the whole point is
   mutaclsym/civ-clone save folders and muchipal-editor project folders
   need to be the SAME kind of thing for either to open the other's
   data.


## 3. "Make sure our ops are compatible" - the actual audit to do

This is the direct ask, and it's a real, boundable piece of work
(don't do it as part of this planning pass - this section is the
checklist for when you come back to mutaclsym, per the user's own
sequencing: "then we will come back to mutaclysm - i just want to make
sure our ops are compatible"):

- **Every mutaclsym op that should be event-block-callable needs a
  stable, positional-args CLI contract**, matching op-ed's
  `CALL_OP "name" "arg1" "arg2"` shape. Audit `ops/*.c`: `craft.c`
  already takes an optional `recipe_id` argv - good shape. `pickup.c`/
  `drop.c`/`eat.c` take NO args (operate on hero's current position/
  first-match-in-inventory implicitly) - fine for direct play, but an
  event block calling "give this NPC's target this specific item"
  can't express that yet without an item_id argv. Don't change these
  op signatures for their OWN sake - only if/when an actual event
  block needs the finer control, matching this project's established
  "don't build for hypothetical requirements" discipline.
- **`ops/choice.c`'s hero-specific hardcoding** (`hero_path` is a
  literal `pieces/world_01/map_start/hero/state.txt` path throughout
  choice.c, move_player.c, etc.) is the real compatibility gap, not
  individual op signatures. An event system needs to trigger ops
  against ARBITRARY pieces (an NPC's own event, not just the hero's),
  which is exactly the "active-target/xlector" concept already
  identified and deliberately deferred in `nav-refactor-2.txt` §4 step
  5. That deferred item is now the actual prerequisite for event-system
  compatibility, not a nice-to-have - worth promoting in priority
  once this work resumes.
- **`piece.pdl`'s METHOD table is already the right shape** - no
  redesign needed there, it already IS "a piece's bound event handlers,
  by name." The gap is only that it's hand-edited today; a
  `piece_manager`-equivalent op (see §2.3) is the only new piece needed
  to make it editor-writable.
- Registries (`terrain_types.txt`, `furniture_types.txt`, `items.txt`,
  `monster_types.txt`) are already exactly the shape a GL mirror needs
  (§0) - no change needed, just confirmed reusable as-is.
- **A third project now needs this same treatment, but a bigger
  version of it**: `2.muchi-verse/wsr-pal` (an economic simulator,
  "Wall $treet Race" - governments/corporations/players/payroll/
  financing mechanics), surveyed and documented locally at
  `wsr-pal/dox/00-overview.md`. Unlike mutaclsym, wsr-pal is **not**
  pal/prisc+x-based at all today - conventional main-loop +
  `system()`-dispatch CLI, no `.pal` script, no VM. It needs an actual
  PORT to the pal architecture (entities → pieces, verbs → ops,
  its real `presets/schedule.txt` interval-scheduler → a `pal`
  turn-loop) before it can be opened in `muchipal-editor` or share ops
  with mutaclsym/muchi-civ-pal - not just an argv-shape audit like
  mutaclsym needed. Real, reusable economic-sim content already exists
  there worth carrying forward (financial-statement entity data,
  government/corporation seed registries, the schedule-driven job
  cadence concept) - see the local doc for the full survey, including
  a real bug found in passing (`payroll_loop.c` contains no payroll
  logic - it's an accidental copy of the main menu). Not started; the
  local doc has the phased shape of this work, not a task list to
  execute blindly.


## 4. Explicitly open / not decided here

- **Real RPG Maker file-format import/export** ("later we will even
  write ports for real rpg maker engine") - no RPG Maker source is
  available in this environment; this would mean reverse-engineering
  the actual `.rpgproject`/event-command binary or JSON format from
  public documentation, a substantial separate research effort, not
  started or scoped here.
- **`muchi-civ-pal` itself** - not designed at all yet beyond "same
  pal/prisc/ops architecture, natural second consumer of
  muchipal-editor once it exists." No civ-specific mechanics (turns,
  tech tree, city/tile management) scoped.
- **The Minecraft clone** - per direct instruction, explicitly deferred
  to a later request. Whatever GL-mirror mechanism gets built for
  mutaclsym (§0/§2) is the same one it would reuse - no separate
  rendering architecture needed for it specifically, but nothing about
  a true 3D voxel game beyond that has been thought through.
- **Conditional branching in the event-block editor** - real, valid
  future work (prisc+x already supports the underlying `beq`
  primitive), not scoped in detail, and not something op-ed's own
  precedent requires for a v1.
