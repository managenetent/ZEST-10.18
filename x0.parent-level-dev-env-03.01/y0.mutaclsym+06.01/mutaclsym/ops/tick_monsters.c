/* tick_monsters - one verb, one binary, no shared headers.
 * Runs once per hero turn (called from the pal script right after
 * end_turn): every monster piece physically nested under the hero's
 * CURRENT map's monsters/ directory takes one action - step one tile
 * toward or away from the hero (diagonal moves allowed, blocked by the
 * same terrain/furniture rules as the hero and by other monsters), or,
 * if a toward-step would land on the hero's own tile, attack instead of
 * moving (reduces hero hp by the monster's damage). Monsters on a map
 * the hero isn't currently on don't tick - same "only what's being
 * observed advances" rule the per-map turn counters already follow.
 *
 * decision_mode (per-instance state.txt field, per-instance not a
 * registry default - see GAME-AI-SPEED-DOCTRINE.txt and the family's
 * wsr-pal/corp_decide.c + muchi-evo-pal/bot_choose.c precedent):
 *   0 (preset, default when the field is absent - existing pieces with
 *      no such field keep behaving byte-identically) - always chase.
 *   1 (weighted) - reads this instance's own flee_hp_pct field (same
 *      state.txt, no separate weights file - matches wsr-pal's
 *      risk_bias precedent). Below that hp percentage, steps AWAY from
 *      the hero instead of toward. Otherwise identical to preset.
 * Deliberately no 2/3/4 (rl/llm/human) branches - a monster's
 * chase-or-flee decision is a small, fixed-shape node (doctrine §3);
 * it doesn't need GOAP/BT/rl/llm/human the way a corp or creature-
 * lineage piece does. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
/* Generous compile-time buffer-size caps, NOT the real per-map
 * dimensions - see move_player.c's identical comment / dox/
 * 01-cdda-architecture.md §5a for why. */
#define MAX_MAP_W 256
#define MAX_MAP_H 256

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
        line[strcspn(line, "\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, key) == 0) { snprintf(out, out_sz, "%s", eq + 1); break; }
    }
    fclose(f);
}

static int glyph_walkable(const char *registry_rel_path, char glyph) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s", project_root, registry_rel_path);
    FILE *f = fopen(path, "r");
    if (!f) return glyph == '.';
    char line[MAX_LINE];
    int result = 0;
    while (fgets(line, sizeof(line), f)) {
        /* See ops/move_player.c's own copy of this function for why
         * line[1]=='|' (not just line[0]=='#') is the real comment
         * test - '#' is itself a valid glyph (t_wall). */
        if (line[0] == '\n' || (line[0] == '#' && line[1] != '|')) continue;
        if (line[0] != glyph) continue;
        char *p = strchr(line, '|');
        if (!p) continue;
        p = strchr(p + 1, '|');
        if (!p) continue;
        p = strchr(p + 1, '|');
        if (!p) continue;
        result = atoi(p + 1);
        break;
    }
    fclose(f);
    return result;
}

static char file_glyph_at(const char *abs_path, int x, int y, char default_glyph, int map_w, int map_h) {
    if (x < 0 || y < 0 || x >= map_w || y >= map_h) return default_glyph;
    FILE *f = fopen(abs_path, "r");
    if (!f) return default_glyph;
    char line[MAX_MAP_W + 4];
    char glyph = default_glyph;
    for (int row = 0; row <= y; row++) {
        if (!fgets(line, sizeof(line), f)) { glyph = default_glyph; break; }
        if (row == y) glyph = (x < (int)strlen(line)) ? line[x] : default_glyph;
    }
    fclose(f);
    return glyph;
}

static int furniture_walkable(char glyph) {
    if (glyph == ' ') return 1;
    return glyph_walkable("pieces/registry/furniture/furniture_types.txt", glyph);
}

static int monster_damage(const char *monster_type) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/monsters/monster_types.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char line[MAX_LINE];
    int dmg = 1;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(monster_type) || strncmp(line, monster_type, p1 - line) != 0) continue;
        char *name = p1 + 1;
        char *p2 = strchr(name, '|');
        if (!p2) continue;
        char *glyph = p2 + 1;
        char *p3 = strchr(glyph, '|');
        if (!p3) continue;
        char *hp = p3 + 1;
        char *p4 = strchr(hp, '|');
        if (!p4) continue;
        dmg = atoi(p4 + 1);
        break;
    }
    fclose(f);
    return dmg;
}

/* Reads the TYPE's hp column from monster_types.txt - used as the
 * "max hp" reference for the weighted tier's flee-percentage calc.
 * The live instance's own hp (its state.txt field) is the "current hp"
 * side of that ratio. */
static int monster_max_hp(const char *monster_type) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/monsters/monster_types.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char line[MAX_LINE];
    int hp = 1;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(monster_type) || strncmp(line, monster_type, p1 - line) != 0) continue;
        char *name = p1 + 1;
        char *p2 = strchr(name, '|');
        if (!p2) continue;
        char *glyph = p2 + 1;
        char *p3 = strchr(glyph, '|');
        if (!p3) continue;
        hp = atoi(p3 + 1);
        break;
    }
    fclose(f);
    return hp > 0 ? hp : 1;
}

static void monster_name(const char *monster_type, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", monster_type);
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/registry/monsters/monster_types.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        if ((size_t)(p1 - line) != strlen(monster_type) || strncmp(line, monster_type, p1 - line) != 0) continue;
        char *name = p1 + 1;
        char *end = strchr(name, '|');
        if (end) *end = '\0';
        snprintf(out, out_sz, "%s", name);
        break;
    }
    fclose(f);
}

static void log_message(const char *msg) {
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/display/message_log.txt", project_root);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s\n", msg);
    fclose(f);
}

static void hero_take_damage(const char *hero_path, int dmg) {
    int hp = read_kv_int(hero_path, "hp", 100);
    hp -= dmg;
    if (hp < 0) hp = 0;

    FILE *f = fopen(hero_path, "r");
    if (!f) return;
    char lines[32][MAX_LINE];
    int nlines = 0;
    while (nlines < 32 && fgets(lines[nlines], MAX_LINE, f)) nlines++;
    fclose(f);

    f = fopen(hero_path, "w");
    if (!f) return;
    for (int i = 0; i < nlines; i++) {
        if (strncmp(lines[i], "hp", 2) == 0 && lines[i][2] == '=') {
            fprintf(f, "hp=%d\n", hp);
            continue;
        }
        fputs(lines[i], f);
    }
    fclose(f);
}

int main(void) {
    resolve_root();

    char hero_path[PATH_BUF];
    snprintf(hero_path, sizeof(hero_path), "%s/pieces/world_01/map_start/hero/state.txt", project_root);
    int hero_x = read_kv_int(hero_path, "pos_x", 0);
    int hero_y = read_kv_int(hero_path, "pos_y", 0);
    char map_id[64];
    read_kv_str(hero_path, "map_id", map_id, sizeof(map_id), "map_start");

    char map_dir[PATH_BUF];
    snprintf(map_dir, sizeof(map_dir), "%s/pieces/world_01/%s", project_root, map_id);
    char monsters_dir[PATH_BUF + 32];
    snprintf(monsters_dir, sizeof(monsters_dir), "%s/monsters", map_dir);
    char map_path[PATH_BUF + 32], furniture_path[PATH_BUF + 32], map_state_path[PATH_BUF + 32];
    snprintf(map_path, sizeof(map_path), "%s/map.txt", map_dir);
    snprintf(furniture_path, sizeof(furniture_path), "%s/furniture.txt", map_dir);
    snprintf(map_state_path, sizeof(map_state_path), "%s/state.txt", map_dir);
    int map_w = read_kv_int(map_state_path, "width", 40);
    int map_h = read_kv_int(map_state_path, "height", 16);

    DIR *d = opendir(monsters_dir);
    if (!d) return 0;

    /* First pass: read every monster's current position, so a later
     * monster in iteration order doesn't see an earlier one's ALREADY
     * updated position and get confused about occupancy - all monsters
     * act simultaneously based on where everyone was at the start of
     * this tick. */
    char names[64][320]; /* headroom for dirent's up-to-256-byte d_name */
    int mx[64], my[64];
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && count < 64) {
        if (entry->d_name[0] == '.') continue;
        snprintf(names[count], sizeof(names[count]), "%s", entry->d_name);
        char state_path[PATH_BUF + 640];
        snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", monsters_dir, entry->d_name);
        mx[count] = read_kv_int(state_path, "pos_x", 0);
        my[count] = read_kv_int(state_path, "pos_y", 0);
        count++;
    }
    closedir(d);

    for (int i = 0; i < count; i++) {
        char state_path[PATH_BUF + 640];
        /* names[i] is genuinely a short piece-directory name ("zombie_01")
         * despite being declared with 256-byte-dirent headroom, so gcc
         * can't prove state_path is big enough from static sizes alone;
         * same class of warning suppressed narrowly elsewhere in this
         * project (mutaclsym/system/prisc+x.c, generate_egg.c,
         * menu_input.c) rather than widened indefinitely. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(state_path, sizeof(state_path), "%s/%s/state.txt", monsters_dir, names[i]);
#pragma GCC diagnostic pop
        char monster_type[64];
        read_kv_str(state_path, "monster_type", monster_type, sizeof(monster_type), "zombie");
        int decision_mode = read_kv_int(state_path, "decision_mode", 0);

        int dx = (hero_x > mx[i]) - (hero_x < mx[i]);
        int dy = (hero_y > my[i]) - (hero_y < my[i]);
        if (dx == 0 && dy == 0) continue; /* already on the hero - shouldn't happen, skip */

        int fleeing = 0;
        if (decision_mode == 1) {
            int flee_hp_pct = read_kv_int(state_path, "flee_hp_pct", 50);
            int hp = read_kv_int(state_path, "hp", 1);
            int max_hp = monster_max_hp(monster_type);
            int hp_pct = hp * 100 / max_hp;
            if (hp_pct < flee_hp_pct) fleeing = 1;
        }
        if (fleeing) { dx = -dx; dy = -dy; }

        int nx = mx[i] + dx, ny = my[i] + dy;

        if (nx == hero_x && ny == hero_y) {
            int dmg = monster_damage(monster_type);
            char name[64], msg[128];
            monster_name(monster_type, name, sizeof(name));
            snprintf(msg, sizeof(msg), "%s hits you for %d!", name, dmg);
            hero_take_damage(hero_path, dmg);
            log_message(msg);
            continue;
        }

        char terrain_glyph = file_glyph_at(map_path, nx, ny, '#', map_w, map_h);
        char furniture_glyph = file_glyph_at(furniture_path, nx, ny, ' ', map_w, map_h);
        if (!glyph_walkable("pieces/registry/terrain/terrain_types.txt", terrain_glyph) ||
            !furniture_walkable(furniture_glyph)) continue;

        int occupied = 0;
        for (int j = 0; j < count; j++) {
            if (j != i && mx[j] == nx && my[j] == ny) { occupied = 1; break; }
        }
        if (occupied) continue;

        FILE *sf = fopen(state_path, "r");
        char lines[16][MAX_LINE];
        int nlines = 0;
        if (sf) { while (nlines < 16 && fgets(lines[nlines], MAX_LINE, sf)) nlines++; fclose(sf); }

        sf = fopen(state_path, "w");
        if (sf) {
            for (int k = 0; k < nlines; k++) {
                if (strncmp(lines[k], "pos_x", 5) == 0 && lines[k][5] == '=') { fprintf(sf, "pos_x=%d\n", nx); continue; }
                if (strncmp(lines[k], "pos_y", 5) == 0 && lines[k][5] == '=') { fprintf(sf, "pos_y=%d\n", ny); continue; }
                fputs(lines[k], sf);
            }
            fclose(sf);
        }
    }

    return 0;
}
