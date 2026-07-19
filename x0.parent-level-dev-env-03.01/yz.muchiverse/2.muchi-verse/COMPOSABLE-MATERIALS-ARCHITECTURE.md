# Composable materials architecture — granular drag-and-drop objects
# (atoms → cells → organisms → environments) and the ops/FSM bridge

Status: PLANNING DOCUMENT, grounded in direct research against real,
already-existing code and docs across four projects - every claim below
either cites a real file or is explicitly marked speculative. Written as
a sibling to `GRAND-ARCHITECTURE.md` in this same directory (that doc
scopes itself to op-ed-style event editing and the GL mirror; this one
covers the part it explicitly says it does NOT attempt: physics/
materials/AI-brain architecture spanning multiple projects).

================================================================================
0. WHAT GROUNDS THIS
================================================================================
Four sources, all read directly before writing this, not assumed:

- `❤️‍🔥️.⚛️.wrai-dust-fable-0.0.md` (ZEST-10.00 root) - the "Dustopia"
  physics/chemistry vision: size=time scaling, a λ-scale/zoom spectrum
  ("Fuzz Pets... electron → muon → tau is one pet at different λ",
  §2 Pillar 3), a 4-symbol `0/1/Z/3` bond-state chemistry table (§11)
  cheap enough to run at any zoom level, and an FSM "brain chassis" with
  4 interchangeable decision tiers (`preset|weighted|rl|llm`, §12) that
  is explicitly the SAME chassis for pets, corporation boards,
  governments, and R&D agents alike.
- `GRAND-ARCHITECTURE.md` (this directory) - `piececraft-wraith`'s
  PROVEN, WORKING registry-row-implies-rendering pipeline: a tile
  registry row (`ascii=k / unicode=🪨 / voxel_source=assets/emoji/
  rock_voxels_8.csv`) is parsed and consumed for real by
  `wraith_rgb_daemon.c` (cited line numbers: parse 1334, 3D sample
  1760/1765, 2D thumbnail 2237/2243) - and op-ed's `available_ops[]`
  event-command palette, which already generates real `.asm`/`.pal`
  scripts from a constrained picker UI, not hand-typed syntax.
- `y0.mutaclsym+05.00/mutaclsym/dox/01-cdda-architecture.md` (read
  directly for this doc, not recalled) - the REAL current state of the
  nearest working engine: `pieces/registry/{items,recipes,terrain,
  furniture,monsters}/` are flat pipe-delimited files (e.g.
  `recipes.txt`: `id|name|result_item_id|req1:qty1,req2:qty2`,
  confirmed working end-to-end via `craft.c`'s real consume/produce
  logic). §4 designs a per-piece FSM layer (`.pal` script dispatched on
  `current_state`, a shared `bot::navigate/interact/wait/choose/assert`
  op vocabulary, explicitly "one system, two uses" for testing bots AND
  AI player bots) but states plainly "**Not implemented yet**" - and
  names `tick_monsters.c`'s hardcoded chase-then-attack C logic as the
  literal, already-identified candidate for that FSM upgrade ("the
  first real attachment point the FSM layer... anticipated"). §7 flags
  **"Registry granularity"** as an explicitly OPEN, unresolved decision
  - flat one-line-per-type files may not scale, might need one piece-
  directory-per-item instead - "not decided, revisit if a flat file
  gets unwieldy."
- `y0.muchi-pal-chat/ROADMAP-models.txt` §11-13 (this session's own
  earlier work) - the growth_policy.txt / self-prune / shared-"trunk"
  / WORDS-BIAS-BEHAVIOR routing-table design, built for iqabod
  curricula specifically but written generically enough to reuse here.

This document's job: answer the direct ask - granular, drag-and-drop
composable objects spanning atoms/chemicals/cells up through humans/
animals/robots up through environments (space/houses/cities), with
growth/shrink limits and voxel/pixel generation baked in - by showing
it's mostly ALREADY-BUILT mechanisms (craft.c's recipe shape,
piececraft-wraith's registry-implies-render pipeline, §13's growth
policy) generalized and connected, plus a direct, honest answer to
"can these things build/train/bugfix themselves," which is genuinely
the least-settled part of the whole vision and is treated as such below
(§7).

================================================================================
1. THE CORE IDEA, STATED PLAINLY
================================================================================
One registry shape, one λ/granularity axis, one bond-state reactivity
table, used recursively at every scale: an "atom" composes into a
"molecule," molecules compose into a "cell," cells compose into an
"organism" (human/animal/robot), organisms plus structure composables
(walls, furniture) compose into a "house," houses plus organisms
compose into a "city." Dropping any ONE of these into a game/level is
enough to imply everything below it in the chain, because each
registry row already names what it's made of and how to render it -
the same way a piececraft-wraith tile row already implies its full 3D
voxel representation today, just for one narrow case (a rock, a tree).
This is Dustopia's own "fractal zoom" idea (wrai-dust-fable §2/§14),
made into an actual registry mechanism instead of prose - and it
directly answers mutaclsym's own open "registry granularity" question
(§0 above) by proposing the missing field set.

================================================================================
2. THE COMPOSABLE-OBJECT DATA SHAPE
================================================================================
One piece-directory per composable (resolving mutaclsym §7's own named
tension - a flat file can't comfortably hold this many fields per
entry), e.g. `pieces/registry/composables/<id>/state.txt`:

  granularity=atom|molecule|cell|organism|structure|environment
  lambda=<numeric scale/λ>          (Dustopia's own λ field, reused
                                      verbatim, not a new axis - a
                                      composable's granularity LABEL is
                                      just a human-readable name for a
                                      position on this one continuous
                                      scale, matching "electron → muon
                                      → tau is one pet at different λ")
  bond_profile=0|1|Z|3               (§11's 4-state chemistry - this is
                                      what makes granularity MEANINGFUL,
                                      not just a label: reactivity/
                                      compatibility with other
                                      composables comes from the same
                                      shared bond table §11 already
                                      worked examples for)
  composed_of=<id>:<qty>,<id>:<qty>  (recursive composition list - this
                                      is LITERALLY mutaclsym's existing,
                                      PROVEN `recipes.txt` shape
                                      generalized from "crafting recipe"
                                      to "what this object IS made of."
                                      Reuse craft.c's exact consume/
                                      produce mechanism for
                                      construction; nothing new to
                                      build here, a wider application
                                      of code that already works.)
  growth_limit=<max>/ shrink_limit=<min> / self_prune=1|0
                                     (directly reuses ROADMAP-models.txt
                                      §13.2/§13.3's growth_policy.txt/
                                      self-pruning design, generalized
                                      from "curriculum vocab size" to
                                      "any composable's growth" - same
                                      config mechanism, wider scope,
                                      not a new one)
  voxel_source=<path.csv> / unicode=<emoji>
                                     (DIRECTLY reuses piececraft-wraith's
                                      ALREADY-WORKING registry row shape
                                      - §0's cited parse/sample/
                                      thumbnail line numbers are real,
                                      running code today, just scoped to
                                      tiles. This field is the actual
                                      "voxel/pixel image generation
                                      ability" tie-in the vision asks
                                      for, and it needs zero new
                                      rendering code - only more
                                      registry rows in this same shape,
                                      fed to the SAME shared
                                      `wraith_rgb_daemon.c`-style
                                      presenter GRAND-ARCHITECTURE.md §0
                                      already resolved on.)
  decision_mode=preset|weighted|rl|llm   (ONLY present on agent-tier
                                      composables - an "organism" that
                                      acts, not a "molecule" that's
                                      inert. Wrai-dust-fable §12's
                                      4-tier FSM chassis, unmodified -
                                      see §7 below for how this connects
                                      to hardcoded ops.)

================================================================================
3. CROSS-SCALE WORKED EXAMPLE
================================================================================
Mirrors wrai-dust-fable §11's own "worked example, not an equation"
style, walked through the full composable chain the vision named:

  atom (bond_profile=1, lambda=0.001)
    -> composed_of nothing (base tier, like a special_token row in
       IQABOD's own vocab format - a "leaf" of the composition tree)
  molecule "iron_oxide" (composed_of=atom_iron:2,atom_oxygen:3)
    -> reactivity with other molecules comes from a bond(A,B) lookup
       exactly like §11's worked table
  cell "rust_forming_cell" (composed_of=molecule_iron_oxide:40,
       membrane per §11's Worked Example 2 - a `Z`-outward/`1`-inward
       shell)
    -> metabolism/replication toy-rules exactly as already specified
  organism "iron_golem" (composed_of=cell_rust_forming_cell:5000,
       decision_mode=preset)
    -> NOW an agent - has an FSM piece, per §7
  structure "golem_workshop" (composed_of=organism_iron_golem:3,
       item_furnace:1 - reusing mutaclsym's EXISTING furniture_types.txt
       row shape, just with granularity/voxel_source fields added)
  environment "iron_district" (composed_of=structure_golem_workshop:12,
       organism_*: many)
    -> at this tier, `Mar$.$treetRace`'s existing corporation/government
       machinery (wrai-dust-fable §2 Pillar 2) attaches directly - a
       "city" composable IS the economic-sim layer's own scope, not a
       new thing to build; the composable chain hands it a populated
       world to run an economy on top of, exactly matching wrai-dust-
       fable §3's fusion diagram.

**Robots ("metal etc"):** the SAME schema, not a fork - just a
different `bond_profile`/material set at the atom/molecule tier (alloys
instead of organic compounds) and typically `decision_mode=preset` or
`weighted` by default rather than starting at `llm` (a robot doesn't
need "personality" the way an organism built for it might, though
nothing stops one from having one - `decision_mode` is a per-instance
field, not a per-granularity-tier constant).

**Environments at the largest scale ("space"):** named directly in the
ask, and the most speculative tier in this whole document - worth
saying so plainly rather than pretending it's as grounded as the rest.
An "environment" composable at planetary/orbital scale is real per
wrai-dust-fable §5's aerospace-audience framing (macro-scale, gravity-
dominant, per Dustopia's own size=time rule putting big/slow things at
the opposite end of the SAME λ spectrum small/fast atoms sit on) - but
nothing about rendering/simulating at that scale has been designed
anywhere yet. Flagged as future work, not solved here.

================================================================================
4. "DROP IT IN AND IT IMPLIES EVERYTHING" - HOW, MECHANICALLY
================================================================================
Demystified on purpose, since stated baldly this can sound like it
needs inventing: dropping a composable into a level is ONE line in a
`scene.objects.pdl` file (piececraft-wraith's own proven format,
GRAND-ARCHITECTURE.md §0: `OBJECT tag=model id=<composable_id>
role=... source=... registry=...`), and everything else is a chain of
registry lookups FROM that one id:
  - `voxel_source`/`unicode` -> the shared rendering daemon already
    knows how to draw it (§0's cited, working code).
  - `composed_of` -> recursively resolves what it's made of, using the
    same lookup `craft.c` already does for recipes today.
  - `bond_profile` -> determines how it reacts with whatever else is
    already in the scene, via the same lookup table §11 already
    specifies.
  - `decision_mode` (if present) -> attaches an FSM piece per §7.
Every link in that chain is either ALREADY BUILT (the renderer, the
recipe-resolution logic) or ALREADY DESIGNED ELSEWHERE in this exact
conversation (growth policy, FSM chassis) - "implies everything" is
registry lookups chained together, not a new inference engine.

================================================================================
5. GROWTH/SHRINK LIMITS AT THE COMPOSABLE LEVEL
================================================================================
Same mechanism as ROADMAP-models.txt §13.2/§13.3, generalized: a
composable's `growth_limit`/`shrink_limit`/`self_prune` fields are
read by the same kind of periodic policy-driven process already
designed there for curriculum vocab size, just checking a different
quantity (cell count inside an organism, structure count inside an
environment, instead of vocab row count inside a curriculum). Worth
building as ONE shared "growth policy" mechanism, not two - a
composable IS conceptually a curriculum-shaped growable thing, per
§13's own WORDS/BIAS/BEHAVIOR framing, this is just a fourth entry in
that taxonomy: **MATERIAL** (composable objects, governed by the same
config-driven growth policy as the other three).

================================================================================
6. PIXEL/VOXEL GENERATION - THE SAME ASSET TYPE, TWO CONSUMERS
================================================================================
Worth naming explicitly since it's a real, concrete integration point,
not just a shared format: `y0.muchi-pal-chat/ROADMAP-models.txt` §10
already designs emoji-scale voxel-grid GENERATION (blend/interpolate
existing emoji CSVs as the v1 vertical slice) for chat's own
`generate_emoji` tool, targeting the EXACT SAME `voxels_N.csv` format
piececraft-wraith already consumes for tiles, which THIS document now
also uses for composable materials at every granularity. All three -
chat-generated emojis, game tiles, and composable-object textures -
are one asset pipeline with three registries pointing into it, not
three separate generation systems. Building §10's generation model
once benefits all three consumers.

================================================================================
7. BRIDGING HARDCODED OPS AND DROP-IN FSM PIECES (the part you asked
   for direct insight on - genuinely the least-settled section here)
================================================================================
### 7.0 The layering that already exists, and why it isn't actually in
     tension
Two things are already true, independently, in this codebase family:
  1. Ops (`bot::navigate`, `craft`, `move_entity`, etc) are compiled C
     binaries - fixed signature, invoked by name with positional args
     (`CALL_OP "name" "arg1" "arg2"`, op-ed's own format). Stable,
     fast, auditable, boring on purpose.
  2. `.pal`/`.asm` scripts are PLAIN TEXT DATA, interpreted at runtime
     by `prisc+x` - no compilation step, drop a new file in and it
     runs next tick. `fsm_bot_programmer.txt`'s whole FSM design (piece.
     pdl + state.txt + fsm/states.txt + fsm/transitions.txt +
     events/on_tick.pal) is ALREADY this shape, and op-ed's editor
     ALREADY generates these files programmatically from a UI picker,
     not hand-typed syntax (GRAND-ARCHITECTURE.md §1's `save_script()`,
     confirmed real).
So "drop in FSM pieces without recompiling" isn't a gap to close -
it's the existing, working shape of a `.pal` file, and mutaclsym's own
doc already names the exact next real instance of it (`tick_monsters.c`
-> a per-monster-type FSM script, §0 above). GRAND-ARCHITECTURE.md §1
already states the resolution plainly for the "plugin system" question:
"the ops themselves already ARE the plugin system" - ops are the
stable verb vocabulary, PAL/pieces are the moddable composition layer
on top. This is not a compromise, it's the correct, deliberate split
every scripting-language-over-a-compiled-core system uses (Lua over a
game engine, etc) - name it as confirmed-correct, not as tension to
resolve.

### 7.1 Where "build/train/bugfix themselves" actually lives - the
     4-tier chassis, restated as a self-improvement ladder
Wrai-dust-fable §12's `decision_mode` tiers, read as increasing degrees
of self-modification, cheapest/safest first:
  - **Tier 1 (`preset`)** - no self-modification. Fixed script.
  - **Tier 2 (`weighted`)** - self-TUNES: option weights nudge up/down
    based on logged `(state,action,outcome)` correlation (§12's own
    spec - "run a batch, log which choice correlated with a better
    outcome, nudge that option's weight up"). This is genuinely a form
    of "trains itself," fully specified already, zero new architecture
    needed - the mildest, safest rung of the ladder.
  - **Tier 3 (`rl`)** - same data, formalized into a learned policy.
    The training data (`state.txt` snapshots + bot-op pass/fail
    results) is ALREADY produced for free by every FSM tick per §12 -
    "no bespoke telemetry needed."
  - **Tier 4 (`llm`)** - the model gets `state.txt` + a personality
    piece, and per §12's OWN explicit safety framing: **"the model's
    only output is one of the FSM's already-enumerated option names -
    it never gets direct write access to state."** This bound is
    already designed, not something this document is adding.
**The genuinely new piece this document adds**: extend Tier 4 from
"choose among enumerated options" to "author a NEW `.pal` script
sequence," under the SAME bound, generalized: the model may only emit
calls to ops that ALREADY EXIST (never invent a call to an op that
isn't in the registry), mirroring exactly the constraint op-ed's own
human-facing editor already enforces via its `available_ops[]` picker
(GRAND-ARCHITECTURE.md §1-2) - an LLM authoring a script through that
same constrained picker/vocabulary is not a new safety mechanism, it's
the same one already built for humans, applied to a model instead.

### 7.2 "Bugfix themselves," concretely
Two levels, both grounded in already-existing machinery, deliberately
NOT extended to a third, riskier level:
  - **Weight-level self-fix (safe, buildable now):** Tier 2/3 already
    IS this - a branch that correlates with repeated `bot::record_fail`
    gets its weight nudged down automatically. No new mechanism.
  - **Script-level self-fix (buildable, needs one new safety step):**
    a Tier 4 LLM, given a `(state,action,outcome)` history showing a
    bad pattern, proposes an EDITED `.pal` script (bounded to existing
    ops per §7.1). Critically: this should land as a NEW CANDIDATE
    script, never an in-place overwrite of the live one, and get
    validated via a DETERMINISTIC, SEEDED REPLAY before promotion -
    reusing the EXACT mechanism `fsm_bot_programmer.txt` already
    commits to for a completely different reason (testing bots and AI
    bots sharing one chassis specifically so a scenario can be re-run
    exactly). Self-bugfixing is testable using machinery this whole
    architecture already has, not a new safety system to invent.
  - **What this does NOT extend to, named plainly as a permanent
    boundary, not a near-term gap:** none of tiers 1-4 as designed can
    fix a genuinely MISSING op (a primitive verb the existing
    vocabulary has no equivalent for) - that requires new compiled C,
    which stays a human-gated (or human-reviewed-LLM-drafted-PR)
    process, not runtime self-authorship. Conflating "the FSM can edit
    its own script" with "the system can rewrite its own compiled ops"
    would be a real overclaim - this document explicitly does not
    propose the latter, matching wrai-dust-fable's own "toy universe,
    say so proudly" honesty discipline (§5/§10 there).

### 7.3 Promotion/demotion between the two layers - config/RL-driven,
     directly answering "the game could later even replace hardcoding
     with fsm/ or vice versa depending on the needs of game/config/rl/
     user feedback"
This isn't speculative invention - mutaclsym's own architecture doc
ALREADY names one direction of exactly this move as pending work:
"`tick_monsters.c`'s hardcoded chase-then-attack logic is the... C-
heuristic precursor... Upgrading specific monster types to real FSM
scripts (so monster behavior can vary by type, or later be user/level-
editor-authored) is still open." That IS a demotion (op -> FSM), named
in this codebase's own words, for exactly the reason you're describing
(wanting per-type variation/moddability the hardcoded version can't
give).
  - **DEMOTE (op -> FSM):** re-express a hardcoded op's internal
    decision logic as a per-piece-type `.pal` FSM script over the SAME
    underlying primitive ops (`bot::navigate`/`bot::interact` etc are
    presumably already what `tick_monsters.c` calls internally in C -
    demotion just moves the SEQUENCING out into data). Trigger
    conditions: a level editor wants per-instance override, or RL/
    user-feedback shows the fixed C behavior needs tuning that
    shouldn't require a recompile per iteration - i.e. exactly "needs
    of game/config/rl/user feedback."
  - **PROMOTE (FSM -> op):** the reverse - once a Tier 2/3 script's
    weights have STABILIZED (a measurable signal: weight deltas below
    a threshold across N recent outcome tuples - the SAME Tier 3 data
    already logged, no new instrumentation needed), a human (or an
    LLM-drafted, human-reviewed PR, mirroring §7.1's bound) hand-
    compiles that now-fixed sequence into a dedicated op for
    performance/robustness. This is deliberately NOT automatic
    runtime code-generation - promotion is an occasional, gated event,
    matching every other "prove it as data, harden into code once
    proven" split already used throughout this whole family of
    documents (ROADMAP-models.txt §12.2a/b, §13.3a/b, §13.4 v1/v2).
  - **The parallel worth naming**: this is wrai-dust-fable §14's own
    "fractal zoom applies to AI compute, not just pixels" idea, one
    level further - a compiled op is the fully-zoomed-out, cached,
    cheap answer (like a coarse tile only needing cached color); an
    FSM script is a zoomed-in, flexible, more expensive answer.
    Promotion/demotion is choosing, per behavior, per moment, which
    zoom level it should live at - decided by config/RL/feedback, not
    fixed at build time. The mechanism for BOTH directions already
    exists in pieces across this codebase family; what's new here is
    naming that they're the same move done in opposite directions, and
    that the decision of which way to go should be config/data-driven,
    not hardcoded per-behavior forever.

================================================================================
8. BUILD ORDER (vertical slice first, same discipline as every other
   roadmap in this family)
================================================================================
  1. Add `granularity`/`lambda`/`voxel_source`/`unicode` fields to
     mutaclsym's EXISTING `items.txt`/`furniture_types.txt` rows
     (don't build a new composable registry from scratch first) - prove
     "drop in, implies rendering" end to end through the ALREADY-WORKING
     `wraith_rgb_daemon.c` path with real, already-extant items before
     generalizing to atoms/cells/organisms.
  2. Add `bond_profile` + the §11 bonding table to ONE pair of existing
     items - prove reactivity works before scaling to a real material
     hierarchy.
  3. Build ONE `composed_of` chain 2-3 levels deep (e.g. plank ->
     wall -> house), reusing `craft.c`'s existing consume/produce logic
     unmodified - prove recursive composition works before attempting
     atom-to-organism depth.
  4. Attach `decision_mode=preset` (Tier 1 only) to ONE organism-tier
     composable, per mutaclsym §4's own next-real-step
     (`tick_monsters.c` -> FSM) - this is the actual, already-flagged
     item to build, not a new invention.
  5. Tier 2/3 self-tuning on that one organism - zero new architecture,
     per §7.1.
  6. Tier 4 LLM-bounded-choice, then LLM-authored-script-within-existing-
     ops (§7.1's extension), validated via seeded replay (§7.2) before
     ANY promotion to a live default.
  7. First real promotion/demotion decision (§7.3) - once step 6 has
     run long enough to produce a stabilized or clearly-struggling
     script, make ONE real promote-or-demote call, by hand, to prove
     the mechanism before treating it as routine.
  8. Growth/shrink policy (§5) - deferred until there's an actual
     composable population large enough for pruning to matter, matching
     ROADMAP-models.txt §13's own "prove it's needed before building
     the harder version" pattern.

================================================================================
9. WHAT'S EXPLICITLY NOT SOLVED HERE
================================================================================
- Environment-scale (planetary/space) composables (§3's last worked-
  example tier) - named, not designed.
- Any real code for §7.3's promotion mechanism (weight-stability
  detection, the hand-compile/PR step itself) - named as a real,
  buildable idea, not built.
- How `Mar$.$treetRace` actually attaches at the "city" composable tier
  mechanically (file formats, which piece owns the boundary) - wrai-
  dust-fable names the fusion at a conceptual level (§3), this document
  doesn't add mechanical detail beyond citing that diagram.
- This document deliberately resolves mutaclsym §7's "registry
  granularity" open question for the COMPOSABLE-MATERIAL case
  specifically (one piece-directory per composable, §2) - it does NOT
  claim to resolve whether mutaclsym's existing items/monsters/terrain
  registries should ALSO migrate off flat files; that's mutaclsym's own
  call to make when/if those files actually become unwieldy, per its
  own doc's existing "revisit if a flat file gets unwieldy" stance.
