/* map_edit_input - one verb, one binary, no shared headers. Owns the
 * "map_edit" screen exclusively - self-filters (no-ops) whenever
 * editor_state.txt's screen isn't "map_edit", same convention
 * ops/project_browser.c uses in reverse (it no-ops FOR this screen).
 * Genuinely different input shape from the rest of the editor:
 * continuous cursor movement + glyph-arming + place-and-save, not
 * digit-accumulator menu navigation - real 1.TPMOS's own
 * wraith_project_input.c/piececraft-3d_manager.c draw exactly this
 * same line (a dedicated "map control" input mode, ESC to leave it),
 * so that's the precedent followed here, not invented fresh.
 *
 * Reads the CURRENT project's map + registry using whichever of the
 * two real, differently-shaped formats this family has produced so
 * far (known_projects.txt's registry_format field, set by
 * project_browser.c when a project is opened - never guessed):
 *   pipe   - mutaclsym's terrain_types.txt: one file,
 *            "glyph|id|name|walkable|rgb_top" per row.
 *   equals - piececraft-3d-pal's registry.txt: "glyph=id" per row (no
 *            per-glyph label beyond the id itself - good enough for a
 *            numbered legend).
 * This is the actual test of "compatible ops across differently-shaped
 * real content" the whole muchi-verse effort has been aiming at - one
 * editor screen, two real projects, two real tile-registry shapes.
 *
 * Controls: arrows move the cursor; digits 1-9 arm which registry row
 * to place; Enter places the armed glyph at the cursor AND SAVES
 * map.txt immediately (no separate "save" step - matches this
 * family's existing "every edit persists to disk right away" norm,
 * e.g. mutaclsym's save_game.c, keyboard_input.c's history.txt); ESC
 * (27) leaves map_edit back to project_menu.
 *
 * Usage: map_edit_input.+x <keycode> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_MAP_W 128
#define MAX_MAP_H 64
#define MAX_REG_ROWS 32

#define ARROW_LEFT 1000
#define ARROW_RIGHT 1001
#define ARROW_UP 1002
#define ARROW_DOWN 1003

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

typedef struct {
    char screen[16];
    int cursor, digit_accum;
    char proj_name[64], proj_path[PATH_BUF];
    char map_rel_path[256], registry_format[16], registry_rel_path[256];
    char piece_pdl_path[PATH_BUF];
    int cursor_x, cursor_y, armed_idx;
} EditorState;

static void state_path(char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s/pieces/system/editor_state.txt", project_root);
}

static void load_state(EditorState *st) {
    memset(st, 0, sizeof(*st));
    snprintf(st->screen, sizeof(st->screen), "title");
    char path[PATH_BUF];
    state_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if (strcmp(key, "screen") == 0) snprintf(st->screen, sizeof(st->screen), "%s", val);
        else if (strcmp(key, "cursor") == 0) st->cursor = atoi(val);
        else if (strcmp(key, "digit_accum") == 0) st->digit_accum = atoi(val);
        else if (strcmp(key, "proj_name") == 0) snprintf(st->proj_name, sizeof(st->proj_name), "%s", val);
        else if (strcmp(key, "proj_path") == 0) snprintf(st->proj_path, sizeof(st->proj_path), "%s", val);
        else if (strcmp(key, "map_rel_path") == 0) snprintf(st->map_rel_path, sizeof(st->map_rel_path), "%s", val);
        else if (strcmp(key, "registry_format") == 0) snprintf(st->registry_format, sizeof(st->registry_format), "%s", val);
        else if (strcmp(key, "registry_rel_path") == 0) snprintf(st->registry_rel_path, sizeof(st->registry_rel_path), "%s", val);
        else if (strcmp(key, "piece_pdl_path") == 0) snprintf(st->piece_pdl_path, sizeof(st->piece_pdl_path), "%s", val);
        else if (strcmp(key, "cursor_x") == 0) st->cursor_x = atoi(val);
        else if (strcmp(key, "cursor_y") == 0) st->cursor_y = atoi(val);
        else if (strcmp(key, "armed_idx") == 0) st->armed_idx = atoi(val);
    }
    fclose(f);
}

static void save_state(const EditorState *st) {
    char path[PATH_BUF];
    state_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "screen=%s\n", st->screen);
    fprintf(f, "cursor=%d\n", st->cursor);
    fprintf(f, "digit_accum=%d\n", st->digit_accum);
    fprintf(f, "proj_name=%s\n", st->proj_name);
    fprintf(f, "proj_path=%s\n", st->proj_path);
    fprintf(f, "map_rel_path=%s\n", st->map_rel_path);
    fprintf(f, "registry_format=%s\n", st->registry_format);
    fprintf(f, "registry_rel_path=%s\n", st->registry_rel_path);
    fprintf(f, "piece_pdl_path=%s\n", st->piece_pdl_path);
    fprintf(f, "cursor_x=%d\n", st->cursor_x);
    fprintf(f, "cursor_y=%d\n", st->cursor_y);
    fprintf(f, "armed_idx=%d\n", st->armed_idx);
    fclose(f);
}

/* Loads glyph+label rows from either real registry format - see this
 * file's header. reg_labels rows are "glyph name" (pipe format's real
 * name field) or "glyph id" (equals format has no separate label). */
static int load_registry_rows(const char *proj_path, const char *registry_rel_path,
                               const char *format, char glyphs[MAX_REG_ROWS], char labels[MAX_REG_ROWS][64]) {
    char path[PATH_BUF + 256];
    snprintf(path, sizeof(path), "%s/%s", proj_path, registry_rel_path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    char line[MAX_LINE];
    int is_pipe = (strcmp(format, "pipe") == 0);
    while (n < MAX_REG_ROWS && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        char sep = is_pipe ? '|' : '=';
        /* A real comment line here always has a SPACE right after '#'
         * (both registries' own header rows are "# free text"); a real
         * data row has the separator character immediately after the
         * one-char glyph - checking for that, not just line[0]=='#',
         * is what correctly keeps '#' itself usable as a glyph (both
         * mutaclsym's t_wall and piececraft's wall row do exactly
         * this) - see GRAND-ARCHITECTURE.md §0b/§0c for the two prior
         * times this exact bug was found and fixed elsewhere. */
        if (line[0] == '#' && line[1] != sep) continue;
        if (line[1] != sep) continue;
        glyphs[n] = line[0];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(labels[n], 64, "%s", line + 2);
#pragma GCC diagnostic pop
        n++;
    }
    fclose(f);
    return n;
}

static int load_map(const char *proj_path, const char *map_rel_path,
                     char grid[MAX_MAP_H][MAX_MAP_W + 1], int *width) {
    char path[PATH_BUF + 256];
    snprintf(path, sizeof(path), "%s/%s", proj_path, map_rel_path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int rows = 0;
    *width = 0;
    char line[MAX_MAP_W + 4];
    while (rows < MAX_MAP_H && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(grid[rows], sizeof(grid[0]), "%s", line);
#pragma GCC diagnostic pop
        int len = (int)strlen(grid[rows]);
        if (len > *width) *width = len;
        rows++;
    }
    fclose(f);
    return rows;
}

static void save_map(const char *proj_path, const char *map_rel_path,
                      char grid[MAX_MAP_H][MAX_MAP_W + 1], int rows) {
    char path[PATH_BUF + 256], tmp[PATH_BUF + 264];
    snprintf(path, sizeof(path), "%s/%s", proj_path, map_rel_path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int r = 0; r < rows; r++) fprintf(f, "%s\n", grid[r]);
    fclose(f);
    rename(tmp, path);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int key = atoi(argv[1]);
    resolve_root();

    EditorState st;
    load_state(&st);
    if (strcmp(st.screen, "map_edit") != 0) return 0; /* owned by project_browser.c */

    char grid[MAX_MAP_H][MAX_MAP_W + 1];
    int width = 0;
    int rows = load_map(st.proj_path, st.map_rel_path, grid, &width);
    if (rows < 1) rows = 1;
    if (width < 1) width = 1;

    char reg_glyphs[MAX_REG_ROWS];
    char reg_labels[MAX_REG_ROWS][64];
    int reg_count = load_registry_rows(st.proj_path, st.registry_rel_path, st.registry_format, reg_glyphs, reg_labels);

    if (key == ARROW_LEFT) { if (st.cursor_x > 0) st.cursor_x--; }
    else if (key == ARROW_RIGHT) { if (st.cursor_x < width - 1) st.cursor_x++; }
    else if (key == ARROW_UP) { if (st.cursor_y > 0) st.cursor_y--; }
    else if (key == ARROW_DOWN) { if (st.cursor_y < rows - 1) st.cursor_y++; }
    else if (key >= '1' && key <= '9') {
        int idx = key - '1';
        if (idx < reg_count) st.armed_idx = idx;
    } else if (key == 10 || key == 13) {
        if (reg_count > 0 && st.armed_idx < reg_count &&
            st.cursor_y >= 0 && st.cursor_y < rows &&
            st.cursor_x >= 0 && st.cursor_x < (int)strlen(grid[st.cursor_y])) {
            grid[st.cursor_y][st.cursor_x] = reg_glyphs[st.armed_idx];
            save_map(st.proj_path, st.map_rel_path, grid, rows);
        }
    } else if (key == 27) {
        snprintf(st.screen, sizeof(st.screen), "project_menu");
        st.cursor = 1;
        st.digit_accum = 0;
    }

    save_state(&st);
    return 0;
}
