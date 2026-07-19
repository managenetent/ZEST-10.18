#!/bin/bash
# button.sh - launcher for wsr-pal.
#
# "run" is the REAL, interactive, human-playable game - same 3-process
# shape (keyboard_input owns the tty / prisc+x dispatches / renderer
# draws) already proven in muchi-pal-chat's own button.sh. Per direct
# instruction ("human testability above all things"), this is now the
# PRIMARY way to use this project - the old single-corp-ORB CLI test
# actions (tick/step/choose/reset) are renamed test-* below, kept for
# quick non-interactive op-level testing, not the main entry point
# anymore.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    run|r|start)
        # Real interact+module pattern (see chtpm-to-pal-layout-plan.txt
        # §8 and pal-standards.txt §7) - the primary entry point for
        # running the game. Each wsr-pal SCREEN is now its own real
        # .chtpm layout file (pal-standards.txt §18 - one file per
        # screen, moving between them is a real <button href="...">,
        # matching wraith-alpha's own settings.chtpm precedent, NOT the
        # old single wsr.chtpm file pretending to be six different
        # screens via a hand-rolled GOTO: string) - the entry point is
        # now pieces/chtpm/layouts/wsr_main_menu.chtpm. Every screen's
        # own <module>${module_path}</module> tag makes chtpm_parser_pal
        # ITSELF launch system/prisc+x pal/main_loop_chtpm.pal as a
        # separate, parallel, persistent process the instant that
        # screen's layout is parsed (the SAME command for every screen -
        # href clears current_module_path unconditionally on every
        # transition, forcing a fresh relaunch regardless). wsr-pal has
        # no map/movement concept - its own ${piece_methods} placeholder
        # auto-renders the CURRENT screen's piece.pdl METHOD table
        # (real actions only now - no navigation rows) as real, clickable
        # numbered buttons, no manual "Control Sim" engage step needed at
        # all - chtpm's own normal nav (arrows/digits/Enter) already
        # relays a clicked button's KEY:n straight through to
        # wsr_menu_input.c via inject_raw_key(), matching real TPMOS's
        # own load_dynamic_methods()/piece_methods pattern (proven live
        # in fuzz-op's own layout).
        #
        # interact target is pieces/apps/player_app/interact_relay.txt,
        # NOT history.txt: this project's own system/keyboard_input.c
        # writes every real keystroke to pieces/apps/player_app/history.txt
        # UNCONDITIONALLY (a real, deliberate divergence from real TPMOS's
        # own raw capture, which only ever writes to
        # pieces/keyboard/history.txt) - pointing main_loop_chtpm.pal's own
        # read at that same file would double-dispatch every relayed
        # keypress. interact_relay.txt is written ONLY by chtpm_parser_pal's
        # own conditional relay (inject_raw_key()), matching the
        # single-writer guarantee real TPMOS gets for free from its own
        # narrower raw-capture design (see main_loop_chtpm.pal's own header
        # comment for the full write-up).
        cd "$SCRIPT_DIR"
        bash scripts/ensure_entities.sh
        mkdir -p pieces/system pieces/display pieces/apps/player_app pieces/keyboard
        : > pieces/apps/player_app/interact_relay.txt
        # chtpm_parser_pal reads pieces/keyboard/history.txt from byte 0 on
        # every launch (matching real TPMOS's chtpm_parser.c) - must be
        # cleared every launch or stale KEY_PRESSED lines from the previous
        # session replay immediately on startup.
        : > pieces/keyboard/history.txt
        # REAL BUG FIX: system/renderer.c's own loop is
        # `while (!quit_requested())`, checking whether
        # pieces/system/quit_flag.txt is non-empty - written by
        # keyboard_input.c on EVERY exit. Without resetting it here, a
        # session ever quit via 'q' before leaves this file non-empty,
        # so the very next launch's own renderer sees
        # quit_requested()==true before its own loop even starts and
        # exits after one frame.
        : > pieces/system/quit_flag.txt
        rm -f pieces/system/gl_focus.lock
        # main_loop_chtpm.pal's own idle-tick gate (pal-standards.txt
        # sec. 19) compares this marker's size against a register it
        # keeps for the life of the process - must start at a known
        # size (empty) every launch, or a stale non-empty file from a
        # previous session would desync the very first comparison.
        : > pieces/display/wsr_screen_changed.txt
        # REAL BUG FIX (user-reported "starts from last open menu"):
        # cursor/digit_accum used to persist ACROSS sessions in
        # projects/wsr-pal/pieces/wsr_menu/state.txt, meaning whichever
        # submenu was showing at the moment of the LAST quit is exactly
        # where the NEXT launch started too. RETIRED ENTIRELY (not just
        # reset) as part of the real href rewrite (pal-standards.txt
        # §18): "which screen is current" is no longer separate mutable
        # state at all - it's derived fresh from chtpm's own real layout
        # file identity every time (see wsr_menu_input.c's own header
        # comment) and the ONLY layout ever launched here is
        # wsr_main_menu.chtpm, so there is no "last open menu" left to
        # accidentally resume - every launch genuinely starts at the
        # real main menu, by construction, not by a reset that has to
        # remember to fire. Only prompt_active (a genuine mid-wizard
        # flag, not a navigation position) still needs resetting, in
        # case a previous session was killed mid-wizard.
        sed -i 's/^prompt_active=.*/prompt_active=0/' \
            "projects/wsr-pal/pieces/wsr_menu/state.txt" 2>/dev/null
        # project_id/active_target_id seeded here so chtpm's own
        # ${piece_methods} (load_dynamic_methods()) can resolve
        # projects/wsr-pal/pieces/wsr_main_menu/piece.pdl on the VERY
        # FIRST frame, before parse_chtm() has run even once (and so
        # before pieces/display/current_layout.txt - what
        # wsr_menu_input.c/wsr_compose_frame.c derive the current screen
        # from on every later call - exists at all).
        cat > pieces/apps/player_app/state.txt << 'EOSTATE'
module_path=system/prisc+x pal/main_loop_chtpm.pal
project_id=wsr-pal
active_target_id=wsr_main_menu
EOSTATE

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="wsr-pal"

        ./system/renderer &
        RENDERER_PID=$!
        ./system/chtpm_parser_pal pieces/chtpm/layouts/wsr_main_menu.chtpm >/dev/null 2>&1 &
        CHTPM_PID=$!

        # chtpm_rgb_render: real wraith_rgb_daemon.c equivalent (see
        # that file's own header comment in shared-ops/) - font-
        # rasterizes current_frame.txt verbatim (chrome AND embedded
        # menu content) into rgb_frame.raw, the SAME path gl_mirror
        # reads. wsr-pal has no map/tiles at all (pure menu text), so,
        # unlike mutaclsym/zoo_0000, this daemon is the ONLY GL
        # renderer this project needs - no separate compose_rgb_frame.c
        # was built (real, deliberate finding, not an oversight - see
        # !.wsr-pal-refactor.txt's own §4 update).
        RGB_PID=""
        if [ -x "system/chtpm_rgb_render" ]; then
            ./system/chtpm_rgb_render >/tmp/wsr_pal_chtpm_rgb_render.log 2>&1 &
            RGB_PID=$!
        fi

        GL_PID=""
        if [ -z "$NO_GL" ] && [ -x "system/gl_mirror" ]; then
            ./system/gl_mirror >/tmp/wsr_pal_gl_mirror.log 2>&1 &
            GL_PID=$!
        fi

        # chtpm_parser_pal has no SIGTERM handler of its own (only
        # SIGINT triggers its own cleanup_module()), so a plain `kill`
        # here would leave its own spawned module (system/prisc+x)
        # orphaned - kill both explicitly, matching the relative-path
        # pkill fix in the "kill" action below.
        trap 'kill "$RENDERER_PID" "$CHTPM_PID" $RGB_PID $GL_PID 2>/dev/null; pkill -f "system/prisc\+x" 2>/dev/null' EXIT INT TERM

        # Clear history.txt right before keyboard_input starts (race
        # condition: if cleared too early, keyboard_input may repopulate
        # with old keys before this point). keyboard_input.c also clears
        # it at its own startup as a second line of defense.
        : > pieces/apps/player_app/history.txt

        ./system/keyboard_input

        kill "$RENDERER_PID" "$CHTPM_PID" $RGB_PID $GL_PID 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        ;;
    kill|k|stop)
        # REAL BUG FIX: this action used to pkill by ABSOLUTE path
        # ("$SCRIPT_DIR/system/...") but "run"/"chtpm" both launch
        # these processes via RELATIVE paths ("./system/...") - the
        # actual process cmdline never contains the absolute prefix
        # this was matching against, so this action never killed
        # anything (confirmed - the exact bug named in
        # !.wsr-pal-refactor.txt §1.5, "almost certainly still exists").
        # Fixed to match the real, relative cmdline, and delegate to
        # the shared, surgical kill_all.sh as the authoritative
        # cleanup - matching mutaclsym's/zoo_0000's own "kill" action.
        pkill -f "system/keyboard_input" 2>/dev/null
        pkill -f "system/renderer" 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        pkill -f "system/chtpm_parser_pal" 2>/dev/null
        pkill -f "system/chtpm_rgb_render" 2>/dev/null
        pkill -f "system/gl_mirror" 2>/dev/null
        bash "$SCRIPT_DIR/../kill_all.sh"
        echo "done"
        ;;
    sim-key)
        # Test the SAME playable interface a human uses, non-
        # interactively - per direct instruction: write a keycode to
        # history.txt (what keyboard_input.c would write), run
        # main_loop.pal for real (backgrounded - it loops forever by
        # design, same as a real session; there's no "process one tick
        # and return" variant, that WOULD be a side-channel shortcut),
        # then read current_frame.txt (what the renderer would draw).
        # Usage: sim-key <keycode> [wait_seconds, default 1]
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="wsr-pal"
        mkdir -p pieces/apps/player_app pieces/display
        echo "$2" >> pieces/apps/player_app/history.txt
        ./system/prisc+x pal/main_loop.pal >/tmp/wsr_simkey.log 2>&1 &
        PID=$!
        sleep "${3:-1}"
        kill "$PID" 2>/dev/null
        wait "$PID" 2>/dev/null
        ;;
    new-game)
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        # Real bug, found TWICE (2026-07-16): first a hardcoded key 51
        # under an older 5-item menu, then a hardcoded key 53 that broke
        # again the moment Trade/Management/Derivatives rows were added
        # and pushed New Game to position 8. Hardcoding a row number
        # here fights the entire point of piece.pdl-driven menus (adding
        # a menu option shouldn't require finding and fixing every
        # hardcoded digit elsewhere) - fixed for real this time by
        # resolving NEW_GAME's row number FROM the piece.pdl itself, the
        # same source of truth the menu rendering already uses.
        new_game_row=$(grep -n '^METHOD' projects/wsr-pal/pieces/wsr_main_menu/piece.pdl | grep 'START_WIZARD:new_game' | head -1 | cut -d: -f1)
        if [ -z "$new_game_row" ]; then
            echo "Could not find New Game row in wsr_main_menu/piece.pdl - aborting." >&2
            exit 1
        fi
        # wsr_menu_input.c derives "which screen" from
        # pieces/display/current_layout.txt (pal-standards.txt §18), not
        # from any field this action sets directly - force it to name
        # wsr_main_menu here so a direct, non-chtpm CLI invocation
        # resolves the same piece.pdl new_game_row was just computed
        # against, regardless of whatever a previous "run" session left
        # this file pointing at.
        mkdir -p pieces/display
        echo "pieces/chtpm/layouts/wsr_main_menu.chtpm" > pieces/display/current_layout.txt
        ./ops/+x/wsr_menu_input.+x "$((48 + new_game_row))"
        ./ops/+x/wsr_menu_input.+x 10
        echo "new game started (world reset from pieces_template)"
        ;;
    tick-all|ta)
        cd "$SCRIPT_DIR"
        bash scripts/ensure_entities.sh
        bash scripts/tick_all.sh "${2:-1}"
        ;;
    test-tick|test-run)
        cd "$SCRIPT_DIR"
        bash scripts/ensure_entities.sh
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="wsr-pal"
        ./system/prisc+x pal/single_tick.pal
        cat projects/wsr-pal/pieces/corp_ORB/state.txt
        ;;
    test-choose)
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        ./ops/+x/corp_set_human_decision.+x corp_ORB "$2"
        ;;
    reset)
        cd "$SCRIPT_DIR"
        printf "current_state=0\ndecision_mode=1\ncash=183.44\nstock_price=167.70\nbook_value=1740.05\nshares_outstanding=11.24\nmarket_cap=1884.31\ndebt_to_equity=0.08\nrisk_bias=9\nshares_held=0\npending_action=\nlast_action=\nhuman_decision=\n" > projects/wsr-pal/pieces/corp_ORB/state.txt
        echo "corp_ORB reset (real seed data: Orbital Express / ORB)."
        ;;
    check|verify)
        for b in system/prisc+x system/keyboard_input system/renderer \
                 ops/+x/corp_tick_idle.+x ops/+x/corp_decide.+x ops/+x/corp_trade.+x \
                 ops/+x/wsr_menu_input.+x ops/+x/wsr_compose_frame.+x \
                 ops/+x/connect_op.+x ops/+x/json_parser.+x; do
            if [ -x "$SCRIPT_DIR/$b" ]; then echo "OK   $b"; else echo "MISSING $b"; fi
        done
        ;;
    help|h|-h|--help)
        echo "wsr-pal button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo "  compile, c, build   - Build prisc+x + ops"
        echo "  run, r              - THE REAL PLAYABLE GAME (interactive, needs a real terminal)"
        echo "  kill, k, stop       - Kill any lingering wsr-pal processes"
        echo "  sim-key <code>      - Test the playable interface non-interactively (see header comment)"
        echo "  new-game            - Reset the world to its initial 57-entity state"
        echo "  tick-all [rounds]   - Advance every entity N rounds, non-interactive batch"
        echo "  test-tick/test-choose/reset - old single-corp-ORB CLI test conveniences"
        echo "  check, verify       - Verify all binaries exist"
        echo "  help, h             - Show this help"
        ;;
    *)
        echo "Unknown action: $ACTION"
        exit 1
        ;;
esac
