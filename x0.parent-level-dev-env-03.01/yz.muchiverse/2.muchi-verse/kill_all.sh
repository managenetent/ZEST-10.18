#!/bin/bash
# kill_all.sh - Surgical pal-family process cleanup, modeled directly
# on real 1.TPMOS's own pieces/os/kill_all.sh (same file read in full
# before writing this - same surgical_kill() shape, same SIGKILL
# discipline, same nuclear-option fallback at the end). Built after a
# real, live incident this session: a `Ctrl+C`'d mutaclsym session left
# a genuinely orphaned system/prisc+x + system/keyboard_input pair
# running, found only by accident via `ps aux`, not by any button.sh
# own `kill` action (which - separately fixed this same session, see
# feedback_pkill_relative_path_gotcha.md - had been matching an
# absolute path that never appears in these processes' real,
# relative-launched command lines).
#
# ═══════════════════════════════════════════════════════════════
# WHY SIGKILL (-9), NOT PLAIN `kill` (SIGTERM): chtpm_parser_pal.c
# registers a SIGINT handler but NO SIGTERM handler - a plain `kill`
# still terminates it via SIGTERM's own default disposition, but does
# NOT run its own cleanup_module() first (that only fires on SIGINT),
# potentially orphaning its own spawned module process. This script
# doesn't rely on any process's own signal handling running cleanly at
# all - it kills the WHOLE known process family directly, by name,
# unconditionally. Run this when in doubt, not just each project's own
# narrower `button.sh kill`.
# ═══════════════════════════════════════════════════════════════
#
# Family-wide, not per-project: every pal-native project shares the
# SAME binary names (system/prisc+x, system/keyboard_input, etc, all
# now literally the same shared-ops/ source in most cases) - one
# script here, callable from any project's own directory, instead of
# five near-identical copies.

surgical_kill() {
    local name="$1"
    # "system/${name}" matches this whole family's own real launch
    # convention (every button.sh does `cd "$SCRIPT_DIR"` then
    # `./system/foo`) regardless of which project's directory it was
    # launched from - pkill -f matches anywhere in the full command
    # line, so this is intentionally NOT anchored to any one project's
    # own absolute path (that was the exact bug already fixed
    # separately in each button.sh's own `kill` action).
    local pattern="system/${name}"
    if pgrep -f "$pattern" > /dev/null 2>&1; then
        echo "Killing $name..."
        pkill -9 -f "$pattern" 2>/dev/null
    fi
}

echo "=== pal-family kill_all.sh - surgical cleanup ==="

# The pal VM + its own always-present infrastructure (shared-ops/,
# see shared-ops-refactor-plan.txt)
surgical_kill "prisc+x"
surgical_kill "keyboard_input"
surgical_kill "renderer"
surgical_kill "chtpm_parser_pal"
surgical_kill "chtpm_rgb_render"

# Optional GL/X11 renderers, project-specific but same binary name
# across whichever projects build them
surgical_kill "gl_mirror"
surgical_kill "zoo_window"
surgical_kill "egg_window"

echo ""
echo "Checking for residual pal-family processes (nuclear option)..."
# Same "Nuclear Option" shape as real 1.TPMOS's own kill_all.sh -
# catches anything the named list above missed. Every process in this
# whole family is EITHER launched with a relative ./system/<name> path
# (already covered by surgical_kill above) OR is a prisc+x instance
# whose own second argv is always a *.pal script path (e.g.
# "pal/main_loop.pal", "pal/main_loop_chtpm.pal") - nothing else
# plausibly running on this machine takes a bare ".pal" argument, so
# this is a real, safe catch-all, not the broken absolute-path pattern
# an earlier draft of this file had (confirmed via the same relative-
# path lesson already fixed in every button.sh's own kill action -
# see feedback_pkill_relative_path_gotcha.md).
if pgrep -f '\.pal$' > /dev/null 2>&1; then
    echo "Killing residual prisc+x module(s) by .pal argument..."
    pkill -9 -f '\.pal$' 2>/dev/null
fi

echo "Cleanup complete."
