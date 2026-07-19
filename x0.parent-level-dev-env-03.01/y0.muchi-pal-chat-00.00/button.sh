#!/bin/bash
# button.sh - launcher for muchi-pal-chat, same verb convention and same
# 3-process shape as mutaclsym's own button.sh:
#   - system/keyboard_input : owns the real terminal in raw mode, reads
#     keys itself, appends bare keycodes to history.txt. Foreground, since
#     it's the one process that needs the controlling tty.
#   - system/prisc+x pal/main_loop.pal : reads history.txt, dispatches to
#     buffer_key/send_message/check_response/compose_frame, halts itself
#     on 'q'.
#   - system/renderer : cooked-mode stdout writer, polls the frame pulse
#     marker, prints current_frame.txt, logs every frame to
#     frame_history.txt for audit. Backgrounded.
# "run" tracks both background PIDs and kills them once keyboard_input
# exits, per cdda-tpm-std-fast.txt's "never leave an untracked subprocess
# running" rule.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    run|r|start)
        cd "$SCRIPT_DIR"
        mkdir -p pieces/system pieces/display pieces/apps/player_app pieces/world_01/session_01/chat
        : > pieces/system/quit_flag.txt
        : > pieces/apps/player_app/history.txt

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="muchi-pal-chat"

        ./system/renderer &
        RENDERER_PID=$!
        ./system/prisc+x pal/main_loop.pal >/dev/null 2>&1 &
        PRISC_PID=$!
        trap 'kill "$RENDERER_PID" "$PRISC_PID" 2>/dev/null' EXIT INT TERM

        ./system/keyboard_input

        kill "$RENDERER_PID" "$PRISC_PID" 2>/dev/null
        ;;
    kill|k|stop)
        echo "=== Killing muchi-pal-chat processes ==="
        pkill -f "$SCRIPT_DIR/system/keyboard_input" 2>/dev/null
        pkill -f "$SCRIPT_DIR/system/renderer" 2>/dev/null
        pkill -f "$SCRIPT_DIR/system/prisc\+x" 2>/dev/null
        echo "done"
        ;;
    check|verify)
        for b in system/prisc+x system/keyboard_input system/renderer \
                 ops/+x/buffer_key.+x ops/+x/send_message.+x ops/+x/check_response.+x \
                 ops/+x/execute_tool.+x ops/+x/deny_tool.+x ops/+x/switch_model.+x \
                 ops/+x/compose_frame.+x ops/+x/json_parser.+x ops/+x/connect_op.+x \
                 ops/+x/text_to_pal_prompt.+x \
                 ops/+x/file_ops.+x ops/+x/cmd_exec.+x ops/+x/edit_file.+x \
                 ops/+x/list_dir.+x ops/+x/search_in_files.+x ops/+x/web_search.+x; do
            if [ -x "$SCRIPT_DIR/$b" ]; then
                echo "OK   $b"
            else
                echo "MISSING $b"
            fi
        done
        ;;
    help|h|-h|--help)
        echo "muchi-pal-chat button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo ""
        echo "Actions:"
        echo "  compile, c, build   - Build all binaries (prisc+x, keyboard_input, renderer, ops)"
        echo "  run, r, start       - Run the chat (keyboard_input + prisc+x + renderer)"
        echo "  kill, k, stop       - Kill any lingering muchi-pal-chat processes"
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
