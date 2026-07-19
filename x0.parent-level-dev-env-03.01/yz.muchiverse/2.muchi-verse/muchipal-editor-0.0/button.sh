#!/bin/bash
# button.sh - launcher for muchipal-editor, same verb convention as
# mutaclsym's own button.sh (which itself mirrors real 1.TPMOS's).
#
# system/prisc+x.c, keyboard_input.c, renderer.c are copied verbatim
# from mutaclsym - they're project-agnostic pal-VM infrastructure (own
# root resolution via PRISC_PROJECT_ROOT, no project-specific paths
# baked in), matching this whole family's "duplicate rather than share
# a header" doctrine rather than pointing at a shared install.
#
# "run" starts with a REAL chtpm-layout title screen (pieces/chtpm/
# layouts/title.chtpm, rendered by the shared system/chtpm_parser_pal -
# see chtpm-to-pal-layout-plan.txt), then hands off to the existing
# prisc+x-driven flow (projects->project_menu->pieces->piece_detail->
# map_edit, all still owned by ops/project_browser.c/map_edit_input.c
# as before - only the title screen itself is chtpm so far; the
# "projects"/"pieces" screens are dynamic-length lists chtpm's own
# markup has no repeat/loop construct for yet, a real, named follow-up,
# not silently skipped). One single system/keyboard_input process runs
# for the WHOLE session (it dual-writes every keystroke to both
# history-file formats unconditionally, so it doesn't matter which
# reader is currently listening) - only WHICH renderer/dispatcher is
# reading changes, via this handoff:
#   phase 1: system/chtpm_parser_pal pieces/chtpm/layouts/title.chtpm
#   phase 2 (once "Open Project" -> onClick="SET_SCREEN:projects" is seen in
#     pieces/display/pending_command.txt): kill phase 1, set
#     editor_state.txt's own screen=projects, start
#     system/prisc+x pal/main_loop.pal - same self-filtering-by-screen
#     convention project_browser.c already uses elsewhere.
#   system/renderer runs the whole time, unmodified - it just polls
#     pieces/display/current_frame.txt, doesn't care who wrote it.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    run|r|start)
        cd "$SCRIPT_DIR"
        mkdir -p pieces/system pieces/display pieces/apps/player_app pieces/keyboard
        : > pieces/system/quit_flag.txt
        : > pieces/apps/player_app/history.txt
        : > pieces/keyboard/history.txt
        : > pieces/display/pending_command.txt

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="muchipal-editor"

        ./system/renderer &
        RENDERER_PID=$!
        trap 'kill "$RENDERER_PID" "$CHTPM_PID" "$HANDOFF_PID" "$PRISC_PID" 2>/dev/null' EXIT INT TERM

        ./system/chtpm_parser_pal pieces/chtpm/layouts/title.chtpm >/dev/null 2>&1 &
        CHTPM_PID=$!
        ( while ! grep -q "SET_SCREEN:projects" pieces/display/pending_command.txt 2>/dev/null; do
              sleep 0.05
          done
          kill "$CHTPM_PID" 2>/dev/null
          : > pieces/display/pending_command.txt
          # REAL BUG FIX (found via live testing, not assumed): prisc+x's
          # own pal script always starts its read_history cursor at byte
          # 0 on a fresh launch. keyboard_input dual-writes every
          # keystroke unconditionally, so any keys pressed during the
          # chtpm title phase (already correctly consumed by
          # chtpm_parser_pal via the OTHER history file) are ALSO still
          # sitting at the start of player_app/history.txt, waiting -
          # prisc+x would replay them the instant it starts, silently
          # double-dispatching the same keystrokes into
          # project_browser.c (confirmed live: pressing '1'+Enter to
          # leave the title screen landed two screens deep, not one,
          # because project_browser.c immediately replayed that exact
          # '1'+Enter itself). Truncate so the new process starts on a
          # truly clean slate - every key from here on belongs only to
          # the phase-2 flow.
          : > pieces/apps/player_app/history.txt
          cat > pieces/system/editor_state.txt << 'EOSTATE'
screen=projects
cursor=1
digit_accum=0
proj_name=
proj_path=
map_rel_path=
registry_format=
registry_rel_path=
piece_pdl_path=
cursor_x=0
cursor_y=0
armed_idx=0
EOSTATE
          ./system/prisc+x pal/main_loop.pal >/dev/null 2>&1 &
          echo $! > /tmp/muchipal_editor_prisc_pid.$$
        ) &
        HANDOFF_PID=$!

        ./system/keyboard_input

        if [ -f "/tmp/muchipal_editor_prisc_pid.$$" ]; then
            PRISC_PID="$(cat "/tmp/muchipal_editor_prisc_pid.$$")"
            rm -f "/tmp/muchipal_editor_prisc_pid.$$"
        fi
        kill "$RENDERER_PID" "$CHTPM_PID" "$HANDOFF_PID" "$PRISC_PID" 2>/dev/null
        ;;
    kill|k|stop)
        echo "=== Killing muchipal-editor processes ==="
        # REAL BUG FIX: run launches every binary via a RELATIVE path
        # (./system/foo, after cd "$SCRIPT_DIR") so its recorded command
        # line never contains $SCRIPT_DIR at all - matching against the
        # absolute path here silently never found the process, letting
        # it leak forever (confirmed live: found two such orphaned
        # prisc+x processes from earlier sessions, still running,
        # neither ever caught by this action). Match the bare relative
        # substring instead - pkill -f matches anywhere in the command
        # line, so this catches it regardless of launch-time cwd.
        pkill -f "system/keyboard_input" 2>/dev/null
        pkill -f "system/renderer" 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        pkill -f "system/chtpm_parser_pal" 2>/dev/null
        # Per direct instruction ("maybe it should run a kill_all.sh
        # script like 1.tpmos does") after a real orphaned-process
        # incident this session - delegate to the shared, surgical,
        # SIGKILL-based 2.muchi-verse/kill_all.sh (modeled on real
        # 1.TPMOS's own pieces/os/kill_all.sh) as the authoritative
        # cleanup, not just this project's own local pass above.
        bash "$SCRIPT_DIR/../kill_all.sh"
        echo "done"
        ;;
    check|verify)
        for b in system/prisc+x system/keyboard_input system/renderer \
                 system/chtpm_parser_pal \
                 ops/+x/compose_title_frame.+x \
                 ops/+x/project_browser.+x ops/+x/pdl_reader.+x ops/+x/map_edit_input.+x; do
            if [ -x "$SCRIPT_DIR/$b" ]; then
                echo "OK   $b"
            else
                echo "MISSING $b"
            fi
        done
        ;;
    help|h|-h|--help)
        echo "muchipal-editor button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo ""
        echo "Actions:"
        echo "  compile, c, build   - Build all binaries (prisc+x, keyboard_input, renderer, ops)"
        echo "  run, r, start       - Run the editor (keyboard_input + prisc+x + renderer)"
        echo "  kill, k, stop       - Kill any lingering muchipal-editor processes"
        echo "  check, verify       - Verify all binaries exist"
        echo "  help, h             - Show this help"
        echo ""
        echo "Recommended workflow:"
        echo "  1. ./button.sh compile"
        echo "  2. ./button.sh check"
        echo "  3. ./button.sh run"
        ;;
    *)
        echo "Unknown action: $ACTION"
        echo "Run './button.sh help' for usage."
        exit 1
        ;;
esac
