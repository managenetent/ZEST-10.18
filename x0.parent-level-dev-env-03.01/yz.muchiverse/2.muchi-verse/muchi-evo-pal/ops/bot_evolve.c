/* bot_evolve - spends EP, appends a body part to body_parts (a plain
 * comma-separated list here - the minimal v1 of EVO-DESIGN.txt §3's
 * composable `composed_of` body-part chain; not the full
 * COMPOSABLE-MATERIALS-ARCHITECTURE.md registry yet, that's a later
 * build-order step, see EVO-DESIGN.txt §7). Returns to idle
 * (current_state 3 -> 0). Real op, not a stub.
 * Self-contained, no shared headers.
 * Usage: bot_evolve.+x <piece_id> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_LINE 512
#define MAX_FIELD 256

static char project_root[MAX_PATH] = ".";
static const char *BODY_PART_POOL[] = {"fin", "jaw", "legs", "shell", "horn", "wing"};
#define POOL_SIZE 6

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

static void read_state_field(const char *state_path, const char *key, char *out, size_t out_sz) {
    out[0] = '\0';
    FILE *f = fopen(state_path, "r");
    if (!f) return;
    char line[MAX_LINE];
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
            char *v = line + key_len + 1;
            v[strcspn(v, "\n")] = '\0';
            snprintf(out, out_sz, "%s", v);
            break;
        }
    }
    fclose(f);
}

static void write_state_field(const char *state_path, const char *key, const char *value) {
    FILE *f = fopen(state_path, "r");
    char lines[64][MAX_LINE];
    int nlines = 0;
    if (f) {
        while (nlines < 64 && fgets(lines[nlines], MAX_LINE, f)) nlines++;
        fclose(f);
    }
    size_t key_len = strlen(key);
    f = fopen(state_path, "w");
    if (!f) return;
    int found = 0;
    for (int i = 0; i < nlines; i++) {
        if (strncmp(lines[i], key, key_len) == 0 && lines[i][key_len] == '=') {
            fprintf(f, "%s=%s\n", key, value);
            found = 1;
        } else {
            fputs(lines[i], f);
        }
    }
    if (!found) fprintf(f, "%s=%s\n", key, value);
    fclose(f);
}

int main(int argc, char *argv[]) {
    resolve_root();
    if (argc < 2) return 1;
    char state_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/projects/muchi-evo-pal/pieces/%s/state.txt", project_root, argv[1]);

    char ep_str[MAX_FIELD];
    read_state_field(state_path, "ep", ep_str, sizeof(ep_str));
    int ep = ep_str[0] ? atoi(ep_str) : 0;

    char body_parts[MAX_FIELD];
    read_state_field(state_path, "body_parts", body_parts, sizeof(body_parts));

    if (ep < 3) {
        /* Recipe requirement not met - "consume" fails, matching
         * craft.c's own real behavior when a recipe's requirements
         * aren't satisfied. No state change. */
        printf("cannot evolve: need 3 ep, have %d\n", ep);
        return 1;
    }

    ep -= 3;
    int part_count = 0;
    for (char *p = body_parts; *p; p++) if (*p == ',') part_count++;
    if (strlen(body_parts) > 0) part_count++;
    const char *new_part = BODY_PART_POOL[part_count % POOL_SIZE];

    char new_body_parts[MAX_FIELD];
    if (strlen(body_parts) > 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(new_body_parts, sizeof(new_body_parts), "%s,%s", body_parts, new_part);
#pragma GCC diagnostic pop
    } else {
        snprintf(new_body_parts, sizeof(new_body_parts), "%s", new_part);
    }

    char new_ep[MAX_FIELD];
    snprintf(new_ep, sizeof(new_ep), "%d", ep);
    write_state_field(state_path, "ep", new_ep);
    write_state_field(state_path, "body_parts", new_body_parts);
    write_state_field(state_path, "current_state", "0");
    printf("evolved: gained %s, body_parts now [%s], ep now %d\n", new_part, new_body_parts, ep);
    return 0;
}
