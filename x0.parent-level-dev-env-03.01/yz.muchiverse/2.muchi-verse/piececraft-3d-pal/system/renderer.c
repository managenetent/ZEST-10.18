/* renderer - standalone frame display, no ncurses.
 * Mirrors the real TPMOS pattern (pieces/display/renderer.c): polls a
 * pulse marker file's SIZE (never mtime), and on growth, clears the
 * screen with a plain ANSI escape and prints pieces/display/
 * current_frame.txt to stdout - a normal cooked-mode terminal write,
 * no raw mode needed here (only keyboard_input needs raw mode).
 *
 * Every frame it draws is also appended, with a timestamp, to
 * pieces/display/frame_history.txt - an auditable, plain-text log of
 * every frame the game has ever shown, so a frame can be verified by
 * reading a file instead of driving a pty.
 *
 * Self-contained: own root resolution, own constants, no shared
 * headers. Exits when pieces/system/quit_flag.txt becomes non-empty
 * (written by keyboard_input on 'q'). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define MAX_PATH 4096
/* Room for MAX_PATH worth of project_root plus the longest relative
 * suffix this file appends, so gcc can prove snprintf can't truncate
 * (silences -Wformat-truncation instead of just being right by luck). */
#define PATH_BUF (MAX_PATH + 256)

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) { snprintf(project_root, sizeof(project_root), "%s", env); return; }
    if (!getcwd(project_root, sizeof(project_root))) snprintf(project_root, sizeof(project_root), ".");
}

static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

/* Write content to the terminal translating '\n' -> "\r\n" ourselves.
 * keyboard_input puts the shared tty into raw mode with OPOST cleared
 * (needed so it can read raw keys), and OPOST is a per-terminal-device
 * setting, not per-process - so this process cannot rely on the tty
 * auto-translating our bare newlines into carriage returns anymore.
 * Emitting \r\n explicitly makes rendering correct regardless of what
 * termios state any other process left the shared tty in. */
static void write_crlf(const char *s, FILE *out) {
    for (const char *p = s; *p; p++) {
        if (*p == '\n') fputc('\r', out);
        fputc(*p, out);
    }
}

static void render_frame(void) {
    char frame_path[PATH_BUF], history_path[PATH_BUF];
    snprintf(frame_path, sizeof(frame_path), "%s/pieces/display/current_frame.txt", project_root);
    snprintf(history_path, sizeof(history_path), "%s/pieces/display/frame_history.txt", project_root);

    FILE *f = fopen(frame_path, "r");
    if (!f) return;
    char content[8192];
    size_t n = fread(content, 1, sizeof(content) - 1, f);
    content[n] = '\0';
    fclose(f);

    /* Plain ANSI clear + cursor-home, no ncurses. */
    fputs("\033[H\033[J", stdout);
    write_crlf(content, stdout);
    fflush(stdout);

    time_t rawtime = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
    FILE *hf = fopen(history_path, "a");
    if (hf) {
        fprintf(hf, "\n--- FRAME at %s ---\n%s\n", timestamp, content);
        fclose(hf);
    }
}

static int quit_requested(void) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/system/quit_flag.txt", project_root);
    return file_size(path) > 0;
}

int main(void) {
    resolve_root();

    char pulse_path[PATH_BUF];
    snprintf(pulse_path, sizeof(pulse_path), "%s/pieces/display/frame_changed.txt", project_root);

    /* Clear the frame history log at the start of a new session, same
     * as TPMOS's renderer does - prevents old runs' frames piling up
     * under a fresh game's log. */
    char history_path[PATH_BUF];
    snprintf(history_path, sizeof(history_path), "%s/pieces/display/frame_history.txt", project_root);
    FILE *clear = fopen(history_path, "w");
    if (clear) fclose(clear);

    render_frame();
    long last_marker = file_size(pulse_path);

    while (!quit_requested()) {
        long m = file_size(pulse_path);
        if (m != last_marker) {
            last_marker = m;
            render_frame();
        }
        usleep(16667);
    }
    return 0;
}
