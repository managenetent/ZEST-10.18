/* project_browser - one verb, one binary, no shared headers.
 * Menu-navigation half of the editor: title -> projects -> project_menu
 * -> pieces -> piece_detail, one unified digit-accumulator + Enter-
 * commit input handler across those five screens - same shape as
 * egg-pals' ops/menu_input.c. The SIXTH screen, map_edit, has genuinely
 * different input semantics (continuous cursor movement + glyph-arming
 * + place-and-save, not digit-accumulator menu navigation) and is owned
 * by ops/map_edit_input.c instead - this op explicitly no-ops whenever
 * screen=="map_edit", same self-filtering-by-screen convention
 * mutaclsym's own move_player.c/choice.c already use (both run every
 * tick, each ignores keys that aren't theirs).
 *
 * State persisted in pieces/system/editor_state.txt: screen, cursor,
 * digit_accum, the currently-selected project's name/path/map info,
 * and (once drilled into a project) the currently-selected piece's
 * piece.pdl path - each keypress here is a fresh short-lived process,
 * so all of this has to live in a file, not memory.
 *
 * Screens this op owns:
 *   title        (1 option):  "Open Project" -> projects
 *   projects     (N options): one row per pieces/registry/
 *                known_projects.txt entry -> project_menu, storing
 *                that project's path/map_rel_path/registry_format/
 *                registry_rel_path
 *   project_menu (2 options): "Browse Pieces" -> pieces;
 *                "Edit Map" -> map_edit (map_edit_input.c takes over
 *                cursor_x/y and armed_idx setup from here)
 *   pieces       (N options): one row per piece.pdl found under
 *                <project_path>/pieces/ (found via `find`, popen'd) ->
 *                piece_detail, storing that piece's piece.pdl path
 *   piece_detail (1 option, "Back"): read-only - ops/compose_title_frame.c
 *                renders the METHOD table via the shared
 *                ops/+x/pdl_reader.+x (list_methods_full)
 *
 * Usage: project_browser.+x <keycode> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_ROWS 64

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

static void state_path(char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s/pieces/system/editor_state.txt", project_root);
}

typedef struct {
    char screen[16];
    int cursor, digit_accum;
    char proj_name[64], proj_path[PATH_BUF];
    char map_rel_path[256], registry_format[16], registry_rel_path[256];
    char piece_pdl_path[PATH_BUF];
    int cursor_x, cursor_y, armed_idx;
} EditorState;

static void load_state(EditorState *st) {
    snprintf(st->screen, sizeof(st->screen), "title");
    st->cursor = 1;
    st->digit_accum = 0;
    st->proj_name[0] = '\0';
    st->proj_path[0] = '\0';
    st->map_rel_path[0] = '\0';
    st->registry_format[0] = '\0';
    st->registry_rel_path[0] = '\0';
    st->piece_pdl_path[0] = '\0';
    st->cursor_x = 0;
    st->cursor_y = 0;
    st->armed_idx = 0;

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

/* id|display_name|path|map_rel_path|registry_format|registry_rel_path
 * rows from pieces/registry/known_projects.txt - see that file's own
 * header for why every field is explicit rather than guessed from
 * convention. */
static int load_projects(char names[][64], char paths[][PATH_BUF],
                          char map_rels[][256], char reg_fmts[][16], char reg_rels[][256], int max_rows) {
    char reg_path[PATH_BUF];
    snprintf(reg_path, sizeof(reg_path), "%s/pieces/registry/known_projects.txt", project_root);
    FILE *f = fopen(reg_path, "r");
    if (!f) return 0;
    int n = 0;
    char line[MAX_LINE];
    while (n < max_rows && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = '\0';
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        *p2 = '\0';
        char *p3 = strchr(p2 + 1, '|');
        if (!p3) continue;
        *p3 = '\0';
        char *p4 = strchr(p3 + 1, '|');
        if (!p4) continue;
        *p4 = '\0';
        char *p5 = strchr(p4 + 1, '|');
        if (!p5) continue;
        *p5 = '\0';
        snprintf(names[n], 64, "%s", p1 + 1);
        snprintf(paths[n], PATH_BUF, "%s", p2 + 1);
        snprintf(map_rels[n], 256, "%s", p3 + 1);
        snprintf(reg_fmts[n], 16, "%s", p4 + 1);
        snprintf(reg_rels[n], 256, "%s", p5 + 1);
        n++;
    }
    fclose(f);
    return n;
}

/* One row per piece.pdl found under <proj_path>/pieces/ - piece_ids
 * are that file's containing directory name. Shells out to `find`,
 * same directory-walking-via-popen precedent used throughout. */
static int load_pieces(const char *proj_path, char ids[][64], char pdl_paths[][PATH_BUF], int max_rows) {
    char cmd[PATH_BUF + 64];
    snprintf(cmd, sizeof(cmd), "find '%s/pieces' -name piece.pdl 2>/dev/null", proj_path);
    FILE *pf = popen(cmd, "r");
    if (!pf) return 0;
    int n = 0;
    char line[PATH_BUF];
    while (n < max_rows && fgets(line, sizeof(line), pf)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        snprintf(pdl_paths[n], PATH_BUF, "%s", line);
        char *slash = strrchr(line, '/');
        if (slash) *slash = '\0';
        char *id_slash = strrchr(line, '/');
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(ids[n], 64, "%s", id_slash ? id_slash + 1 : line);
#pragma GCC diagnostic pop
        n++;
    }
    pclose(pf);
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int key = atoi(argv[1]);
    resolve_root();

    EditorState st;
    load_state(&st);

    if (strcmp(st.screen, "map_edit") == 0) return 0; /* owned by map_edit_input.c */

    char row_names[MAX_ROWS][64];
    char row_paths[MAX_ROWS][PATH_BUF];
    char row_map_rels[MAX_ROWS][256];
    char row_reg_fmts[MAX_ROWS][16];
    char row_reg_rels[MAX_ROWS][256];
    int count = 0;
    if (strcmp(st.screen, "title") == 0) count = 1;
    else if (strcmp(st.screen, "projects") == 0)
        count = load_projects(row_names, row_paths, row_map_rels, row_reg_fmts, row_reg_rels, MAX_ROWS);
    else if (strcmp(st.screen, "project_menu") == 0) count = 2;
    else if (strcmp(st.screen, "pieces") == 0) count = load_pieces(st.proj_path, row_names, row_paths, MAX_ROWS);
    else if (strcmp(st.screen, "piece_detail") == 0) count = 1;
    if (count < 1) count = 1;

    int is_digit = (key >= '0' && key <= '9');
    int is_enter = (key == 10 || key == 13);

    if (is_digit) {
        int d = key - '0';
        int new_val = st.digit_accum * 10 + d;
        if (new_val >= 1 && new_val <= count) { st.digit_accum = new_val; st.cursor = new_val; }
        else if (d >= 1 && d <= count) { st.digit_accum = d; st.cursor = d; }
        else st.digit_accum = 0;
    } else if (is_enter) {
        if (strcmp(st.screen, "title") == 0) {
            snprintf(st.screen, sizeof(st.screen), "projects");
            st.cursor = 1;
        } else if (strcmp(st.screen, "projects") == 0) {
            if (st.cursor >= 1 && st.cursor <= count) {
                int i = st.cursor - 1;
                snprintf(st.proj_name, sizeof(st.proj_name), "%s", row_names[i]);
                snprintf(st.proj_path, sizeof(st.proj_path), "%s", row_paths[i]);
                snprintf(st.map_rel_path, sizeof(st.map_rel_path), "%s", row_map_rels[i]);
                snprintf(st.registry_format, sizeof(st.registry_format), "%s", row_reg_fmts[i]);
                snprintf(st.registry_rel_path, sizeof(st.registry_rel_path), "%s", row_reg_rels[i]);
                snprintf(st.screen, sizeof(st.screen), "project_menu");
                st.cursor = 1;
            }
        } else if (strcmp(st.screen, "project_menu") == 0) {
            if (st.cursor == 1) {
                snprintf(st.screen, sizeof(st.screen), "pieces");
                st.cursor = 1;
            } else if (st.cursor == 2) {
                snprintf(st.screen, sizeof(st.screen), "map_edit");
                st.cursor_x = 0;
                st.cursor_y = 0;
                st.armed_idx = 0;
            } else {
                snprintf(st.screen, sizeof(st.screen), "projects");
                st.cursor = 1;
            }
        } else if (strcmp(st.screen, "pieces") == 0) {
            if (st.cursor >= 1 && st.cursor <= count) {
                snprintf(st.piece_pdl_path, sizeof(st.piece_pdl_path), "%s", row_paths[st.cursor - 1]);
                snprintf(st.screen, sizeof(st.screen), "piece_detail");
                st.cursor = 1;
            } else {
                snprintf(st.screen, sizeof(st.screen), "project_menu");
                st.cursor = 1;
            }
        } else if (strcmp(st.screen, "piece_detail") == 0) {
            snprintf(st.screen, sizeof(st.screen), "pieces");
            st.cursor = 1;
        }
        st.digit_accum = 0;
    } else {
        st.digit_accum = 0;
    }

    save_state(&st);
    return 0;
}
