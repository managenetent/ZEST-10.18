/* compose_title_frame - one verb, one binary, no shared headers.
 * Renders whichever of the editor's four screens (title/projects/
 * pieces/piece_detail) is currently active into
 * pieces/display/current_frame.txt - same bracket-cursor numbered-list
 * convention used throughout this whole project family (see
 * nav-refactor-2.txt / mutaclsym's compose_frame.c / egg-pals'
 * compose_menu.c, all cited there). The piece_detail screen is the
 * concrete proof of cross-project compatibility: it calls the SHARED
 * ops/+x/pdl_reader.+x (yz.muchiverse/2.muchi-verse/shared-ops/
 * pdl_reader.c - see that dir's own shared-ops-refactor-plan.txt)
 * with `list_methods_full`, against a REAL piece.pdl absolute path
 * belonging to whatever external project (mutaclsym, later others)
 * the user opened, and displays its actual METHOD table - not mock
 * data. This used to call a private, per-project piece_viewer.+x
 * whose parser was a third independent retyping of pdl_reader.c's own
 * parse_method_line() - retired once pdl_reader.c itself was
 * genericized to accept an absolute path and print the same
 * "name|handler" shape via list_methods_full. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_ROWS 64
#define BOX_W 60

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

static int read_kv_int(const char *path, const char *key, int def) {
    FILE *f = fopen(path, "r");
    if (!f) return def;
    char line[MAX_LINE];
    int val = def;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, key) == 0) { val = atoi(eq + 1); break; }
    }
    fclose(f);
    return val;
}

static void read_kv_str(const char *path, const char *key, char *out, size_t out_sz, const char *def) {
    snprintf(out, out_sz, "%s", def);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, key) == 0) { snprintf(out, out_sz, "%s", eq + 1); break; }
    }
    fclose(f);
}

static void border(FILE *out) {
    fputc('+', out);
    for (int i = 0; i < BOX_W; i++) fputc('=', out);
    fputc('+', out);
    fputc('\n', out);
}

static void line(FILE *out, const char *content) {
    int len = (int)strlen(content);
    if (len > BOX_W) len = BOX_W;
    fprintf(out, "|%.*s", len, content);
    for (int i = len; i < BOX_W; i++) fputc(' ', out);
    fputc('|', out);
    fputc('\n', out);
}

static void blank(FILE *out) { line(out, ""); }

static void title(FILE *out, const char *text) {
    char spaced[BOX_W + 1] = "";
    int p = 0;
    for (const char *c = text; *c && p < BOX_W - 1; c++) {
        spaced[p++] = *c;
        if (c[1]) spaced[p++] = ' ';
    }
    spaced[p] = '\0';
    int pad = (BOX_W - (int)strlen(spaced)) / 2;
    char padded[BOX_W + 1] = "";
    for (int i = 0; i < pad && i < BOX_W; i++) padded[i] = ' ';
    snprintf(padded + (pad > 0 ? pad : 0), sizeof(padded) - (pad > 0 ? pad : 0), "%s", spaced);
    line(out, padded);
}

static void option(FILE *out, int index, int cursor, const char *label) {
    char buf[BOX_W + 1];
    const char *cur = index == cursor ? "[>]" : "[ ]";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(buf, sizeof(buf), "  %s %d. [%s]", cur, index, label);
#pragma GCC diagnostic pop
    line(out, buf);
}

int main(void) {
    resolve_root();

    char state_path[PATH_BUF], out_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/pieces/system/editor_state.txt", project_root);
    snprintf(out_path, sizeof(out_path), "%s/pieces/display/current_frame.txt", project_root);

    char screen[16], proj_name[64], proj_path[PATH_BUF], piece_pdl_path[PATH_BUF];
    read_kv_str(state_path, "screen", screen, sizeof(screen), "title");
    int cursor = read_kv_int(state_path, "cursor", 1);
    read_kv_str(state_path, "proj_name", proj_name, sizeof(proj_name), "");
    read_kv_str(state_path, "proj_path", proj_path, sizeof(proj_path), "");
    read_kv_str(state_path, "piece_pdl_path", piece_pdl_path, sizeof(piece_pdl_path), "");

    FILE *out = fopen(out_path, "w");
    if (!out) return 1;

    border(out);
    blank(out);
    title(out, "MUCHIPAL-EDITOR");
    blank(out);

    if (strcmp(screen, "title") == 0) {
        option(out, 1, cursor, "Open Project");
    } else if (strcmp(screen, "projects") == 0) {
        title(out, "PROJECTS");
        blank(out);
        char reg_path[PATH_BUF];
        snprintf(reg_path, sizeof(reg_path), "%s/pieces/registry/known_projects.txt", project_root);
        FILE *rf = fopen(reg_path, "r");
        int row = 0;
        if (rf) {
            char rline[MAX_LINE];
            while (fgets(rline, sizeof(rline), rf)) {
                if (rline[0] == '#' || rline[0] == '\n') continue;
                rline[strcspn(rline, "\r\n")] = '\0';
                char *p1 = strchr(rline, '|');
                if (!p1) continue;
                char *p2 = strchr(p1 + 1, '|');
                if (!p2) continue;
                *p2 = '\0';
                option(out, row + 1, cursor, p1 + 1);
                row++;
            }
            fclose(rf);
        }
        if (row == 0) line(out, "  (no known projects - edit pieces/registry/known_projects.txt)");
    } else if (strcmp(screen, "project_menu") == 0) {
        char hdr[96];
        snprintf(hdr, sizeof(hdr), "PROJECT: %s", proj_name);
        title(out, hdr);
        blank(out);
        option(out, 1, cursor, "Browse Pieces");
        option(out, 2, cursor, "Edit Map");
    } else if (strcmp(screen, "map_edit") == 0) {
        char hdr[96];
        snprintf(hdr, sizeof(hdr), "MAP EDIT: %s", proj_name);
        title(out, hdr);
        blank(out);

        char map_rel_path[256], registry_format[16], registry_rel_path[256];
        read_kv_str(state_path, "map_rel_path", map_rel_path, sizeof(map_rel_path), "");
        read_kv_str(state_path, "registry_format", registry_format, sizeof(registry_format), "pipe");
        read_kv_str(state_path, "registry_rel_path", registry_rel_path, sizeof(registry_rel_path), "");
        int cursor_x = read_kv_int(state_path, "cursor_x", 0);
        int cursor_y = read_kv_int(state_path, "cursor_y", 0);
        int armed_idx = read_kv_int(state_path, "armed_idx", 0);

        char map_path[PATH_BUF + 256];
        snprintf(map_path, sizeof(map_path), "%s/%s", proj_path, map_rel_path);
        FILE *mf = fopen(map_path, "r");
        int row = 0;
        if (mf) {
            char mline[BOX_W + 8];
            while (row < 24 && fgets(mline, sizeof(mline), mf)) {
                mline[strcspn(mline, "\r\n")] = '\0';
                if (!mline[0]) continue;
                /* Highlight the cursor cell with brackets - only cheap
                 * way to show position in a plain fixed-width text
                 * grid without touching the character underneath. */
                if (row == cursor_y && cursor_x >= 0 && cursor_x < (int)strlen(mline)) {
                    char marked[BOX_W + 8];
                    int len = (int)strlen(mline);
                    int mi = 0;
                    for (int i = 0; i < len && mi < BOX_W - 2; i++) {
                        if (i == cursor_x) { marked[mi++] = '['; marked[mi++] = mline[i]; marked[mi++] = ']'; }
                        else marked[mi++] = mline[i];
                    }
                    marked[mi] = '\0';
                    line(out, marked);
                } else {
                    line(out, mline);
                }
                row++;
            }
            fclose(mf);
        }
        if (row == 0) line(out, "  (map file not found or empty)");

        blank(out);
        char reg_path[PATH_BUF + 256];
        snprintf(reg_path, sizeof(reg_path), "%s/%s", proj_path, registry_rel_path);
        FILE *rf = fopen(reg_path, "r");
        int reg_row = 0;
        int is_pipe = (strcmp(registry_format, "pipe") == 0);
        char sep = is_pipe ? '|' : '=';
        if (rf) {
            char rline[MAX_LINE];
            while (reg_row < 9 && fgets(rline, sizeof(rline), rf)) {
                rline[strcspn(rline, "\r\n")] = '\0';
                if (!rline[0]) continue;
                if (rline[0] == '#' && rline[1] != sep) continue;
                if (rline[1] != sep) continue;
                char legend[BOX_W + 1];
                const char *cur = (reg_row == armed_idx) ? "[ARMED]" : "       ";
                /* pipe format's remainder is "id|name|walkable|rgb_top" -
                 * take just the name (2nd field) for a readable legend
                 * instead of the whole row; equals format's remainder
                 * is already just the id, used as-is. */
                char label[64];
                if (is_pipe) {
                    char *name_start = strchr(rline + 2, '|');
                    if (name_start) {
                        name_start++;
                        char *name_end = strchr(name_start, '|');
                        int len = name_end ? (int)(name_end - name_start) : (int)strlen(name_start);
                        if (len > 63) len = 63;
                        memcpy(label, name_start, (size_t)len);
                        label[len] = '\0';
                    } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                        snprintf(label, sizeof(label), "%s", rline + 2);
#pragma GCC diagnostic pop
                    }
                } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                    snprintf(label, sizeof(label), "%s", rline + 2);
#pragma GCC diagnostic pop
                }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(legend, sizeof(legend), "  %s %d. '%c' %s", cur, reg_row + 1, rline[0], label);
#pragma GCC diagnostic pop
                line(out, legend);
                reg_row++;
            }
            fclose(rf);
        }
        if (reg_row == 0) line(out, "  (registry not found or empty)");
    } else if (strcmp(screen, "pieces") == 0) {
        char hdr[96];
        snprintf(hdr, sizeof(hdr), "PIECES IN: %s", proj_name);
        title(out, hdr);
        blank(out);
        char cmd[PATH_BUF + 64];
        snprintf(cmd, sizeof(cmd), "find '%s/pieces' -name piece.pdl 2>/dev/null", proj_path);
        FILE *pf = popen(cmd, "r");
        int row = 0;
        if (pf) {
            char pline[PATH_BUF];
            while (fgets(pline, sizeof(pline), pf)) {
                pline[strcspn(pline, "\r\n")] = '\0';
                if (!pline[0]) continue;
                char *slash = strrchr(pline, '/');
                if (slash) *slash = '\0';
                char *id_slash = strrchr(pline, '/');
                option(out, row + 1, cursor, id_slash ? id_slash + 1 : pline);
                row++;
            }
            pclose(pf);
        }
        if (row == 0) line(out, "  (no piece.pdl files found under this project)");
    } else if (strcmp(screen, "piece_detail") == 0) {
        char *slash = strrchr(piece_pdl_path, '/');
        char piece_id[64];
        if (slash) {
            char tmp[PATH_BUF];
            snprintf(tmp, sizeof(tmp), "%s", piece_pdl_path);
            tmp[slash - piece_pdl_path] = '\0';
            char *id_slash = strrchr(tmp, '/');
            /* piece_id is genuinely short (a directory basename)
             * despite gcc only being able to prove it no longer than
             * tmp's own PATH_BUF - same class of warning suppressed
             * narrowly elsewhere in this project family. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(piece_id, sizeof(piece_id), "%s", id_slash ? id_slash + 1 : tmp);
#pragma GCC diagnostic pop
        } else {
            snprintf(piece_id, sizeof(piece_id), "%s", "?");
        }
        char hdr[96];
        snprintf(hdr, sizeof(hdr), "PIECE: %s", piece_id);
        title(out, hdr);
        blank(out);
        line(out, "  METHOD table:");
        blank(out);

        char cmd[(PATH_BUF * 2) + 32];
        snprintf(cmd, sizeof(cmd), "'%s/ops/+x/pdl_reader.+x' '%s' list_methods_full", project_root, piece_pdl_path);
        FILE *pf = popen(cmd, "r");
        int row = 0;
        if (pf) {
            char pline[MAX_LINE];
            while (fgets(pline, sizeof(pline), pf)) {
                pline[strcspn(pline, "\r\n")] = '\0';
                char *bar = strchr(pline, '|');
                if (!bar) continue;
                *bar = '\0';
                char rowbuf[BOX_W + 1];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(rowbuf, sizeof(rowbuf), "  %d. %s -> %s", row, pline, bar + 1);
#pragma GCC diagnostic pop
                line(out, rowbuf);
                row++;
            }
            pclose(pf);
        }
        if (row == 0) line(out, "  (no METHOD rows found)");
        blank(out);
        option(out, 1, cursor, "Back");
    }

    blank(out);
    border(out);
    if (strcmp(screen, "map_edit") == 0) {
        fprintf(out, "[arrows] move  [1-9] arm tile  [enter] place+save  [esc] back\n");
    } else {
        fprintf(out, "[0-9] jump  [enter] select  [q] quit\n");
    }

    fclose(out);
    return 0;
}
