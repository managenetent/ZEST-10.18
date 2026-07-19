/* buffer_key - the one op mutaclsym never needed: accumulate free text.
 * mutaclsym only ever dispatches single keystrokes directly as a verb
 * (wasd, a digit, Enter). A chat message is many keystrokes typed before
 * Enter commits it, so main_loop.pal calls this on every keycode that
 * isn't Enter/quit/a y-or-n-while-pending-tool answer, feeding it into a
 * persisted input_buffer field the same way hero/state.txt persists
 * action_cursor/digit_accum across choice.c's own single-shot invocations.
 *
 * Printable ASCII (32-126) appends. Backspace (8 or 127) drops the last
 * character. Anything else (arrows, stray control bytes) is a no-op -
 * this op still runs and re-writes state.txt unchanged rather than
 * erroring, matching choice.c's "always exits 0, silently ignores keys
 * it doesn't handle" convention.
 *
 * Self-contained: own root resolution, own constants, no shared headers.
 * Usage: buffer_key.+x <keycode> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_LINE 512
#define MAX_BUFFER 2048

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int key = atoi(argv[1]);
    resolve_root();

    char state_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/pieces/world_01/session_01/chat/state.txt", project_root);

    FILE *f = fopen(state_path, "r");
    if (!f) return 1;

    char lines[64][MAX_LINE];
    int nlines = 0;
    char buffer[MAX_BUFFER] = "";
    while (nlines < 64 && fgets(lines[nlines], MAX_LINE, f)) {
        if (strncmp(lines[nlines], "input_buffer=", 13) == 0) {
            char *v = lines[nlines] + 13;
            v[strcspn(v, "\n")] = '\0';
            snprintf(buffer, sizeof(buffer), "%s", v);
        }
        nlines++;
    }
    fclose(f);

    size_t len = strlen(buffer);
    if (key == 8 || key == 127) {
        if (len > 0) buffer[len - 1] = '\0';
    } else if (key >= 32 && key <= 126) {
        if (len + 1 < sizeof(buffer)) {
            buffer[len] = (char)key;
            buffer[len + 1] = '\0';
        }
    }
    /* else: arrow keys, other control bytes - no-op */

    f = fopen(state_path, "w");
    if (!f) return 1;
    int found = 0;
    for (int i = 0; i < nlines; i++) {
        if (strncmp(lines[i], "input_buffer=", 13) == 0) {
            fprintf(f, "input_buffer=%s\n", buffer);
            found = 1;
        } else {
            fputs(lines[i], f);
        }
    }
    if (!found) fprintf(f, "input_buffer=%s\n", buffer);
    fclose(f);

    return 0;
}
