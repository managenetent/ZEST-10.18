#!/bin/bash
# button.sh - launcher for piececraft-3d-pal, same verb convention as
# mutaclsym's/muchipal-editor's own button.sh.
#
# system/prisc+x.c, keyboard_input.c, renderer.c are copied verbatim
# from mutaclsym - project-agnostic pal-VM infrastructure, matching
# this whole family's "duplicate rather than share a header" doctrine.
# system/gl_mirror.c is ALSO copied from mutaclsym, unmodified except
# for its WIDTH/HEIGHT constants - proof of GRAND-ARCHITECTURE.md §0's
# "one shared rendering daemon" bet: a second, different project's GL
# mirror needed zero new GL code.
#
# v0 scope: map_01 is a fixed, whole, flat top-down scene - no hero, no
# camera, no movement yet (see ops/compose_rgb_frame.c's own header).
# "run" starts the same three processes mutaclsym does; "gl" is the
# same optional 4th process mutaclsym's own button.sh has.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    run|r|start)
        cd "$SCRIPT_DIR"
        mkdir -p pieces/system pieces/display pieces/apps/player_app
        : > pieces/system/quit_flag.txt
        : > pieces/apps/player_app/history.txt

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="piececraft-3d-pal"

        ./system/renderer &
        RENDERER_PID=$!
        ./system/prisc+x pal/main_loop.pal >/dev/null 2>&1 &
        PRISC_PID=$!
        trap 'kill "$RENDERER_PID" "$PRISC_PID" 2>/dev/null' EXIT INT TERM

        ./system/keyboard_input

        kill "$RENDERER_PID" "$PRISC_PID" 2>/dev/null
        ;;
    gl|mirror)
        cd "$SCRIPT_DIR"
        if [ ! -x "system/gl_mirror" ]; then
            echo "system/gl_mirror not built (GLUT/GL may not be available - see scripts/build.sh output)"
            exit 1
        fi
        mkdir -p pieces/display
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="piececraft-3d-pal"
        echo "Launching gl_mirror - run './button.sh run' separately (or first) so"
        echo "pieces/display/rgb_frame.raw actually gets written."
        echo "Correctness is verifiable via pieces/display/gl_display.receipt.txt"
        echo "and rgb_frame.receipt.txt without needing to see the window - or run"
        echo "./ops/+x/dump_rgb_png.+x for a real viewable PNG."
        ./system/gl_mirror
        ;;
    kill|k|stop)
        echo "=== Killing piececraft-3d-pal processes ==="
        pkill -f "$SCRIPT_DIR/system/keyboard_input" 2>/dev/null
        pkill -f "$SCRIPT_DIR/system/renderer" 2>/dev/null
        pkill -f "$SCRIPT_DIR/system/prisc\+x" 2>/dev/null
        pkill -f "$SCRIPT_DIR/system/gl_mirror" 2>/dev/null
        echo "done"
        ;;
    check|verify)
        for b in system/prisc+x system/keyboard_input system/renderer \
                 ops/+x/compose_rgb_frame.+x ops/+x/dump_rgb_png.+x; do
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
        echo "piececraft-3d-pal button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo ""
        echo "Actions:"
        echo "  compile, c, build   - Build all binaries"
        echo "  run, r, start       - Run (keyboard_input + prisc+x + renderer)"
        echo "  gl, mirror          - Launch the optional GL/RGB mirror window"
        echo "  kill, k, stop       - Kill any lingering processes"
        echo "  check, verify       - Verify all binaries exist"
        echo "  help, h             - Show this help"
        echo ""
        echo "Recommended workflow:"
        echo "  1. ./button.sh compile"
        echo "  2. ./button.sh check"
        echo "  3. ./button.sh run"
        echo "  4. ./ops/+x/dump_rgb_png.+x   (view pieces/display/rgb_frame_debug.png)"
        ;;
    *)
        echo "Unknown action: $ACTION"
        echo "Run './button.sh help' for usage."
        exit 1
        ;;
esac
