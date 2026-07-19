#!/bin/bash
# button.sh - launcher for mutaclsym, same verb convention as TPMOS's
# button.sh (c/compile, r/run, k/kill...).
#
# No ncurses anywhere: "run" starts these processes via chtpm_parser_pal -
#   - system/keyboard_input : owns the real terminal in raw mode, reads
#     keys itself, appends bare keycodes to history.txt. Runs in the
#     foreground since it's the one that needs the controlling tty.
#   - system/chtpm_parser_pal pieces/chtpm/layouts/game.chtpm : parses
#     the menu layout and spawns system/prisc+x pal/main_loop_chtpm.pal
#     as a separate persistent process (reads its own history from
#     interact_relay.txt), writes menu chrome + embedded game content
#     to pieces/display/current_frame.txt.
#   - system/renderer : cooked-mode stdout writer, polls the frame
#     pulse marker, prints current_frame.txt, logs every frame to
#     frame_history.txt for audit. Backgrounded.
#   - system/chtpm_rgb_render : font-rasterizes current_frame.txt
#     verbatim (menu chrome + embedded game content) into rgb_frame.raw,
#     the SAME path gl_mirror reads (replaces compose_rgb_frame which
#     doesn't run in chtpm mode).
#   - system/gl_mirror : a GLUT window that blits rgb_frame.raw (a plain
#     RGBA32 buffer computed by portable CPU C, zero GL calls anywhere
#     except gl_mirror.c itself - see GOVERNING CONSTRAINT in
#     2.muchi-verse/GRAND-ARCHITECTURE.md). Same "one state, two renderers"
#     mirror the ASCII path already is; this one just also happens to open
#     a window, and per direct intent it pops up automatically alongside
#     the terminal - it's a SECOND LIVE INPUT SOURCE usable simultaneously
#     with the terminal (real keyboard/arrow forwarding via its own GLUT
#     callbacks - confirmed working via direct synthetic-key testing once
#     the window has real focus). "run" launches it automatically if
#     system/gl_mirror was actually built (best-effort in scripts/build.sh -
#     some environments lack GLUT dev libs/a display, and the window failing
#     to open there is not fatal to the rest of "run"). Set NO_GL=1 to skip
#     launching it explicitly (e.g. for a headless/no-DISPLAY test run).
# "run" tracks every background PID it started and kills them (per the
# cdda-tpm-std-fast.txt rule: never leave an untracked subprocess
# running) once keyboard_input exits.
#
# "gl" below is a way to (re-)launch just the mirror window standalone
# - useful if "run" was started with NO_GL=1, or without a display
# available at the time, or after the window was closed without
# quitting the whole game. gl_mirror.c's own receipt-writing
# (pieces/display/gl_display.receipt.txt) is how its correctness gets
# verified either way, not the window itself.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    run|r|start)
        # Real interact+module pattern (see chtpm-to-pal-layout-plan.txt
        # §8 and pal-standards.txt §7) - the primary entry point for
        # running the game (least-impact: uses the proven menu + embedded
        # gameplay flow with chtpm_parser_pal + main_loop_chtpm.pal).
        # pieces/chtpm/layouts/game.chtpm's own
        # <module>${module_path}</module> tag makes chtpm_parser_pal
        # ITSELF launch system/prisc+x pal/main_loop_chtpm.pal as a
        # separate, parallel, persistent process the instant the layout
        # is parsed - a NEW pal script that boots straight into
        # gameplay, skipping the title phase entirely. chtpm_parser_pal
        # writes keyboard input (when interact is active) to the file
        # specified in the layout's <interact src=...> tag, matching
        # fuzz-op's proven standard. system/renderer already works
        # unmodified - it just polls pieces/display/current_frame.txt,
        # which chtpm_parser_pal writes (menu chrome + the embedded
        # ${game_map}).
        cd "$SCRIPT_DIR"
        mkdir -p pieces/system pieces/display pieces/apps/player_app pieces/keyboard
        rm -f pieces/apps/player_app/interact_relay.txt
        # chtpm_parser_pal reads pieces/keyboard/history.txt from byte 0 on
        # every launch (matching real TPMOS's chtpm_parser.c) - this is the
        # universal chtpm_parser<->keyboard_input bridge file (keyboard_input.c's
        # own "CHTPM-BRIDGE ADDITION"), NOT the map-specific interact_relay.txt
        # pattern. Must be cleared every launch or stale KEY_PRESSED lines from
        # the previous session replay immediately on startup (real TPMOS's own
        # run_chtpm.sh clears this exact file for the same reason).
        : > pieces/keyboard/history.txt
        # REAL BUG FIX (user-reported "unresponsive ascii", found via
        # direct testing): system/renderer.c's own loop is
        # `while (!quit_requested())`, checking whether
        # pieces/system/quit_flag.txt is non-empty - written by
        # keyboard_input.c on EVERY exit (any session). Without resetting
        # it here, a session that was ever quit via 'q' before leaves
        # this file non-empty, so the very next launch's own renderer
        # sees quit_requested()==true BEFORE its own loop even starts - it
        # prints exactly one frame (the unconditional render_frame() call
        # before the loop) and exits immediately. chtpm_parser_pal and
        # keyboard_input keep running fine underneath, correctly updating
        # current_frame.txt - but nothing is left to print it to the
        # terminal, giving the exact "looks unresponsive" symptom.
        : > pieces/system/quit_flag.txt
        rm -f pieces/system/gl_focus.lock
        cat > pieces/apps/player_app/state.txt << 'EOSTATE'
module_path=system/prisc+x pal/main_loop_chtpm.pal
active_target_id=hero
EOSTATE

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="mutaclsym"

        ./system/renderer &
        RENDERER_PID=$!
        ./system/chtpm_parser_pal pieces/chtpm/layouts/game.chtpm >/dev/null 2>&1 &
        CHTPM_PID=$!

        # chtpm_rgb_render: real wraith_rgb_daemon.c equivalent (see
        # that file's own header comment in shared-ops/) - font-
        # rasterizes current_frame.txt verbatim (chrome AND embedded
        # game content) into rgb_frame.raw, the SAME path gl_mirror
        # already reads. Replaces the module's own compose_rgb_frame
        # call for chtpm mode specifically (removed from
        # main_loop_chtpm.pal - see that file's own comment) since
        # compose_rgb_frame draws real tile graphics from game state
        # directly and never touches current_frame.txt at all, which
        # is exactly why chtpm's own menu chrome never appeared in the
        # GL window before this daemon existed.
        RGB_PID=""
        if [ -x "system/chtpm_rgb_render" ]; then
            ./system/chtpm_rgb_render >/tmp/mutaclsym_chtpm_rgb_render.log 2>&1 &
            RGB_PID=$!
        fi

        GL_PID=""
        if [ -z "$NO_GL" ] && [ -x "system/gl_mirror" ]; then
            ./system/gl_mirror >/tmp/mutaclsym_gl_mirror.log 2>&1 &
            GL_PID=$!
            echo "GL/RGB mirror window launching (pid $GL_PID) - a second live" >&2
            echo "input source alongside this terminal. Click it to give it real" >&2
            echo "OS keyboard focus if you want to control the game from there;" >&2
            echo "the terminal keeps working regardless (arrow-key movement only" >&2
            echo "applies once you're past the title screen, which is digit+Enter" >&2
            echo "only by design). If no window appears (no DISPLAY/GLUT), see" >&2
            echo "/tmp/mutaclsym_gl_mirror.log - the terminal session below is" >&2
            echo "unaffected either way." >&2
        fi

        # chtpm_parser_pal has no SIGTERM handler of its own (only
        # SIGINT triggers its own cleanup_module()), so a plain `kill`
        # here would leave its own spawned module (system/prisc+x)
        # orphaned - kill both explicitly, matching the relative-path
        # pkill fix below.
        trap 'kill "$RENDERER_PID" "$CHTPM_PID" $RGB_PID $GL_PID 2>/dev/null; pkill -f "system/prisc\+x" 2>/dev/null' EXIT INT TERM

        # Clear history.txt right before keyboard_input starts (race condition fix:
        # if cleared too early, keyboard_input may repopulate with old keys before
        # the module reads it; clearing here ensures fresh input for this session)
        rm -f pieces/apps/player_app/history.txt
        touch pieces/apps/player_app/history.txt

        ./system/keyboard_input

        kill "$RENDERER_PID" "$CHTPM_PID" $RGB_PID $GL_PID 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        ;;
    gl|mirror)
        cd "$SCRIPT_DIR"
        if [ ! -x "system/gl_mirror" ]; then
            echo "system/gl_mirror not built (GLUT/GL may not be available - see scripts/build.sh output)"
            exit 1
        fi
        mkdir -p pieces/display
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="mutaclsym"
        echo "Launching gl_mirror standalone - './button.sh run' launches the game"
        echo "automatically (which includes gl_mirror if available), so you only need"
        echo "this verb to (re-)launch just the window on its own (e.g. after closing"
        echo "it without quitting the game). Make sure './button.sh run' is also running"
        echo "(or was run first) so pieces/display/rgb_frame.raw actually gets"
        echo "updated each tick. Correctness is verifiable via"
        echo "pieces/display/gl_display.receipt.txt and rgb_frame.receipt.txt"
        echo "without needing to see the window."
        ./system/gl_mirror
        ;;
    generate|gen)
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="mutaclsym"
        shift
        ./ops/+x/generate_map.+x "$@"
        ;;
    kill|k|stop)
        echo "=== Killing mutaclsym processes ==="
        # REAL INCIDENT (this session): a Ctrl+C'd session left a
        # genuinely orphaned prisc+x+keyboard_input pair running,
        # found only by accident via `ps aux`, not by this action
        # (which - separately - had been matching an absolute path
        # that never appears in these processes' real, relative-
        # launched command lines; fixed below too). Per direct
        # instruction ("maybe it should run a kill_all.sh script like
        # 1.tpmos does"), delegate to the shared, surgical, SIGKILL-
        # based 2.muchi-verse/kill_all.sh (modeled on real 1.TPMOS's
        # own pieces/os/kill_all.sh) as the authoritative cleanup -
        # the lines below are this project's own fast local pass,
        # kept as a first line of defense, not the only one.
        pkill -f "system/keyboard_input" 2>/dev/null
        pkill -f "system/renderer" 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        pkill -f "system/gl_mirror" 2>/dev/null
        pkill -f "system/chtpm_parser_pal" 2>/dev/null
        pkill -f "system/chtpm_rgb_render" 2>/dev/null
        bash "$SCRIPT_DIR/../../yz.muchiverse/2.muchi-verse/kill_all.sh"
        sleep 0.2
        rm -f "$SCRIPT_DIR/pieces/system/gl_focus.lock"
        echo "done"
        ;;
    check|verify)
        for b in system/prisc+x system/keyboard_input system/renderer \
                 system/chtpm_parser_pal system/chtpm_rgb_render \
                 ops/+x/move_player.+x ops/+x/end_turn.+x ops/+x/compose_frame.+x ops/+x/pickup.+x ops/+x/drop.+x ops/+x/eat.+x \
                 ops/+x/tick_monsters.+x ops/+x/craft.+x ops/+x/examine.+x ops/+x/save_game.+x \
                 ops/+x/title_input.+x ops/+x/compose_title_frame.+x ops/+x/pdl_reader.+x ops/+x/choice.+x \
                 ops/+x/compose_rgb_frame.+x ops/+x/dump_rgb_png.+x ops/+x/generate_map.+x; do
            if [ -x "$SCRIPT_DIR/$b" ]; then
                echo "OK   $b"
            else
                echo "MISSING $b"
            fi
        done
        if [ -x "$SCRIPT_DIR/system/gl_mirror" ]; then
            echo "OK   system/gl_mirror"
        else
            echo "SKIP system/gl_mirror (optional - needs GLUT/GL, see scripts/build.sh output)"
        fi
        ;;
    help|h|-h|--help)
        echo "mutaclsym button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo ""
        echo "Actions:"
        echo "  compile, c, build   - Build all binaries (prisc+x, keyboard_input, renderer, ops)"
        echo "  run, r, start       - Run the game with chtpm menu interface (keyboard_input +"
        echo "                        chtpm_parser_pal + renderer + gl_mirror, if built -"
        echo "                        set NO_GL=1 to skip the GL window)"
        echo "  gl, mirror          - (Re-)launch just the GL/RGB mirror window standalone"
        echo "  kill, k, stop       - Kill any lingering mutaclsym processes"
        echo "  check, verify       - Verify all binaries exist"
        echo "  generate, gen <map_id> <seed> [w] [h] [link_map_id] [link_x] [link_y]"
        echo "                      - Procedurally generate a new map (authoring-time"
        echo "                        tool, deterministic per seed) and wire a"
        echo "                        bidirectional stairway back into an existing map"
        echo "                        (defaults: 80x40, linked from map_02 at (36,13))"
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
