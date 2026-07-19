/* keyboard_input - standalone raw-terminal input capture, no ncurses.
 * Mirrors the real TPMOS pattern (pieces/system/input_dispatcher/plugins/
 * input_capture.c): puts STDIN into raw termios mode directly, decodes
 * "ESC [ A/B/C/D" arrow sequences itself, and appends bare decimal
 * keycodes to pieces/apps/player_app/history.txt - one int per line,
 * matching what prisc+x's read_history opcode already expects (it
 * fseeks to a byte cursor and fscanf("%d", ...), so this file must
 * stay bare-decimal, not the timestamped "[ts] KEY_PRESSED: N" format
 * some other TPMOS subsystems use).
 *
 * Self-contained: own root resolution, own constants, no shared headers.
 * Exits (and restores the terminal) on Ctrl+C (ETX, keycode 3) - the quit
 * signal is written to history.txt first, so prisc+x's pal script sees
 * it and halts itself independently. This process additionally drops
 * a byte in pieces/system/quit_flag.txt on exit so the renderer knows
 * to stop polling.
 *
 * NOT switched onto shared-ops/keyboard_input.c (see this family's own
 * pal-standards.txt §1c on when NOT to force-share): that file
 * normalizes Ctrl+C/ETX to 'q' before appending, and quits on 'q';
 * this file appends the raw ETX byte (3) and quits directly on 3 - a
 * real, existing behavioral difference (wsr-pal's own pal script
 * presumably keys off keycode 3, not 113), not just a style choice,
 * so this file stays its own local copy.
 *
 * CHTPM-BRIDGE ADDITION (see chtpm-to-pal-layout-plan.txt and
 * shared-ops/chtpm_parser_pal.c's own header comment for the full
 * why): append_key() now ALSO appends "KEY_PRESSED: N\n" to
 * pieces/keyboard/history.txt - real 1.TPMOS's own chtpm_parser.c
 * reads that exact file in that exact format, completely unmodified.
 * Mirrors the identical addition already made to shared-ops/
 * keyboard_input.c for mutaclsym/zoo_0000/muchipal-editor-0.0. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>

#define MAX_PATH 4096
/* Room for MAX_PATH worth of project_root plus the longest relative
 * suffix this file appends, so gcc can prove snprintf can't truncate. */
#define PATH_BUF (MAX_PATH + 256)

#define ARROW_LEFT  1000
#define ARROW_RIGHT 1001
#define ARROW_UP    1002
#define ARROW_DOWN  1003

static char project_root[MAX_PATH] = ".";
static struct termios orig_term;

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) { snprintf(project_root, sizeof(project_root), "%s", env); return; }
    if (!getcwd(project_root, sizeof(project_root))) snprintf(project_root, sizeof(project_root), ".");
}

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
}

static void write_quit_flag(void);

static void handle_signal(int sig) {
    /* Defensive fallback for a real external signal (e.g. `kill -TERM`
     * from outside, or `button.sh kill`). Ctrl+C itself does NOT reach
     * here in the normal case - see the ETX check in main()'s loop and
     * the comment on enable_raw_mode() below for why. */
    (void)sig;
    disable_raw_mode();
    write_quit_flag();
    _exit(0);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_term);
    atexit(disable_raw_mode);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    struct termios raw = orig_term;
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (CS8);
    /* ISIG cleared means the tty line discipline stops turning Ctrl+C
     * into SIGINT - it arrives as ordinary input, byte 0x03 (ETX), like
     * any other keystroke. handle_signal() above is only a fallback for
     * a real external signal; Ctrl+C itself must be caught as data in
     * main()'s read loop, same as real TPMOS's input_capture.c does. */
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static int read_key(void) {
    char c;
    int nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) return -1;
    }
    if (c == '\x1b') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return (unsigned char)c;
}

static void append_key(int key) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/apps/player_app/history.txt", project_root);
    FILE *f = fopen(path, "a");
    if (f) { fprintf(f, "%d\n", key); fclose(f); }

    /* CHTPM-BRIDGE ADDITION - see this file's own top-of-file comment.
     * A second, differently-formatted line for chtpm_parser_pal.c's
     * own unmodified history-reading code, which expects real 1.TPMOS's
     * native "KEY_PRESSED: N" shape. Never read by prisc+x's own
     * OP_READ_HISTORY (a different file), so this can't collide with
     * the write above. */
    char chtpm_path[PATH_BUF];
    snprintf(chtpm_path, sizeof(chtpm_path), "%s/pieces/keyboard/history.txt", project_root);
    FILE *cf = fopen(chtpm_path, "a");
    if (cf) { fprintf(cf, "KEY_PRESSED: %d\n", key); fclose(cf); }
}

static void write_quit_flag(void) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/system/quit_flag.txt", project_root);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("1", f);
    fclose(f);
}

int main(void) {
    resolve_root();

    /* Clear history.txt on startup - avoids replaying old keys from previous
     * session (race condition: shell-script clearing is not reliable). Truncate
     * the file to ensure fresh input stream for this session. */
    char history_path[PATH_BUF];
    snprintf(history_path, sizeof(history_path), "%s/pieces/apps/player_app/history.txt", project_root);
    FILE *hf = fopen(history_path, "w");
    if (hf) fclose(hf);

    enable_raw_mode();

    for (;;) {
        int key = read_key();
        if (key == -1) continue;
        append_key(key);
        if (key == 3) break; /* Ctrl+C (ETX) - see enable_raw_mode() */
    }

    disable_raw_mode();
    write_quit_flag();
    return 0;
}
