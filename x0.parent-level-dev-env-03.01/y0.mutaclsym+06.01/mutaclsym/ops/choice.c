/* choice - one verb, one binary, no shared headers.
 * The number-key dispatcher: prisc+x's pal scripts only have `beq`
 * (exact equality), no range comparison, so digit/Enter handling has to
 * live in a C op, not pal/main_loop.pal itself - see nav-refactor-2.txt
 * §3 for why. Runs UNCONDITIONALLY every tick, alongside move_player
 * (self-filters like it does), because it now owns persistent
 * accumulator state that must be reset on ANY key it doesn't recognize
 * as a digit or Enter, not just ignored.
 *
 * Ported faithfully from real 1.TPMOS's chtmp_parser.c (the actual live
 * renderer behind its numbered-panel menus - confirmed via its compiled
 * +x binary; the sibling chtmp_player.c has no equivalent digit
 * handling and no +x artifact, i.e. dead code): each digit keystroke
 * accumulates into a persisted digit_accum field (persisted in
 * hero/state.txt since every keypress here is a fresh short-lived
 * process, unlike chtmp_parser.c's one long-lived process with the
 * accumulator in memory), bounds-checked on every keystroke. A digit
 * only PREVIEWS a selection - it does NOT execute anything. Only Enter
 * commits. Matches real 1.TPMOS's do_jump()/process_key() split exactly
 * (digit keys only ever move focus_index; activation is Enter-only).
 *
 * Two independent accumulator "layers" share this one op, matching
 * real CDDA's own convention of a menu drawn ON TOP of the still-
 * visible map (researched via gl_desktop.c's project_mirror window
 * type - draws the map, then draws a menu list directly over it in the
 * same pass, no screen swap) rather than a full-screen mode switch:
 *
 *   - The outer action bar (action_cursor/digit_accum, valid range
 *     [2, total_methods) - piece.pdl rows 0/1 = move/end_turn are
 *     reserved, matching real 1.TPMOS's fuzz-op_manager.c route_input()
 *     convention of starting at 2) - active whenever hero/state.txt's
 *     active_panel field is "none".
 *   - An overlay PANEL (active_panel/panel_cursor/panel_digit_accum),
 *     active once active_panel != "none". Two panel types exist:
 *     "craft" (opened when Enter commits the outer bar's craft choice
 *     instead of executing it directly - lets the player pick WHICH
 *     satisfiable recipe to make instead of always getting the first
 *     satisfiable one; committing execs craft.+x with the chosen
 *     recipe_id) and "inventory" (opened the same way from the outer
 *     bar's examine choice - a read-only browse of hero/inventory/,
 *     Enter never execs anything on any row, only closes the panel).
 *     compose_frame.c reads active_panel the same way and draws each
 *     panel's own bracket-numbered list over a sub-rectangle of the
 *     already-rendered map grid. Panel list numbering is 1-based (not
 *     the piece.pdl-index-based 2-based scheme the outer bar uses)
 *     with a trailing "Cancel" row at item_count+1, closing the panel
 *     without acting - same "Back always gets the next free slot"
 *     convention already used in egg-pals. While a panel is open,
 *     digits/Enter/arrows/Escape all do something (see below); any
 *     OTHER key is a pure no-op that does not close the panel - closing
 *     requires an explicit Cancel selection or Escape, unlike the outer
 *     bar's "any other key abandons the sequence" rule, since an
 *     accidental keypress silently dropping an open menu would be bad
 *     UX. move_player.c independently suspends movement while
 *     active_panel != "none", so wasd genuinely does nothing while a
 *     panel is open, not just "doesn't reach here."
 *
 * "Interact mode" (interact_mode field, 0/1) - the REAL xlector active-
 * target pattern from real 1.TPMOS (`projects/fuzz-op/manager/
 * fuzz-op_manager.c`, read in full, not excerpted - see dox/
 * 04-chtpm-parser-research-and-interact-mode.txt for the complete
 * research writeup), adapted for mutaclsym's own shape (a single game,
 * not a multi-project desktop host; monsters/items have no piece.pdl
 * METHOD table of their own yet, so there's nothing to redirect further
 * VERBS to - v1 scope is a real EXAMINE, not full verb-redirection).
 *
 * CORRECTION (2026-07-17, direct user instruction after this file's own
 * EARLIER claim here turned out wrong on a full, not excerpted, re-read
 * of fuzz-op_manager.c): the previous version of this comment said the
 * xlector cursor feature "SUPERSEDES an earlier, narrower build...
 * (arrows moving the ACTION BAR's own cursor) - that wasn't the real
 * feature." That was WRONG. Real fuzz-op_manager.c's own route_input()
 * has ZERO menu-cursor/accumulator logic of its own at all - arrow-key
 * menu navigation (moving a "[>]" cursor among numbered options) is
 * ENTIRELY chtpm_parser.c's own generic top-level nav (focus_index,
 * confirmed via direct read), applied to `${piece_methods}` - a real,
 * dynamically-generated button list from the active piece's own
 * piece.pdl METHOD table (see `load_dynamic_methods()` in shared-ops/
 * chtpm_parser_pal.c, already present, unmodified, just never wired
 * into this project's own game.chtpm until now). Once Enter commits a
 * focused numbered button, chtpm_parser.c relays the digit via
 * `onClick="KEY:n"` -> `inject_raw_key()` -> the SAME `<interact src>`
 * file this project's own module already reads - fuzz-op_manager.c
 * then receives that relayed digit and dispatches it exactly like any
 * other keypress. The xlector active-target/cursor feature above is a
 * REAL, SEPARATE, ADDITIONAL mechanic (an examine-cursor you walk
 * around the MAP) - it does NOT replace or conflict with arrow-based
 * menu nav, because that nav was never this file's job to begin with;
 * it happens one layer up, at the CHTPM level. Fixed by adding real
 * `<button onClick="KEY:n">` entries to `pieces/chtpm/layouts/
 * game.chtpm` for Pickup(2)/Drop(3)/Eat(4)/Craft(5)/Examine(6)/
 * Save(7), matching this file's own existing digit numbering exactly -
 * verified live: arrow-down visibly moves "[>]" between them, and
 * Enter on a focused one correctly sets `action_cursor` here via the
 * normal relay path, with the embedded in-game footer (built by this
 * project's own compose_frame.c) staying in sync with the chtpm-level
 * cursor. zoo_0000 already had the right shape for this on its own
 * side (`ops/build_footer()` already calls `pdl_reader.+x list_methods`
 * dynamically, unlike mutaclsym's own hardcoded digit dispatch below) -
 * its own `zoo.chtpm` got the same kind of static buttons for now
 * (Feed/Pet/Play/Export), with real `${piece_methods}`-driven dynamic
 * buttons (matching fuzz-op.chtpm's own exact usage) as a real,
 * separate, not-yet-done follow-up, not silently assumed equivalent.
 *
 * 'i'/'I' toggles interact_mode outside a panel (meaningless inside one
 * - a panel is already its own menu-mode context), resetting the
 * xlector cursor's position (hero/state.txt's xlector_pos_x/
 * xlector_pos_y) to the hero's current position every time it's
 * entered. While interact_mode=1: wasd/arrows move the CURSOR, not the
 * hero - that's ops/move_player.c's own responsibility (this file does
 * NOT handle arrows while interact_mode=1; see that file's own header
 * comment), not this one's. Enter examines whatever's at the cursor's
 * current position (examine_at(), below) - the real v1 "select."
 * Digits are a pure no-op in this mode - there's no action-bar menu to
 * jump around while controlling the cursor. Escape (27) exits
 * interact_mode back to plain hero movement (or closes an open panel
 * first, if one happens to be open) - matches real chtpm_parser.c's own
 * confirmed ESC_KEY=27 "always wins, checked first" convention.
 * ARROW_UP/DOWN inside a panel still move its own cursor (wraparound,
 * unaffected by any of this - a panel is its own menu-mode context,
 * independent of interact_mode's value, same as it always was).
 *
 * 'e'/'E' toggles emoji_mode - the real ASCII<->emoji display toggle
 * from op-ed/fuzz-op (`op-ed_manager.c:1079`, `fuzz-op_manager.c:
 * 693-705` - a single boolean flag flipped by one key, read in full,
 * not excerpted - see 2.muchi-verse/GRAND-ARCHITECTURE.md §0a for the
 * complete research writeup, including the mistake to avoid repeating:
 * op-ed/fuzz-op's own two hardcoded glyph->emoji tables actively
 * disagree with each other. mutaclsym's version is registry-driven
 * instead (a `unicode=` field on each of the four content registries -
 * see ops/compose_frame.c's own header comment), the doc's explicitly
 * recommended fix for that exact drift. Checked FIRST, unconditionally
 * - before panel handling, before interact_mode - since it's a pure
 * display preference, orthogonal to whatever menu/cursor state is
 * currently active (matches how the real toggle works globally in
 * both reference projects, never scoped to a submode there either).
 * Does not touch action_cursor/digit_accum/panel state at all.
 *
 * Quit stays the separate, fixed, non-numbered 'q' key in
 * pal/main_loop.pal - confirmed real 1.TPMOS keeps Quit as a literal
 * 'q'/'Q' check outside this numbered/accumulator dispatch entirely
 * (only a genuine "Back to X" row is a real navigable numbered item).
 * Note 'q' never actually reaches this op at all: pal/main_loop.pal's
 * beq x2,x9,quit runs BEFORE `choice x2` and jumps straight to halt, so
 * any pending state here simply goes stale, harmless since the process
 * is halting anyway (this means 'q' also quits the whole game even with
 * a panel open - there is no dedicated "close panel" key by design,
 * only the panel's own Cancel row).
 *
 * Usage: choice.+x <keycode> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_PANEL_ITEMS 32

/* Same ARROW_* sentinel values keyboard_input.c/gl_mirror.c/move_player.c
 * already use everywhere else in this project. */
#define ARROW_UP    1002
#define ARROW_DOWN  1003

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

/* Real, native chtpm_parser_pal.c signal (export_active_index(),
 * confirmed via direct read) - the interactive_idx of whichever
 * top-level element is currently active/focused. Used ONLY to tell
 * apart a digit relayed from INSIDE the chtpm-native "craft"/"examine"
 * ACTIVATE submenus (see game.chtpm) from a digit relayed by the outer
 * flat ${piece_methods} bar - both share the same single-ASCII-digit
 * relay space (mutaclsym's prisc+x has no string-command capability,
 * see compose_frame.c's own write_panel_gui_state() comment), so this
 * op needs a real way to know which context a bare digit came from.
 * NAMED, ACCEPTED FRAGILITY: 7/8 are hardcoded to game.chtpm's own
 * current element order (Control Hero, 5 flat methods, craft, examine)
 * - confirmed empirically via a live isolated test reading this exact
 * file while entering each submenu, not assumed. If game.chtpm's
 * layout order ever changes, these two constants must be re-verified
 * the same way. */
#define CHTPM_CRAFT_GUI_IDX 7
#define CHTPM_EXAMINE_GUI_IDX 8

static int read_active_gui_index(void) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/display/active_gui_index.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* REAL BUG, LIVE-CAUGHT (via choice.+x's own key=6/toggle_emoji dispatch
 * silently no-op'ing, digit_accum/action_cursor tracing showed the
 * outer numbered-action branch was never even reached): chtpm_parser_
 * pal.c's own export_active_index() ALREADY documents (see that
 * function's own comment) that active_gui_index.txt alone conflates
 * "just focused" with "genuinely active/typing" - it always contains
 * SOME element's index (falling back to whatever is merely focus_index
 * when nothing is truly active), never -1/empty for "nothing engaged".
 * The CHTPM_CRAFT_GUI_IDX/CHTPM_EXAMINE_GUI_IDX check below used to
 * treat "craft/examine happens to be focused right now" the same as
 * "the craft/examine panel is genuinely open" - meaning simply having
 * NAVIGATED PAST the craft/examine button (never opening it) left a
 * stale index in this file that then silently swallowed EVERY later
 * digit press on the real outer action bar, confirmed live (a value of
 * 7 left over from earlier browsing was still there turns later,
 * intercepting pickup/toggle_emoji/etc with no error, no message,
 * nothing). export_active_index() already writes the real, correct
 * disambiguating signal for exactly this reason
 * (active_gui_is_typing.txt, "1" only when active_index != -1, a
 * genuinely engaged ACTIVATE container or cli_io field) - this was
 * simply never read here. */
static int read_active_gui_is_typing(void) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/display/active_gui_is_typing.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1) v = 0;
    fclose(f);
    return v;
}

static void read_kv_str_local(const char *path, const char *key, char *out, size_t out_sz, const char *def) {
    snprintf(out, out_sz, "%s", def);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, key) == 0) { snprintf(out, out_sz, "%s", eq + 1); break; }
    }
    fclose(f);
}

/* Reads hero's own piece.pdl method NAMES into a small in-memory list
 * (not just a single lookup) since this op needs both the total count
 * (for bounds-checking the accumulator) and, on commit, the name at a
 * specific index - one popen covers both instead of two like an
 * earlier version needed.
 *
 * FILTERED to match chtpm_parser_pal.c's own load_dynamic_methods()
 * exactly (same reserved-key list, same leading-underscore skip rule -
 * confirmed via direct read of that function) - REAL BUG FOUND LIVE:
 * pdl_reader.+x's own `list_methods` is deliberately raw/unfiltered
 * (a shared tool other consumers rely on staying that way), so before
 * this fix, this op's own digit numbering (used by BOTH "run" mode's
 * direct terminal play and this op's own dispatch when chtpm relays a
 * digit) drifted out of sync with chtpm's rendered ${piece_methods}
 * numbering the moment _craft/_examine were hidden from chtpm's list -
 * chtpm's own "6. [toggle_emoji]" button relayed a raw '6' that this
 * op, still using raw piece.pdl order, resolved to "_examine" instead.
 * Filtering here keeps both numbering schemes identical by
 * construction, not by coincidence, for any future piece.pdl change
 * too. Written starting at index 2 (indices 0/1 left blank) so the
 * existing `d >= 2 && d < total` bounds-check at every call site needs
 * no changes. */
static int load_method_names(char names[][MAX_LINE], int max_names) {
    char cmd[PATH_BUF];
    snprintf(cmd, sizeof(cmd), "'%s/ops/+x/pdl_reader.+x' hero list_methods", project_root);
    FILE *pf = popen(cmd, "r");
    if (!pf) return 0;
    int n = 2;
    char line[MAX_LINE];
    while (n < max_names && fgets(line, sizeof(line), pf)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "move") == 0 || strcmp(line, "select") == 0 ||
            strcmp(line, "interact") == 0 || strcmp(line, "stat_decay") == 0 ||
            strcmp(line, "on_turn_end") == 0 || line[0] == '_') {
            continue;
        }
        snprintf(names[n], MAX_LINE, "%s", line);
        n++;
    }
    pclose(pf);
    return n;
}

/* Plain top-to-bottom scan of recipes.txt, ids only - deliberately the
 * same simple, unfiltered order compose_frame.c's own panel renderer
 * uses, so the position a recipe is drawn at and the position this op
 * resolves a digit to can never drift (the exact class of bug real
 * 1.TPMOS's fuzz-op subsystem had between its renderer and its
 * dispatcher's independently-written skip-lists). */
static int load_recipe_ids(char ids[][MAX_LINE], int max_ids) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/recipes/recipes.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    char line[MAX_LINE];
    while (n < max_ids && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *bar = strchr(line, '|');
        if (!bar) continue;
        snprintf(ids[n], MAX_LINE, "%.*s", (int)(bar - line), line);
        n++;
    }
    fclose(f);
    return n;
}

/* Directory-order scan of hero/inventory/ - deliberately the same
 * order compose_frame.c's inventory panel renderer uses (readdir()
 * order is stable within a single process run against an unchanged
 * directory), same anti-drift reasoning as load_recipe_ids(). Only
 * needs item_ids since the inventory panel never execs anything on
 * commit - see this file's header comment. */
static int load_inventory_ids(char ids[][MAX_LINE], int max_ids) {
    char inventory_dir[PATH_BUF];
    snprintf(inventory_dir, sizeof(inventory_dir), "%s/pieces/world_01/map_start/hero/inventory", project_root);
    DIR *d = opendir(inventory_dir);
    if (!d) return 0;
    struct dirent *entry;
    int n = 0;
    while (n < max_ids && (entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char state_path[PATH_BUF + 384];
        snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", inventory_dir, entry->d_name);
        FILE *f = fopen(state_path, "r");
        if (!f) continue;
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\n")] = '\0';
            if (strncmp(line, "item_id=", 8) == 0) { snprintf(ids[n], MAX_LINE, "%s", line + 8); break; }
        }
        fclose(f);
        n++;
    }
    closedir(d);
    return n;
}

/* Single-field registry name lookups, matching the same pipe-delimited
 * shape compose_frame.c's/compose_rgb_frame.c's own item_registry_field()/
 * monster_registry_field() already read - duplicated narrowly here (just
 * the name column, field index 2) rather than shared, per this project's
 * own "no shared headers" convention. */
static void item_name(const char *item_id, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", item_id);
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/items/items.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(item_id) || strncmp(line, item_id, p1 - line) != 0) continue;
        char *name = p1 + 1;
        char *end = strchr(name, '|');
        if (end) *end = '\0';
        snprintf(out, out_sz, "%s", name);
        break;
    }
    fclose(f);
}

static void monster_name(const char *monster_type, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", monster_type);
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/monsters/monster_types.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(monster_type) || strncmp(line, monster_type, p1 - line) != 0) continue;
        char *name = p1 + 1;
        char *end = strchr(name, '|');
        if (end) *end = '\0';
        snprintf(out, out_sz, "%s", name);
        break;
    }
    fclose(f);
}

/* The real v1 scope of xlector "select" (see this file's own header
 * comment and dox/04-chtpm-parser-research-and-interact-mode.txt): an
 * EXAMINE, not full verb-redirection - monsters/items have no piece.pdl
 * METHOD table of their own yet, so there's nothing to hand further
 * verbs to. Scans the given map's items/ and monsters/ directories for
 * anything at (x,y), logs one human-readable line to message_log.txt -
 * "You see nothing here." if empty, matching real CDDA's own examine
 * convention for an empty tile. */
/* Generic piece.pdl STATE-field int lookup - matches the pipe-
 * delimited "SECTION | KEY | VALUE" shape every piece.pdl in this
 * family already uses. Real, direct request: "set in .pdl per game
 * what entities can or can't be possessed" - `possessable` is a real
 * STATE field a piece.pdl author declares, not a hardcoded name-
 * substring exclusion list (real fuzz-op_manager.c's own "Can't select
 * zombies" check, confirmed via direct read, hardcodes "zombie" as a
 * substring match - a real, working reference, but a worse fit for
 * this project's own piece.pdl-driven philosophy already established
 * this session for METHOD tables). Defaults to possessable (1) if the
 * field is absent, matching "opt out, not opt in" - a piece with no
 * opinion is assumed selectable, same spirit as fuzz-op's own default
 * (only a NAMED exclusion, zombies, is blocked; everything else is
 * fair game). */
static int piece_pdl_state_int(const char *piece_pdl_path, const char *key, int def) {
    FILE *f = fopen(piece_pdl_path, "r");
    if (!f) return def;
    char line[MAX_LINE];
    int val = def;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        char field[64];
        int len = (int)(p2 - (p1 + 1));
        if (len < 0) continue;
        if (len >= (int)sizeof(field)) len = sizeof(field) - 1;
        memcpy(field, p1 + 1, len);
        field[len] = '\0';
        char *k = field;
        while (*k == ' ') k++;
        char *kend = k + strlen(k);
        while (kend > k && *(kend - 1) == ' ') { *(kend - 1) = '\0'; kend--; }
        if (strcmp(k, key) == 0) {
            val = atoi(p2 + 1);
            break;
        }
    }
    fclose(f);
    return val;
}

/* Real xlector "possession", per direct instruction (see this file's
 * own header comment on the full research against fuzz-op_manager.c,
 * lines 559-627, read in full): while piloting the xlector cursor
 * (interact_mode=1), Enter on a POSSESSABLE piece's own tile transfers
 * control to it - real fuzz-op does this GENERICALLY for any piece
 * (redirects a global active_target_id, movement/dispatch become
 * fully piece_id-parameterized). Mutaclsym has exactly ONE real
 * player-controllable piece today (hero - monsters are pure AI, same
 * exclusion fuzz-op itself applies), so this is the REAL, right-sized
 * v1: hero is the one real possessable target, and "possessing" it
 * from the cursor is functionally identical to exiting interact_mode
 * (which 'i'/Escape already do) - but now gated on a REAL, checked
 * piece.pdl flag instead of an assumption, so the exact same check
 * already correctly refuses to "possess" a monster standing on the
 * same tile. Full generic multi-entity possession (movement/dispatch
 * genericized to take a piece_id, a real standalone xlector piece
 * with its own directory - matching fuzz-op's own shape exactly) is a
 * real, named, deliberately deferred future step for whenever a
 * second player-controllable piece (a companion/pet) actually exists -
 * see pal-standards.txt's own new section for the full writeup, not
 * silently built halfway here. */
static int try_possess_at(int hero_x, int hero_y, int x, int y) {
    if (x != hero_x || y != hero_y) return 0;
    char pdl_path[PATH_BUF];
    snprintf(pdl_path, sizeof(pdl_path), "%s/pieces/world_01/map_start/hero/piece.pdl", project_root);
    if (!piece_pdl_state_int(pdl_path, "possessable", 1)) return 0;

    char log_path[PATH_BUF];
    snprintf(log_path, sizeof(log_path), "%s/pieces/display/message_log.txt", project_root);
    FILE *lf = fopen(log_path, "a");
    if (lf) { fprintf(lf, "You take control of hero.\n"); fclose(lf); }
    return 1;
}

static void examine_at(const char *map_id, int x, int y) {
    char items_dir[PATH_BUF + 32], monsters_dir[PATH_BUF + 32];
    snprintf(items_dir, sizeof(items_dir), "%s/pieces/world_01/%s/items", project_root, map_id);
    snprintf(monsters_dir, sizeof(monsters_dir), "%s/pieces/world_01/%s/monsters", project_root, map_id);

    char msg[160] = "";

    DIR *d = opendir(items_dir);
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char state_path[PATH_BUF + 384];
            snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", items_dir, entry->d_name);
            FILE *sf = fopen(state_path, "r");
            if (!sf) continue;
            char line[MAX_LINE], item_id[64] = "?";
            int ix = -1, iy = -1;
            while (fgets(line, sizeof(line), sf)) {
                line[strcspn(line, "\n")] = '\0';
                /* gcc can't prove line+8 fits in item_id's 64 bytes from
                 * static sizes alone - same class of warning narrowly
                 * suppressed elsewhere in this project (tick_monsters.c,
                 * prisc+x.c) rather than widened indefinitely. */
                if (strncmp(line, "item_id=", 8) == 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                    snprintf(item_id, sizeof(item_id), "%s", line + 8);
#pragma GCC diagnostic pop
                }
                else if (strncmp(line, "pos_x=", 6) == 0) ix = atoi(line + 6);
                else if (strncmp(line, "pos_y=", 6) == 0) iy = atoi(line + 6);
            }
            fclose(sf);
            if (ix == x && iy == y) {
                char name[64];
                item_name(item_id, name, sizeof(name));
                snprintf(msg, sizeof(msg), "You see a %s here.", name);
                break;
            }
        }
        closedir(d);
    }

    if (!msg[0]) {
        d = opendir(monsters_dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                char state_path[PATH_BUF + 384];
                snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", monsters_dir, entry->d_name);
                FILE *sf = fopen(state_path, "r");
                if (!sf) continue;
                char line[MAX_LINE], monster_type[64] = "?";
                int mx = -1, my = -1, hp = 0;
                while (fgets(line, sizeof(line), sf)) {
                    line[strcspn(line, "\n")] = '\0';
                    if (strncmp(line, "monster_type=", 13) == 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                        snprintf(monster_type, sizeof(monster_type), "%s", line + 13);
#pragma GCC diagnostic pop
                    }
                    else if (strncmp(line, "pos_x=", 6) == 0) mx = atoi(line + 6);
                    else if (strncmp(line, "pos_y=", 6) == 0) my = atoi(line + 6);
                    else if (strncmp(line, "hp=", 3) == 0) hp = atoi(line + 3);
                }
                fclose(sf);
                if (mx == x && my == y) {
                    char name[64];
                    monster_name(monster_type, name, sizeof(name));
                    snprintf(msg, sizeof(msg), "You see a %s (hp %d) here.", name, hp);
                    break;
                }
            }
            closedir(d);
        }
    }

    if (!msg[0]) snprintf(msg, sizeof(msg), "You see nothing here.");

    char log_path[PATH_BUF];
    snprintf(log_path, sizeof(log_path), "%s/pieces/display/message_log.txt", project_root);
    FILE *lf = fopen(log_path, "a");
    if (lf) { fprintf(lf, "%s\n", msg); fclose(lf); }
}

/* Registry field lookup matching eat.c's/move_player.c's own
 * established indexing convention exactly (1=name, 2=category,
 * 5=power) - duplicated narrowly here per this project's own "no
 * shared headers" rule. */
static void item_registry_field(const char *item_id, int field_index, char *out, size_t out_sz, const char *def) {
    snprintf(out, out_sz, "%s", def);
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/items/items.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\n")] = '\0';
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(item_id) || strncmp(line, item_id, p1 - line) != 0) continue;
        char *field = p1 + 1;
        for (int i = 1; i < field_index && field; i++) {
            field = strchr(field, '|');
            if (field) field++;
        }
        if (!field) break;
        char *end = strchr(field, '|');
        if (end) *end = '\0';
        snprintf(out, out_sz, "%s", field);
        break;
    }
    fclose(f);
}

/* Real, named simplification vs CDDA: CDDA's own thrown weapons land
 * on the ground at the target tile (recoverable) - this v1 just
 * consumes the item outright on throw (matches eat.c's own "consumed,
 * not moved anywhere" convention), whether it hits or not is still
 * gated on a REAL target existing in range first (no consuming an
 * item on a pure whiff at nothing). Full recoverable-throw + real
 * firearms/ammo are a real, separate, deliberately deferred future
 * step - see dox/ for the named follow-up, not silently skipped. */
#define THROW_RANGE 6
static void throw_at(const char *map_id, int hero_x, int hero_y, int target_x, int target_y) {
    char msg[160] = "";
    int dx = target_x - hero_x, dy = target_y - hero_y;
    int dist2 = dx * dx + dy * dy;

    char inventory_dir[PATH_BUF];
    snprintf(inventory_dir, sizeof(inventory_dir), "%s/pieces/world_01/map_start/hero/inventory", project_root);

    char weapon_state_path[PATH_BUF + 384] = "";
    char weapon_name[64] = "";
    int weapon_power = 0;
    DIR *d = opendir(inventory_dir);
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char state_path[PATH_BUF + 384];
            snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", inventory_dir, entry->d_name);
            char item_id[64] = "?";
            FILE *sf = fopen(state_path, "r");
            if (sf) {
                char line[MAX_LINE];
                while (fgets(line, sizeof(line), sf)) {
                    line[strcspn(line, "\n")] = '\0';
                    if (strncmp(line, "item_id=", 8) == 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                        snprintf(item_id, sizeof(item_id), "%s", line + 8);
#pragma GCC diagnostic pop
                        break;
                    }
                }
                fclose(sf);
            }
            char category[16];
            item_registry_field(item_id, 2, category, sizeof(category), "?");
            if (strcmp(category, "weapon") != 0) continue;
            snprintf(weapon_state_path, sizeof(weapon_state_path), "%s", state_path);
            char power_str[16];
            item_registry_field(item_id, 5, power_str, sizeof(power_str), "0");
            weapon_power = atoi(power_str);
            item_registry_field(item_id, 1, weapon_name, sizeof(weapon_name), item_id);
            break;
        }
        closedir(d);
    }

    if (!weapon_state_path[0]) {
        snprintf(msg, sizeof(msg), "You have nothing to throw.");
    } else if (dist2 > THROW_RANGE * THROW_RANGE) {
        snprintf(msg, sizeof(msg), "That's too far to throw.");
    } else {
        char monsters_dir[PATH_BUF + 32];
        snprintf(monsters_dir, sizeof(monsters_dir), "%s/pieces/world_01/%s/monsters", project_root, map_id);
        DIR *md = opendir(monsters_dir);
        int found = 0;
        char monster_state_path[PATH_BUF + 384] = "";
        char monster_type[64] = "?";
        int monster_hp = 0;
        if (md) {
            struct dirent *entry;
            while ((entry = readdir(md)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                char state_path[PATH_BUF + 384];
                snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", monsters_dir, entry->d_name);
                FILE *sf = fopen(state_path, "r");
                if (!sf) continue;
                char line[MAX_LINE], mtype[64] = "?";
                int mx = -1, my = -1, hp = 0;
                while (fgets(line, sizeof(line), sf)) {
                    line[strcspn(line, "\n")] = '\0';
                    if (strncmp(line, "monster_type=", 13) == 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                        snprintf(mtype, sizeof(mtype), "%s", line + 13);
#pragma GCC diagnostic pop
                    }
                    else if (strncmp(line, "pos_x=", 6) == 0) mx = atoi(line + 6);
                    else if (strncmp(line, "pos_y=", 6) == 0) my = atoi(line + 6);
                    else if (strncmp(line, "hp=", 3) == 0) hp = atoi(line + 3);
                }
                fclose(sf);
                if (mx == target_x && my == target_y) {
                    snprintf(monster_state_path, sizeof(monster_state_path), "%s", state_path);
                    snprintf(monster_type, sizeof(monster_type), "%s", mtype);
                    monster_hp = hp;
                    found = 1;
                    break;
                }
            }
            closedir(md);
        }

        if (!found) {
            snprintf(msg, sizeof(msg), "There's nothing there to throw at.");
        } else {
            char mname[64];
            monster_name(monster_type, mname, sizeof(mname));
            monster_hp -= weapon_power;
            if (monster_hp <= 0) {
                char *dir_end = strrchr(monster_state_path, '/');
                char monster_dir[PATH_BUF + 384];
                if (dir_end) { size_t len = dir_end - monster_state_path; snprintf(monster_dir, sizeof(monster_dir), "%.*s", (int)len, monster_state_path); }
                else snprintf(monster_dir, sizeof(monster_dir), "%s", monster_state_path);
                remove(monster_state_path);
                rmdir(monster_dir);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(msg, sizeof(msg), "You throw the %s and kill the %s!", weapon_name, mname);
#pragma GCC diagnostic pop
            } else {
                FILE *mf = fopen(monster_state_path, "r");
                char mlines[16][MAX_LINE];
                int mnlines = 0;
                if (mf) { while (mnlines < 16 && fgets(mlines[mnlines], MAX_LINE, mf)) mnlines++; fclose(mf); }
                mf = fopen(monster_state_path, "w");
                if (mf) {
                    for (int i = 0; i < mnlines; i++) {
                        if (strncmp(mlines[i], "hp", 2) == 0 && mlines[i][2] == '=') { fprintf(mf, "hp=%d\n", monster_hp); continue; }
                        fputs(mlines[i], mf);
                    }
                    fclose(mf);
                }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(msg, sizeof(msg), "You throw the %s at the %s for %d.", weapon_name, mname, weapon_power);
#pragma GCC diagnostic pop
            }

            /* Consume the thrown weapon - see this function's own
             * header comment on the real, named CDDA difference. */
            char *idir_end = strrchr(weapon_state_path, '/');
            char weapon_dir[PATH_BUF + 384];
            if (idir_end) { size_t len = idir_end - weapon_state_path; snprintf(weapon_dir, sizeof(weapon_dir), "%.*s", (int)len, weapon_state_path); }
            else snprintf(weapon_dir, sizeof(weapon_dir), "%s", weapon_state_path);
            remove(weapon_state_path);
            rmdir(weapon_dir);
        }
    }

    char log_path2[PATH_BUF];
    snprintf(log_path2, sizeof(log_path2), "%s/pieces/display/message_log.txt", project_root);
    FILE *lf2 = fopen(log_path2, "a");
    if (lf2) { fprintf(lf2, "%s\n", msg); fclose(lf2); }
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int key = atoi(argv[1]);
    resolve_root();

    char hero_path[PATH_BUF];
    snprintf(hero_path, sizeof(hero_path), "%s/pieces/world_01/map_start/hero/state.txt", project_root);
    FILE *f = fopen(hero_path, "r");
    if (!f) return 1;

    char lines[32][MAX_LINE];
    int nlines = 0;
    int action_cursor = -1, digit_accum = 0, panel_cursor = 0, panel_digit_accum = 0;
    int interact_mode = 0;
    int emoji_mode = 1;
    int render_mode = 0; /* 0=2D, 1=3D - '0' toggles, GL-only (no-op visually in ASCII) */
    int camera_mode = 1; /* 1=1st person, 2=3rd person, 3=free camera */
    int hero_x = 0, hero_y = 0;
    int xlector_x = -1, xlector_y = -1; /* -1 = absent, filled from hero_x/y below */
    char map_id[64] = "map_start";
    char active_panel[32] = "none";
    while (nlines < 32 && fgets(lines[nlines], MAX_LINE, f)) {
        char *eq = strchr(lines[nlines], '=');
        if (eq) {
            *eq = '\0';
            if (strcmp(lines[nlines], "action_cursor") == 0) action_cursor = atoi(eq + 1);
            else if (strcmp(lines[nlines], "digit_accum") == 0) digit_accum = atoi(eq + 1);
            else if (strcmp(lines[nlines], "panel_cursor") == 0) panel_cursor = atoi(eq + 1);
            else if (strcmp(lines[nlines], "panel_digit_accum") == 0) panel_digit_accum = atoi(eq + 1);
            else if (strcmp(lines[nlines], "interact_mode") == 0) interact_mode = atoi(eq + 1);
            else if (strcmp(lines[nlines], "emoji_mode") == 0) emoji_mode = atoi(eq + 1);
            else if (strcmp(lines[nlines], "render_mode") == 0) render_mode = atoi(eq + 1);
            else if (strcmp(lines[nlines], "camera_mode") == 0) camera_mode = atoi(eq + 1);
            else if (strcmp(lines[nlines], "pos_x") == 0) hero_x = atoi(eq + 1);
            else if (strcmp(lines[nlines], "pos_y") == 0) hero_y = atoi(eq + 1);
            else if (strcmp(lines[nlines], "xlector_pos_x") == 0) xlector_x = atoi(eq + 1);
            else if (strcmp(lines[nlines], "xlector_pos_y") == 0) xlector_y = atoi(eq + 1);
            else if (strcmp(lines[nlines], "map_id") == 0) {
                /* Copy into a separate buffer before stripping the
                 * newline - `eq + 1` aliases directly into lines[nlines],
                 * so stripping in place would destroy that line's own
                 * trailing '\n' for the rest of this op's lifetime,
                 * corrupting the later passthrough fputs() write-back
                 * (map_id has no dedicated found/replace branch there,
                 * so it always goes through that path) by merging it
                 * with whatever field follows it in the file. Hit this
                 * for real: produced a `map_id=map_starthp=100` glued
                 * line. See active_panel's own matching fix and its
                 * comment on the self-heal this exact bug class already
                 * needed once before. */
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%s", eq + 1);
                tmp[strcspn(tmp, "\n")] = '\0';
                snprintf(map_id, sizeof(map_id), "%s", tmp);
            } else if (strcmp(lines[nlines], "active_panel") == 0) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%s", eq + 1);
                tmp[strcspn(tmp, "\n")] = '\0';
                snprintf(active_panel, sizeof(active_panel), "%s", tmp);
            }
            *eq = '=';
        }
        nlines++;
    }
    fclose(f);
    if (xlector_x < 0) xlector_x = hero_x;
    if (xlector_y < 0) xlector_y = hero_y;

    int is_digit = (key >= '0' && key <= '9');
    int is_enter = (key == 10 || key == 13);
    /* "Interact mode" (per real 1.TPMOS's is_map_control, researched and
     * adapted - see dox/04-chtpm-parser-research-and-interact-mode.txt
     * for the full writeup, not copied verbatim since mutaclsym is a
     * single game, not a multi-project desktop host). Lets wasd/arrows
     * drive the action-bar cursor instead of the hero, for input
     * sources with no digit keys (a joystick's d-pad, deferred to a
     * later pass - this groundwork is what that will build on). Digits
     * still directly jump the action bar regardless of this flag - it's
     * a pure addition, never required for keyboard play. */
    int is_up = (key == ARROW_UP);
    int is_down = (key == ARROW_DOWN);
    int is_interact_toggle = (key == 'i' || key == 'I');
    int is_emoji_toggle = (key == 'e' || key == 'E');
    /* Real, direct instruction (2026-07-17), grounded against real
     * piececraft-wraith (`wraith-alpha/wraith-projects/piececraft-
     * wraith/manager/piececraft-wraith_manager.c`'s own
     * update_scene_objects(), confirmed via direct read - NOT the
     * legacy gltpm-based `projects/piececraft-3d`, which is dead kruft
     * predating wraith): '0' toggles the 3D GL view on/off (a no-op in
     * ASCII terminal rendering - see ops/compose_frame.c, which never
     * reads render_mode at all). While render_mode==1, '1'/'2'/'3'
     * switch camera POV (1st person/3rd person/free camera) INSTEAD of
     * their normal outer-action-bar meaning (pickup/drop) - matches
     * real piececraft-wraith's own scene.objects.pdl button set
     * (btn_pov1/2/3 = KEY:49/50/51) exactly. This is a real,
     * mode-gated key reinterpretation, same shape as 't' meaning
     * "throw" only in interact_mode - not a new, separate input path. */
    int is_3d_toggle = (key == '0');
    int is_pov_key = (render_mode == 1 && (key == '1' || key == '2' || key == '3'));
    /* Universal escape, same real 1.TPMOS chtpm_parser.c convention
     * (ESC_KEY=27, confirmed via direct citation) - closes an open
     * panel without acting (same outcome as its own trailing Cancel
     * row, just reachable without digits), or exits interact_mode back
     * to movement if no panel is open. Already flows through both
     * keyboard_input.c and gl_mirror.c unmodified (confirmed by direct
     * code read - bare ESC returns raw byte 27 in both paths already). */
    int is_escape = (key == 27);
    char exec_handler[MAX_LINE] = "";
    char exec_arg[64] = "";

    /* REAL, LIVE BUG FOUND DURING TESTING: active_panel is a real
     * "run"-mode-only concept now (see the CHTPM_*_GUI_IDX branch
     * below and compose_frame.c's own write_panel_gui_state() comment
     * - chtpm mode's craft/examine ACTIVATE submenus never set it,
     * their native "Close"/onClick="BACK" never relays to this op at
     * all). A STALE value left over from an earlier "run"-mode session
     * (or old testing) sitting in hero/state.txt would otherwise
     * PERMANENTLY route every future digit through the OLD in_panel
     * branch below in chtpm mode too - confirmed live: a leftover
     * "active_panel=inventory" silently swallowed every single digit
     * dispatch (pickup, toggle_emoji, everything) in a real chtpm
     * session, forever, since nothing in chtpm mode can ever reset it
     * back to "none" again. Gated the same way compose_frame.c's own
     * overlay is (module_path - "run" mode boots main_loop.pal, chtpm
     * mode boots main_loop_chtpm.pal). */
    char module_path_check[PATH_BUF];
    {
        char pas_path[PATH_BUF];
        snprintf(pas_path, sizeof(pas_path), "%s/pieces/apps/player_app/state.txt", project_root);
        read_kv_str_local(pas_path, "module_path", module_path_check, sizeof(module_path_check), "");
    }
    int is_chtpm_mode = (strstr(module_path_check, "main_loop_chtpm") != NULL);
    int in_panel = !is_chtpm_mode && (strcmp(active_panel, "craft") == 0 || strcmp(active_panel, "inventory") == 0);
    if (!in_panel && strcmp(active_panel, "none") != 0) {
        /* Unrecognized value (not "none" and not a real panel type) -
         * self-heal back to "none" rather than leaving it stuck. Hit
         * this for real once: a corrupted "active_panel=none<garbage>"
         * line (two fields glued together with a missing newline from
         * some earlier partial write) meant move_player.c's own
         * strcmp(active_panel,"none") check was never true again,
         * permanently blocking movement while turns kept ticking in
         * the background - see move_player.c's matching defensive fix
         * for the full writeup. This op is the only writer of
         * active_panel, so healing it here is the actual fix, not just
         * a symptom workaround. */
        snprintf(active_panel, sizeof(active_panel), "none");
    }

    if (is_3d_toggle) {
        /* Same "pure display-preference toggle" shape as is_emoji_toggle
         * below - touches nothing else, so flipping the 3D view doesn't
         * cancel a pending digit sequence or panel state. */
        render_mode = !render_mode;
    } else if (is_pov_key) {
        /* Only reachable while render_mode==1 (see is_pov_key's own
         * definition above) - camera_mode is the only thing this
         * changes, matching is_3d_toggle's own "pure preference,
         * touches nothing else" shape. */
        camera_mode = key - '0';
    } else if (is_emoji_toggle) {
        /* Pure display-preference toggle, deliberately outside every
         * other branch below - see this file's own header comment.
         * Leaves action_cursor/digit_accum/panel state completely
         * untouched, unlike the catch-all "any other key abandons a
         * pending sequence" else branch further down - flipping how
         * glyphs are drawn shouldn't also silently cancel whatever the
         * player was in the middle of selecting. */
        emoji_mode = !emoji_mode;
    } else if (in_panel) {
        /* Panel mode - "craft" (recipe picker, commits by exec'ing
         * craft.+x with the chosen recipe_id) or "inventory" (examine -
         * read-only browse, Enter never execs anything, just closes). */
        char panel_ids[MAX_PANEL_ITEMS][MAX_LINE];
        int panel_item_count = 0;
        if (strcmp(active_panel, "craft") == 0) panel_item_count = load_recipe_ids(panel_ids, MAX_PANEL_ITEMS);
        else if (strcmp(active_panel, "inventory") == 0) panel_item_count = load_inventory_ids(panel_ids, MAX_PANEL_ITEMS);
        int panel_total = panel_item_count + 1; /* + trailing Cancel row */

        if (is_up || is_down) {
            /* Wraparound arrow-cursor movement - real chtpm_parser.c
             * convention (confirmed via direct citation), always live
             * while a panel is open regardless of interact_mode's own
             * value (a panel IS already a menu-mode context, same as
             * move_player.c already independently suspends movement
             * here - no additional toggle needed to reach this). */
            if (panel_cursor < 1 || panel_cursor > panel_total) panel_cursor = is_down ? 0 : (panel_total + 1);
            if (is_up) { panel_cursor--; if (panel_cursor < 1) panel_cursor = panel_total; }
            else       { panel_cursor++; if (panel_cursor > panel_total) panel_cursor = 1; }
            panel_digit_accum = 0;
        } else if (is_escape) {
            /* Close the panel without acting - same outcome as
             * selecting the trailing Cancel row, just reachable
             * without digits (see this file's header comment on
             * is_escape). */
            snprintf(active_panel, sizeof(active_panel), "none");
            panel_cursor = 0;
            panel_digit_accum = 0;
        } else if (is_digit) {
            int d = key - '0';
            int new_val = panel_digit_accum * 10 + d;
            if (new_val >= 1 && new_val <= panel_total) {
                panel_digit_accum = new_val;
                panel_cursor = new_val;
            } else if (d >= 1 && d <= panel_total) {
                panel_digit_accum = d;
                panel_cursor = d;
            } else {
                panel_digit_accum = 0;
            }
        } else if (is_enter) {
            if (strcmp(active_panel, "craft") == 0 && panel_cursor >= 1 && panel_cursor <= panel_item_count) {
                snprintf(exec_handler, sizeof(exec_handler), "ops/+x/craft.+x");
                snprintf(exec_arg, sizeof(exec_arg), "%s", panel_ids[panel_cursor - 1]);
            }
            /* Inventory panel: never execs anything, whatever row is
             * selected - it's read-only browsing, not an action list.
             * Craft panel on Cancel/out-of-range: also nothing to exec.
             * Either way, closing is the only outcome. */
            snprintf(active_panel, sizeof(active_panel), "none");
            panel_cursor = 0;
            panel_digit_accum = 0;
        } else {
            /* Any other key is a pure no-op while a panel is open - see
             * this file's header comment for why closing on a stray key
             * would be bad UX. Only abandon a partial digit sequence. */
            panel_digit_accum = 0;
        }
    } else if (is_interact_toggle) {
        /* Only meaningful outside a panel - a panel is already its own
         * menu-mode context, so toggling interact_mode while one is
         * open would be a no-op distinction without a difference.
         * Entering (0->1) resets the xlector cursor to the hero's
         * CURRENT position every time - a fresh start each entry,
         * matching real 1.TPMOS's own xlector behavior of always
         * starting wherever the player currently is, not remembering a
         * stale position from a previous session. */
        interact_mode = !interact_mode;
        if (interact_mode) { xlector_x = hero_x; xlector_y = hero_y; }
        digit_accum = 0;
        action_cursor = -1;
    } else if (is_escape) {
        /* No panel open - Escape exits interact_mode back to plain
         * movement if currently active; a harmless no-op otherwise
         * (matches every other unrecognized key's "abandon the pending
         * sequence" behavior in the final else branch below). Matches
         * real chtpm_parser.c's own "ESC always wins, checked first"
         * convention (confirmed via direct citation). */
        interact_mode = 0;
        digit_accum = 0;
        action_cursor = -1;
    } else if (interact_mode) {
        /* Real xlector "select" (adapted - see this file's own header
         * comment and dox/04-chtpm-parser-research-and-interact-
         * mode.txt for the full research): Enter examines whatever's
         * at the cursor's CURRENT position (moved by ops/move_player.c,
         * not this file - arrows belong to that op while interact_mode
         * is active, never reach here as a nav concern). Digits/
         * anything else are a pure no-op - there is no action-bar menu
         * to jump around while controlling the cursor. */
        if (is_enter) {
            /* Possession check FIRST - see try_possess_at()'s own
             * header comment. Only exits interact_mode (and skips the
             * normal examine) when the cursor is on a real possessable
             * piece's own tile; otherwise falls through to the
             * existing plain examine behavior unchanged (items,
             * monsters - never possessable today). */
            if (try_possess_at(hero_x, hero_y, xlector_x, xlector_y)) {
                interact_mode = 0;
            } else {
                examine_at(map_id, xlector_x, xlector_y);
            }
        }
        /* Thrown ranged combat, real, direct user request - reuses
         * the SAME xlector cursor "select" mechanism as examine (see
         * this file's own header comment): 't'/'T' throws the first
         * weapon in inventory at whatever's under the cursor, if
         * anything's there and it's in range. See throw_at()'s own
         * header comment for the real, named difference from CDDA
         * (item is consumed outright, doesn't land recoverable on the
         * ground) and for firearms/ammo being a separate, deferred
         * future step. */
        else if (key == 't' || key == 'T') throw_at(map_id, hero_x, hero_y, xlector_x, xlector_y);
        digit_accum = 0;
    } else if (is_digit) {
        /* Real fuzz-op_manager.c convention (confirmed via direct
         * read, line 710: "if (key >= '2' && key <= '9') method_key =
         * key - '0'" - dispatched immediately, same keypress, no
         * separate confirm step exists in the real reference at all).
         * This file used to require a SECOND keypress (Enter) to
         * commit a digit selection - harmless for direct terminal
         * play (a player naturally types both), but a real bug for
         * chtpm-driven play: a ${piece_methods} button's
         * onClick="KEY:n" (chtpm_parser_pal.c's load_dynamic_methods())
         * relays exactly ONE keypress per click, so the old two-stage
         * model could set action_cursor but could never actually fire
         * - EVERY numbered action (pickup/drop/eat/craft/examine/save,
         * not just the newer toggle_emoji) silently no-op'd when
         * driven through chtpm nav, confirmed live. Single-stage,
         * immediate-on-digit dispatch matches the real reference and
         * fixes both input paths at once. */
        int active_gui_idx = read_active_gui_index();
        int gui_is_typing = read_active_gui_is_typing();
        if (gui_is_typing && (active_gui_idx == CHTPM_CRAFT_GUI_IDX || active_gui_idx == CHTPM_EXAMINE_GUI_IDX)) {
            /* Real chtpm-native ACTIVATE submenu item, relayed by
             * game.chtpm's own "craft"/"examine" buttons (see
             * compose_frame.c's write_panel_gui_state() and this
             * file's own CHTPM_*_GUI_IDX comment) - NOT the outer
             * action bar at all, even though it's the same raw digit
             * space. "Close" never reaches here - it's onClick="BACK",
             * handled entirely by the parser, no relay. */
            char panel_ids[MAX_PANEL_ITEMS][MAX_LINE];
            int item_idx = key - '0'; /* 1-based, matches write_panel_gui_state()'s own KEY:'1'.. numbering */
            if (active_gui_idx == CHTPM_CRAFT_GUI_IDX) {
                int n = load_recipe_ids(panel_ids, MAX_PANEL_ITEMS);
                if (item_idx >= 1 && item_idx <= n) {
                    snprintf(exec_handler, sizeof(exec_handler), "ops/+x/craft.+x");
                    snprintf(exec_arg, sizeof(exec_arg), "%s", panel_ids[item_idx - 1]);
                }
            }
            /* Examine/inventory: read-only, matches the old in_panel
             * behavior exactly - selecting a row never execs anything. */
        } else {
            char names[32][MAX_LINE];
            int total = load_method_names(names, 32);
            int d = key - '0';
            if (d >= 2 && d < total) {
                action_cursor = d;
                if (strcmp(names[d], "_craft") == 0) {
                    /* Craft doesn't execute directly - real "run" mode
                     * (no chtpm layer) still opens the OLD text overlay
                     * panel here. Renamed from "craft" only to hide
                     * this METHOD row from chtpm's own auto-generated
                     * ${piece_methods} flat list (leading underscore is
                     * chtpm_parser_pal.c's own real, unmodified skip
                     * rule) - chtpm mode's REAL trigger is game.chtpm's
                     * own hand-authored "craft" ACTIVATE button, a
                     * completely separate path from this branch. */
                    snprintf(active_panel, sizeof(active_panel), "craft");
                    panel_cursor = 1;
                    panel_digit_accum = 0;
                } else if (strcmp(names[d], "_examine") == 0) {
                    /* Same pattern - opens the inventory browse panel
                     * instead of running examine.+x directly. */
                    snprintf(active_panel, sizeof(active_panel), "inventory");
                    panel_cursor = 1;
                    panel_digit_accum = 0;
                } else {
                    char cmd[PATH_BUF];
                    snprintf(cmd, sizeof(cmd), "'%s/ops/+x/pdl_reader.+x' hero get_method '%s'", project_root, names[d]);
                    FILE *pf = popen(cmd, "r");
                    if (pf) {
                        if (fgets(exec_handler, sizeof(exec_handler), pf)) exec_handler[strcspn(exec_handler, "\n")] = '\0';
                        pclose(pf);
                    }
                }
            }
        }
        digit_accum = 0;
    } else if (is_enter) {
        /* Real fuzz-op_manager.c has no bare-Enter action-bar concept
         * at all in this context - its own Enter is reserved for the
         * xlector select-at-cursor-tile mechanic, and mutaclsym's own
         * equivalent already lives entirely in the interact_mode=1
         * branch above, not here. Kept as a harmless reset (not
         * removed outright) so a stray Enter can't leave a stale
         * action_cursor/digit_accum sitting around. */
        digit_accum = 0;
        action_cursor = -1;
    } else {
        /* Any other key (movement, 'q', an unrecognized byte) abandons
         * a pending sequence rather than leaving it stale for several
         * turns. */
        digit_accum = 0;
        action_cursor = -1;
    }

    f = fopen(hero_path, "w");
    if (!f) return 1;
    int ac_found = 0, da_found = 0, ap_found = 0, pc_found = 0, pda_found = 0, lk_found = 0, im_found = 0;
    int xx_found = 0, xy_found = 0, em_found = 0, rm_found = 0, cm_found = 0;
    for (int i = 0; i < nlines; i++) {
        char *eq = strchr(lines[i], '=');
        if (eq) {
            *eq = '\0';
            if (strcmp(lines[i], "action_cursor") == 0) { fprintf(f, "action_cursor=%d\n", action_cursor); ac_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "digit_accum") == 0) { fprintf(f, "digit_accum=%d\n", digit_accum); da_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "active_panel") == 0) { fprintf(f, "active_panel=%s\n", active_panel); ap_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "panel_cursor") == 0) { fprintf(f, "panel_cursor=%d\n", panel_cursor); pc_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "panel_digit_accum") == 0) { fprintf(f, "panel_digit_accum=%d\n", panel_digit_accum); pda_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "interact_mode") == 0) { fprintf(f, "interact_mode=%d\n", interact_mode); im_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "emoji_mode") == 0) { fprintf(f, "emoji_mode=%d\n", emoji_mode); em_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "render_mode") == 0) { fprintf(f, "render_mode=%d\n", render_mode); rm_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "camera_mode") == 0) { fprintf(f, "camera_mode=%d\n", camera_mode); cm_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "xlector_pos_x") == 0) { fprintf(f, "xlector_pos_x=%d\n", xlector_x); xx_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "xlector_pos_y") == 0) { fprintf(f, "xlector_pos_y=%d\n", xlector_y); xy_found = 1; *eq = '='; continue; }
            if (strcmp(lines[i], "last_key") == 0) { fprintf(f, "last_key=%d\n", key); lk_found = 1; *eq = '='; continue; }
            *eq = '=';
        }
        fputs(lines[i], f);
    }
    if (!ac_found) fprintf(f, "action_cursor=%d\n", action_cursor);
    if (!da_found) fprintf(f, "digit_accum=%d\n", digit_accum);
    if (!ap_found) fprintf(f, "active_panel=%s\n", active_panel);
    if (!pc_found) fprintf(f, "panel_cursor=%d\n", panel_cursor);
    if (!pda_found) fprintf(f, "panel_digit_accum=%d\n", panel_digit_accum);
    if (!im_found) fprintf(f, "interact_mode=%d\n", interact_mode);
    if (!em_found) fprintf(f, "emoji_mode=%d\n", emoji_mode);
    if (!rm_found) fprintf(f, "render_mode=%d\n", render_mode);
    if (!cm_found) fprintf(f, "camera_mode=%d\n", camera_mode);
    if (!xx_found) fprintf(f, "xlector_pos_x=%d\n", xlector_x);
    if (!xy_found) fprintf(f, "xlector_pos_y=%d\n", xlector_y);
    /* last_key: the raw keycode this op was invoked with, on EVERY
     * tick regardless of what it turned out to mean - a debugging aid
     * so "why does the game look frozen" (e.g. repeatedly pressing a
     * movement key into a wall, which used to be a silent no-op - see
     * move_player.c's new "You can't go that way." message) has a
     * second, independent way to confirm input really is being read,
     * not just trust the message log. compose_frame.c displays this
     * in the footer. */
    if (!lk_found) fprintf(f, "last_key=%d\n", key);
    fclose(f);

    if (exec_handler[0]) {
        char exec_cmd[PATH_BUF];
        if (exec_arg[0]) snprintf(exec_cmd, sizeof(exec_cmd), "'%s/%s' '%s'", project_root, exec_handler, exec_arg);
        else snprintf(exec_cmd, sizeof(exec_cmd), "'%s/%s'", project_root, exec_handler);
        int rc = system(exec_cmd);
        (void)rc; /* the handler op's own exit status isn't consulted here, matching every other op-calling-op site in this project */
    }
    return 0;
}
