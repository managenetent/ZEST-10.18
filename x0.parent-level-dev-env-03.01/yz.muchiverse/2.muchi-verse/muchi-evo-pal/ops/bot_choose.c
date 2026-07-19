/* bot_choose - the "deciding" state's op: reads decision_mode and
 * picks eat vs evolve accordingly, then advances current_state to
 * whichever action was chosen (1 -> 2 for eat, 1 -> 3 for evolve).
 * This is the one op in this whole vertical slice that actually
 * exercises EVO-DESIGN.txt §5/§12's 4-tier decision_mode chassis - see
 * ../pieces/world_01/creature_01/fsm/states.txt for what's real vs
 * stubbed per tier right now.
 *
 * decision_mode=3 (llm) is the REAL, live-wired tier: it calls the
 * LAN Mac's gemma3:270m model (model_list-equivalent: ollama,
 * http://10.0.0.144:11434, gemma3:270m - see
 * y0.muchi-pal-chat/pieces/registry/models/model_list.txt's
 * "gemma-lan" entry, same LAN host, reused here directly rather than
 * duplicating a separate registry - per direct instruction: prefer the
 * LAN Mac's gemma over localhost, since this machine is slow and these
 * decision calls are tiny/frequent, not a case that needs groq's much
 * larger/slower tool-use model). Reuses connect_op.+x/json_parser.+x
 * verbatim - the exact ollama /api/chat request shape already proven
 * in muchi-pal-chat's build_ollama_request, just a one-shot call with
 * no history/tools[], matching generate_corpus.c's own precedent for
 * "standalone CLI tool, not a chat turn."
 *
 * Self-contained, no shared headers.
 * Usage: bot_choose.+x <piece_id> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_LINE 512
#define MAX_FIELD 256

static char project_root[MAX_PATH] = ".";
static const char *GEMMA_LAN_URL = "http://10.0.0.144:11434";
static const char *GEMMA_LAN_MODEL = "gemma3:270m";

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

static char *read_full_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (buf) {
        size_t n = fread(buf, 1, size, f);
        buf[n] = '\0';
    }
    fclose(f);
    return buf;
}

static void json_escaped(FILE *out, const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputs("\\\"", out);
        else if (*p == '\\') fputs("\\\\", out);
        else if (*p == '\n') fputs("\\n", out);
        else fputc(*p, out);
    }
}

static char *run_capture(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;
    char *buf = malloc(4096);
    size_t total = 0, n;
    while ((n = fread(buf + total, 1, 4095 - total, pipe)) > 0) {
        total += n;
        if (total >= 4095) break;
    }
    buf[total] = '\0';
    pclose(pipe);
    return buf;
}

/* preset threshold, shared by decision_mode 0/1/2 - weighted/rl are
 * explicitly stubbed to this same logic for now, see states.txt. */
static int preset_choice(int ep) {
    return ep >= 3; /* 1 = evolve, 0 = eat */
}

/* decision_mode=3 (llm): real call to the LAN gemma model. Returns
 * 1 for evolve, 0 for eat - falls back to preset_choice() if the
 * model call fails/is unparseable, so a flaky network call never
 * leaves the FSM stuck. */
static int llm_choice(int ep, const char *body_parts) {
    char persona_path[PATH_BUF];
    snprintf(persona_path, sizeof(persona_path), "%s/pieces/registry/personas/decide_eat_or_evolve.txt", project_root);
    char *persona = read_full_file(persona_path);
    if (!persona) return preset_choice(ep);

    char user_turn[MAX_FIELD];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(user_turn, sizeof(user_turn), "ep=%d body_parts=%s", ep, body_parts[0] ? body_parts : "(none)");
#pragma GCC diagnostic pop

    char request_path[PATH_BUF], response_path[PATH_BUF];
    snprintf(request_path, sizeof(request_path), "/tmp/bot_choose_request_%d.json", getpid());
    snprintf(response_path, sizeof(response_path), "/tmp/bot_choose_response_%d.json", getpid());

    FILE *pf = fopen(request_path, "w");
    if (!pf) { free(persona); return preset_choice(ep); }
    fprintf(pf, "{\"model\":\"%s\",\"stream\":false,\"messages\":[{\"role\":\"system\",\"content\":\"", GEMMA_LAN_MODEL);
    json_escaped(pf, persona);
    fputs("\"},{\"role\":\"user\",\"content\":\"", pf);
    json_escaped(pf, user_turn);
    fputs("\"}]}", pf);
    fclose(pf);
    free(persona);

    char full_url[MAX_FIELD];
    snprintf(full_url, sizeof(full_url), "%s/api/chat", GEMMA_LAN_URL);

    char connect_cmd[PATH_BUF * 3];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(connect_cmd, sizeof(connect_cmd), "'%s/ops/+x/connect_op.+x' '%s' '%s' '%s'",
             project_root, full_url, request_path, response_path);
#pragma GCC diagnostic pop
    int rc = system(connect_cmd);
    remove(request_path);
    if (rc != 0) { remove(response_path); return preset_choice(ep); }

    char json_parser_cmd[PATH_BUF * 2];
    snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'message.content'", project_root, response_path);
    char *content = run_capture(json_parser_cmd);
    remove(response_path);

    int result = preset_choice(ep); /* fallback default */
    if (content) {
        for (char *p = content; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (strstr(content, "evolve") && ep >= 3) result = 1;
        else if (strstr(content, "eat")) result = 0;
        free(content);
    }
    return result;
}

int main(int argc, char *argv[]) {
    resolve_root();
    if (argc < 2) return 1;
    char state_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/projects/muchi-evo-pal/pieces/%s/state.txt", project_root, argv[1]);

    char mode_str[MAX_FIELD], ep_str[MAX_FIELD], body_parts[MAX_FIELD], human_decision[MAX_FIELD];
    read_state_field(state_path, "decision_mode", mode_str, sizeof(mode_str));
    read_state_field(state_path, "ep", ep_str, sizeof(ep_str));
    read_state_field(state_path, "body_parts", body_parts, sizeof(body_parts));
    read_state_field(state_path, "human_decision", human_decision, sizeof(human_decision));
    int mode = mode_str[0] ? atoi(mode_str) : 0;
    int ep = ep_str[0] ? atoi(ep_str) : 0;

    /* decision_mode=4 (human): NOT auto-resolved. Unlike the other three
     * tiers, this one can leave the FSM PARKED in "deciding" - if no
     * human_decision is queued yet, this op deliberately does nothing
     * (no state change), so repeated ticks just wait. A human queues a
     * choice via `button.sh choose eat|evolve` (writes human_decision),
     * then the next tick consumes it. This is the honest architecture
     * for human-in-the-loop control in a tick-driven FSM - see
     * pal/single_tick.pal's own header for why batch main_loop.pal can't
     * support this (no pause point mid-batch). */
    if (mode == 4) {
        if (!human_decision[0]) {
            printf("[human] waiting for input (run: button.sh choose eat|evolve)\n");
            return 0;
        }
        int evolve = (strcmp(human_decision, "evolve") == 0) && ep >= 3;
        write_state_field(state_path, "human_decision", "");
        write_state_field(state_path, "current_state", evolve ? "3" : "2");
        write_state_field(state_path, "last_choice", evolve ? "evolve" : "eat");
        printf("[human] chose %s (ep=%d)\n", evolve ? "evolve" : "eat", ep);
        return 0;
    }

    int evolve;
    const char *mode_name;
    if (mode == 3) {
        evolve = llm_choice(ep, body_parts);
        mode_name = "llm";
    } else if (mode == 2) {
        evolve = preset_choice(ep); /* rl: STUB, see states.txt */
        mode_name = "rl(stub)";
    } else if (mode == 1) {
        evolve = preset_choice(ep); /* weighted: STUB, see states.txt */
        mode_name = "weighted(stub)";
    } else {
        evolve = preset_choice(ep);
        mode_name = "preset";
    }

    write_state_field(state_path, "current_state", evolve ? "3" : "2");
    write_state_field(state_path, "last_choice", evolve ? "evolve" : "eat");
    printf("[%s] chose %s (ep=%d)\n", mode_name, evolve ? "evolve" : "eat", ep);
    return 0;
}
