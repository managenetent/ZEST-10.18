#!/bin/bash
# button.sh - launcher for muchi-evo-pal's first vertical slice.
# Deliberately NOT the full interactive keyboard_input/renderer/
# compose_frame loop yet (unlike mutaclsym/muchi-pal-chat's own
# button.sh) - this slice is a non-interactive FSM tick-loop proof
# (see pal/main_loop.pal's own header comment), tested directly via
# `./button.sh tick`. The interactive shell is real future work once
# this slice's FSM-in-PAL mechanism is proven, per EVO-DESIGN.txt §7.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    tick|run|r)
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="muchi-evo-pal"
        ./system/prisc+x pal/main_loop.pal
        echo "--- final creature_01 state ---"
        cat projects/muchi-evo-pal/pieces/creature_01/state.txt
        ;;
    step|s)
        # ONE tick only, via pal/single_tick.pal - the human-drivable
        # counterpart to `tick` (which auto-runs 12 in a row). Meant to
        # be called repeatedly, alternating with `choose` when
        # decision_mode=4.
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="muchi-evo-pal"
        ./system/prisc+x pal/single_tick.pal
        cat projects/muchi-evo-pal/pieces/creature_01/state.txt
        ;;
    choose)
        # button.sh choose eat|evolve - a human's manual decision,
        # queued for the next `step` to consume (decision_mode=4 only).
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        ./ops/+x/bot_set_human_decision.+x creature_01 "$2"
        ;;
    reset)
        cd "$SCRIPT_DIR"
        printf "current_state=0\ndecision_mode=1\nep=0\nbody_parts=\nlast_choice=\n" > projects/muchi-evo-pal/pieces/creature_01/state.txt
        echo "creature_01 reset."
        ;;
    check|verify)
        for b in system/prisc+x ops/+x/bot_tick_idle.+x ops/+x/bot_choose.+x \
                 ops/+x/bot_eat.+x ops/+x/bot_evolve.+x ops/+x/connect_op.+x \
                 ops/+x/json_parser.+x; do
            if [ -x "$SCRIPT_DIR/$b" ]; then echo "OK   $b"; else echo "MISSING $b"; fi
        done
        ;;
    help|h|-h|--help)
        echo "muchi-evo-pal button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo "  compile, c, build   - Build prisc+x + ops"
        echo "  tick, run, r        - Run 12 FSM ticks against creature_01, print final state (automated tiers)"
        echo "  step, s             - Run exactly ONE tick, print state (human tier - alternate with 'choose')"
        echo "  choose eat|evolve   - Queue a manual decision for the next 'step' (decision_mode=4 only)"
        echo "  reset               - Reset creature_01 back to a fresh state"
        echo "  check, verify       - Verify all binaries exist"
        echo "  help, h             - Show this help"
        ;;
    *)
        echo "Unknown action: $ACTION"
        exit 1
        ;;
esac
