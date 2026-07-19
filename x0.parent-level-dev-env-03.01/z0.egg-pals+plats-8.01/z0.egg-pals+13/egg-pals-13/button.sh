#!/bin/bash
# button.sh - launcher for egg-pals, same verb convention as mutaclsym's
# button.sh (c/compile, r/run, k/kill...).
#
# Status: the terminal menu loop is real and working (User/Faucet/Store/
# Pets, same three-process shape as mutaclsym: keyboard_input + prisc+x/
# pal + renderer, no ncurses). Hatched pets now have a per-pet detail
# screen (Feed/Clean/Sleep/Train/Export Card/Destroy Card/Open Window)
# and metabolism ticks on their own via pal/main_loop.pal's epoch dispatch
# to tick_pets.+x. Run `./button.sh icons` once before opening a pet's GL
# window if you want the poop/sleep status overlays to render.
ACTION="${1:-help}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Only system/* binaries ever get a .exe sibling - gcc appends .exe
# itself to an -o name with no dot in it (system/renderer -> both
# system/renderer and system/renderer.exe can exist side by side after
# building on different platforms at different times); ops/+x/*.+x names
# already contain a dot, so Windows builds land directly in that same
# *.+x file, never a separate *.+x.exe.
#
# This tree can carry a stale prebuilt binary from *either* platform
# after being packaged/copied around (e.g. a Windows .exe left sitting
# next to a freshly-built native Linux/Mac binary, or vice versa) - the
# previous version of this helper always preferred .exe whenever one
# existed, which "worked" for the Windows side of that but broke the
# Linux/Mac side the moment a stale .exe was also present (exactly what
# produced "Exec format error" here). Detect the actual OS instead of
# guessing from which files happen to exist.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
    *) EXE="" ;;
esac
bin_path() {
    echo "$SCRIPT_DIR/$1$EXE"
}

case "$ACTION" in
    compile|c|build)
        bash "$SCRIPT_DIR/scripts/build.sh"
        ;;
    check|verify)
        for b in system/prisc+x system/emoji_gen_atlas system/emoji_xtract system/egg_window \
                 system/keyboard_input system/renderer system/chtpm_parser_pal \
                 ops/+x/generate_egg.+x ops/+x/claim_tokens.+x ops/+x/coin_flip.+x \
                 ops/+x/buy_egg.+x ops/+x/hatch_egg.+x ops/+x/menu_input.+x ops/+x/compose_menu.+x \
                 ops/+x/tick_pets.+x ops/+x/feed_pet.+x ops/+x/clean_pet.+x ops/+x/toggle_sleep.+x \
                 ops/+x/train_pet.+x ops/+x/export_card.+x ops/+x/destroy_card.+x \
                 ops/+x/list_processes.+x; do
            # ops/+x/*.+x names already have a dot, so they're never
            # renamed to *.+x.exe even on Windows - only system/* gets a
            # platform-dependent suffix (see bin_path's own comment above).
            case "$b" in
                system/*) check_path="$SCRIPT_DIR/$b$EXE" ;;
                *) check_path="$SCRIPT_DIR/$b" ;;
            esac
            if [ -x "$check_path" ]; then
                echo "OK   $b"
            else
                echo "MISSING $b"
            fi
        done
        ;;
    demo|d)
        cd "$SCRIPT_DIR"
        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        echo "=== Minting an egg for user_01 (bypasses the store's token cost) ==="
        EGG_ID=$(./ops/+x/generate_egg.+x user_01)
        echo "Minted: $EGG_ID"
        echo "--- state.txt ---"
        cat "pieces/world_01/map_lobby/$EGG_ID/state.txt"
        echo "--- user_01 inventory.txt ---"
        cat "pieces/world_01/map_lobby/user_01/inventory.txt"
        ;;
    run|r|start)
        # Real interact+module pattern (see chtpm-to-pal-layout-plan.txt
        # §8 and pal-standards.txt §7) - the primary entry point for
        # running the game. pieces/chtpm/layouts/eggpal.chtpm's own
        # <module>${module_path}</module> tag makes chtpm_parser_pal
        # ITSELF launch system/prisc+x pal/main_loop_chtpm.pal as a
        # separate, parallel, persistent process the instant the layout
        # is parsed. This project has no map/movement concept - digit-menu
        # keys relay through menu_input.c's own dispatch once "Control
        # Pets" is engaged.
        #
        # interact target is pieces/apps/player_app/interact_relay.txt,
        # NOT history.txt: this project's own system/keyboard_input.c
        # writes every real keystroke to pieces/apps/player_app/history.txt
        # UNCONDITIONALLY (a real, deliberate divergence from real TPMOS's
        # own raw capture, which only ever writes to
        # pieces/keyboard/history.txt) - pointing main_loop_chtpm.pal's own
        # read at that same file would double-dispatch every interact-mode
        # keypress relayed by chtpm_parser_pal's own inject_raw_key(), and
        # would leak menu-nav keystrokes into the game module as bogus
        # actions. interact_relay.txt is written ONLY by chtpm_parser_pal's
        # own conditional relay, matching the single-writer guarantee real
        # TPMOS gets for free from its own narrower raw-capture design (see
        # main_loop_chtpm.pal's own header comment for the full write-up).
        cd "$SCRIPT_DIR"
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
        # quit_requested()==true before its own loop even starts - one
        # frame prints, then it exits immediately.
        : > pieces/system/quit_flag.txt
        cat > pieces/apps/player_app/state.txt << 'EOSTATE'
module_path=system/prisc+x pal/main_loop_chtpm.pal
EOSTATE

        export PRISC_PROJECT_ROOT="$SCRIPT_DIR"
        export PRISC_PROJECT_ID="egg-pals"

        "$(bin_path system/renderer)" &
        RENDERER_PID=$!
        "$(bin_path system/chtpm_parser_pal)" pieces/chtpm/layouts/eggpal.chtpm >/dev/null 2>&1 &
        CHTPM_PID=$!
        # Session marker so a floating egg_window (spawned via
        # menu_input.c's spawn_egg_window) can tell when this session
        # ends and close itself instead of being left behind frozen.
        echo "$CHTPM_PID" > pieces/system/session.pid
        # chtpm_parser_pal has no SIGTERM handler of its own (only
        # SIGINT triggers its own cleanup_module()), so a plain `kill`
        # here would leave its own spawned module (system/prisc+x)
        # orphaned - kill both explicitly.
        trap 'kill "$RENDERER_PID" "$CHTPM_PID" 2>/dev/null; pkill -f "system/prisc\+x" 2>/dev/null; rm -f pieces/system/session.pid' EXIT INT TERM

        # Clear history.txt right before keyboard_input starts (race
        # condition: if cleared too early, keyboard_input may repopulate
        # with old keys before this point; clearing here ensures fresh
        # input for this session). keyboard_input.c also clears it at its
        # own startup as a second line of defense.
        : > pieces/apps/player_app/history.txt

        "$(bin_path system/keyboard_input)"

        kill "$RENDERER_PID" "$CHTPM_PID" 2>/dev/null
        pkill -f "system/prisc\+x" 2>/dev/null
        rm -f pieces/system/session.pid
        ;;
    icons)
        bash "$SCRIPT_DIR/scripts/gen_icons.sh"
        ;;
    kill|k|stop)
        echo "=== Killing egg-pals processes ==="
        # pkill isn't installed by default under MSYS2/Git-for-Windows -
        # fall back to matching `ps` output and killing by PID directly
        # when it's missing (still works fine on Linux/Mac if some
        # minimal image happens not to ship pkill either).
        # REAL BUG FIX (found live in muchipal-editor-0.0's own
        # button.sh this same session - see
        # feedback_pkill_relative_path_gotcha.md): run launches every
        # binary via a RELATIVE path (./system/foo, after
        # cd "$SCRIPT_DIR"), so its recorded command line never contains
        # $SCRIPT_DIR at all - matching against the absolute path here
        # (as this action used to) silently never found the process,
        # letting it leak forever. Match the bare relative substring
        # instead.
        kill_matching() {
            if command -v pkill >/dev/null 2>&1; then
                pkill -f "$1" 2>/dev/null
            else
                ps 2>/dev/null | grep -F "$1" | grep -v grep | awk '{print $1}' | while read -r pid; do
                    kill "$pid" 2>/dev/null
                done
            fi
        }
        kill_matching "system/keyboard_input"
        kill_matching "system/renderer"
        kill_matching "system/prisc+x"
        kill_matching "system/chtpm_parser_pal"
        # Per direct instruction ("maybe it should run a kill_all.sh
        # script like 1.tpmos does") after a real orphaned-process
        # incident this session - delegate to the shared, surgical,
        # SIGKILL-based 2.muchi-verse/kill_all.sh (modeled on real
        # 1.TPMOS's own pieces/os/kill_all.sh) too, on platforms where
        # bash+pkill are available (harmless no-op otherwise).
        if command -v pkill >/dev/null 2>&1; then
            bash "$SCRIPT_DIR/../../../yz.muchiverse/2.muchi-verse/kill_all.sh" 2>/dev/null
        fi
        echo "done"
        ;;
    killpets|kp)
        # Deliberately separate from `kill` above: that one only ever
        # touched the terminal-session processes, never the floating pet
        # windows (which are meant to outlive a single menu action) - see
        # scripts/kill_pets.sh's own header for why this exists as a
        # dedicated escape hatch instead of folding it into `kill`.
        sh "$SCRIPT_DIR/scripts/kill_pets.sh"
        ;;
    help|h|-h|--help)
        echo "egg-pals button.sh"
        echo ""
        echo "Usage: ./button.sh <action>"
        echo ""
        echo "Actions:"
        echo "  compile, c, build   - Build all binaries"
        echo "  check, verify       - Verify all binaries exist"
        echo "  demo, d             - Mint one test egg end to end and print its state"
        echo "  run, r, start       - Run the menu (User/Faucet/Store/Pets)"
        echo "  icons               - Pre-render the poop/sleep status-overlay icons (once)"
        echo "  kill, k, stop       - Kill any lingering egg-pals processes"
        echo "  killpets, kp        - Emergency: force-close every floating pet window"
        echo "  help, h             - Show this help"
        ;;
    *)
        echo "Unknown action: $ACTION"
        echo "Run './button.sh help' for usage."
        exit 1
        ;;
esac
