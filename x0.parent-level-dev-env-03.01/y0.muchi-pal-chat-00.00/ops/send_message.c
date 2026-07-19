/* send_message - the "Enter" verb. Reads the live input_buffer, and
 * either delegates to switch_model.+x (if the buffer starts with
 * "/model ") or sends it as a chat turn.
 *
 * Builds one of three request shapes depending on state.txt's
 * provider_kind, each ported from a proven-working TPMOS fix:
 *   - ollama:   native tools[] + /api/chat (build_ollama_request) -
 *               proven in groq-ollama.
 *   - gemini:   systemInstruction+contents+tools[functionDeclarations] +
 *               non-streaming generateContent (build_gemini_request) -
 *               ported from gem-dev's gemini_payload_builder.c.
 *   - llamacpp: manually-built raw Llama3 prompt + /completion
 *               (build_llamacpp_request, via text_to_pal_prompt.+x) -
 *               proven in cpp-llm. No native tool schema on this path.
 *   - iqabod:   no HTTP, no JSON request at all - forks
 *               main_orchestrator.+x directly (build_iqabod_prompt,
 *               see model_list.txt's header comment for how api_url/
 *               model_name are repurposed as IQABOD's project root /
 *               curriculum path). IQABOD's curricula are raw
 *               word-continuation models, not instruction-tuned to
 *               emit the tool/args/response JSON convention the
 *               llamacpp path uses - see ROADMAP-models.txt §4 - so
 *               this path only ever produces plain assistant text,
 *               never a tool call.
 *
 * Non-blocking by design: for ollama/gemini/llamacpp, forks a child that
 * execs connect_op (which itself blocks on curl internally and exits
 * when done); for iqabod, forks a child that execs main_orchestrator.+x
 * directly (no network, no connect_op). Either way the child's PID is
 * recorded in state.txt's curl_pid field, and this op returns
 * immediately. check_response.c (called every tick from main_loop.pal
 * while ai_state=THINKING) polls that PID for liveness to know when to
 * parse the response - ops have no persistent memory between
 * invocations, so this is the only way a one-shot op can hand off a
 * long-running call to future ticks, mirroring how prisc+x's own tick
 * loop already polls everything else.
 *
 * Self-contained: own root resolution, own constants, no shared headers.
 * Usage: send_message.+x (no args - reads/writes only via state files) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_LINE 4096
#define MAX_BUFFER 2048
#define MAX_FIELD 256
#define MAX_IQABOD_PROMPT_CHARS 200
#define MAX_IQABOD_LOG_LINES 256

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
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

/* Reversible pipe-format escaping for context_log.txt fields: literal
 * backslash -> \\, literal pipe -> \|, literal newline -> \n. Keeps each
 * turn to exactly one line, one field per "|", matching mutaclsym's own
 * doctrine of plain pipe-delimited text over JSON for persisted state. */
static char *pipe_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    char *out = malloc(len * 2 + 1);
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else if (s[i] == '|') { *p++ = '\\'; *p++ = '|'; }
        else if (s[i] == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else *p++ = s[i];
    }
    *p = '\0';
    return out;
}

static char *pipe_unescape(const char *s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) {
            i++;
            if (s[i] == 'n') *p++ = '\n';
            else *p++ = s[i];
        } else *p++ = s[i];
    }
    *p = '\0';
    return out;
}

/* JSON-string escaping, distinct from pipe_escape above - this one is for
 * embedding text inside a JSON string literal in the outgoing request. */
static void json_escaped(FILE *out, const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputs("\\\"", out);
        else if (*p == '\\') fputs("\\\\", out);
        else if (*p == '\n') fputs("\\n", out);
        else if (*p == '\r') fputs("\\r", out);
        else if (*p == '\t') fputs("\\t", out);
        else if ((unsigned char)*p < 32) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(out, out_sz, "%s", v);
#pragma GCC diagnostic pop
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

static void append_log_turn(const char *log_path, const char *role, const char *kind, const char *tool_name, const char *content) {
    FILE *f = fopen(log_path, "a");
    if (!f) return;
    char *esc_content = pipe_escape(content);
    fprintf(f, "%s|%s|%s|%s\n", role, kind, tool_name ? tool_name : "", esc_content);
    free(esc_content);
    fclose(f);
}

static char *run_tool_capture(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;
    char *buf = malloc(262144);
    size_t total = 0;
    size_t n;
    while ((n = fread(buf + total, 1, 262144 - total - 1, pipe)) > 0) {
        total += n;
        if (total >= 262143) break;
    }
    buf[total] = '\0';
    pclose(pipe);
    return buf;
}

static const char *OLLAMA_TOOLS_JSON =
    "{\"type\":\"function\",\"function\":{\"name\":\"exec_cmd\",\"description\":\"Run a shell command on the host system\",\"parameters\":{\"type\":\"object\",\"properties\":{\"cmd\":{\"type\":\"string\"}},\"required\":[\"cmd\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"description\":\"Read the contents of a file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"description\":\"Create or overwrite a file with content\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"list_dir\",\"description\":\"List files and subdirectories in a directory\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"search_in_files\",\"description\":\"Search local files recursively for a text query\",\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"edit_file\",\"description\":\"Search and replace a block of text in a file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"search\":{\"type\":\"string\"},\"replace\":{\"type\":\"string\"}},\"required\":[\"path\",\"search\",\"replace\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"web_search\",\"description\":\"Search the internet using DuckDuckGo\",\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}}}";

static const char *GEMINI_FUNCTION_DECLARATIONS =
    "{\"name\":\"exec_cmd\",\"description\":\"Run a shell command on the host system\",\"parameters\":{\"type\":\"object\",\"properties\":{\"cmd\":{\"type\":\"string\"}},\"required\":[\"cmd\"]}},"
    "{\"name\":\"read_file\",\"description\":\"Read the contents of a file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"write_file\",\"description\":\"Create or overwrite a file with content\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}},"
    "{\"name\":\"list_dir\",\"description\":\"List files and subdirectories in a directory\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}},"
    "{\"name\":\"search_in_files\",\"description\":\"Search local files recursively for a text query\",\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}},"
    "{\"name\":\"edit_file\",\"description\":\"Search and replace a block of text in a file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"search\":{\"type\":\"string\"},\"replace\":{\"type\":\"string\"}},\"required\":[\"path\",\"search\",\"replace\"]}},"
    "{\"name\":\"web_search\",\"description\":\"Search the internet using DuckDuckGo\",\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}}";

/* Ollama-native request body: flat "messages" array + native "tools[]" -
 * proven working in groq-ollama. Persona is the "just call the tool"
 * variant (native_tools.txt), since this provider has a real tool schema
 * and doesn't need prompt-engineered JSON. */
static void build_ollama_request(FILE *pf, const char *log_path, const char *model_name) {
    char persona_path[PATH_BUF];
    snprintf(persona_path, sizeof(persona_path), "%s/pieces/registry/personas/native_tools.txt", project_root);
    char *persona = read_full_file(persona_path);

    fprintf(pf, "{\"model\":\"%s\",\"stream\":false,\"messages\":[", model_name);
    if (persona && strlen(persona) > 0) {
        fputs("{\"role\":\"system\",\"content\":\"", pf);
        json_escaped(pf, persona);
        fputs("\"}", pf);
    }
    free(persona);

    FILE *lf = fopen(log_path, "r");
    if (lf) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), lf)) {
            line[strcspn(line, "\n")] = '\0';
            char *p1 = strchr(line, '|'); if (!p1) continue;
            char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            char *p3 = strchr(p2 + 1, '|'); if (!p3) continue;
            *p1 = '\0'; *p2 = '\0'; *p3 = '\0';
            const char *role = line;
            if (strcmp(role, "system") == 0) continue; /* already emitted above */
            char *content = pipe_unescape(p3 + 1);
            fprintf(pf, ",{\"role\":\"%s\",\"content\":\"", role);
            json_escaped(pf, content);
            fputs("\"}", pf);
            free(content);
        }
        fclose(lf);
    }

    fputs("],\"tools\":[", pf);
    fputs(OLLAMA_TOOLS_JSON, pf);
    fputs("]}", pf);
}

/* Gemini request body: systemInstruction + contents[] (role "user"/
 * "model"/"function", parts holding text/functionCall/functionResponse) +
 * tools[{functionDeclarations}]. Same native-tool-calling persona as
 * Ollama - Gemini has a real tool schema too. Ported from gem-dev's
 * proven gemini_payload_builder.c, adapted to read our pipe format
 * (which already carries tool_name on the relevant lines directly, so no
 * "last function name" tracking is needed the way that file needed it). */
static void build_gemini_request(FILE *pf, const char *log_path) {
    char persona_path[PATH_BUF];
    snprintf(persona_path, sizeof(persona_path), "%s/pieces/registry/personas/native_tools.txt", project_root);
    char *persona = read_full_file(persona_path);

    fputs("{", pf);
    if (persona && strlen(persona) > 0) {
        fputs("\"systemInstruction\":{\"parts\":[{\"text\":\"", pf);
        json_escaped(pf, persona);
        fputs("\"}]},", pf);
    }
    free(persona);

    fputs("\"contents\":[", pf);
    int first = 1;
    FILE *lf = fopen(log_path, "r");
    if (lf) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), lf)) {
            line[strcspn(line, "\n")] = '\0';
            char *p1 = strchr(line, '|'); if (!p1) continue;
            char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            char *p3 = strchr(p2 + 1, '|'); if (!p3) continue;
            *p1 = '\0'; *p2 = '\0'; *p3 = '\0';
            const char *role = line;
            const char *kind = p1 + 1;
            const char *tool_name = p2 + 1;

            if (strcmp(role, "system") == 0) continue;
            char *content = pipe_unescape(p3 + 1);

            if (!first) fputs(",", pf);
            first = 0;

            if (strcmp(role, "user") == 0) {
                fputs("{\"role\":\"user\",\"parts\":[{\"text\":\"", pf);
                json_escaped(pf, content);
                fputs("\"}]}", pf);
            } else if (strcmp(role, "assistant") == 0 && strcmp(kind, "tool_call") == 0) {
                fprintf(pf, "{\"role\":\"model\",\"parts\":[{\"functionCall\":{\"name\":\"%s\",\"args\":%s}}]}", tool_name, content);
            } else if (strcmp(role, "assistant") == 0) {
                fputs("{\"role\":\"model\",\"parts\":[{\"text\":\"", pf);
                json_escaped(pf, content);
                fputs("\"}]}", pf);
            } else if (strcmp(role, "tool") == 0) {
                fprintf(pf, "{\"role\":\"function\",\"parts\":[{\"functionResponse\":{\"name\":\"%s\",\"response\":{\"result\":\"", tool_name);
                json_escaped(pf, content);
                fputs("\"}}}]}", pf);
            } else {
                first = 1; /* unrecognized role - didn't actually emit, don't count it for comma placement */
            }
            free(content);
        }
        fclose(lf);
    }
    fputs("]", pf);

    fputs(",\"tools\":[{\"functionDeclarations\":[", pf);
    fputs(GEMINI_FUNCTION_DECLARATIONS, pf);
    fputs("]}]", pf);
    fputs("}", pf);
}

/* llama.cpp/completion request body: a manually-built raw Llama3 prompt
 * (text_to_pal_prompt.+x) posted to /completion, matching cpp-llm's
 * proven pattern. No native tool schema on this path - prompt-engineered
 * JSON only, via the prompt_json.txt persona text_to_pal_prompt.+x
 * already selects. */
static void build_llamacpp_request(FILE *pf, const char *log_path) {
    (void)log_path; /* text_to_pal_prompt.+x reads context_log.txt itself */
    char cmd[PATH_BUF];
    snprintf(cmd, sizeof(cmd), "'%s/ops/+x/text_to_pal_prompt.+x'", project_root);
    char *raw_prompt = run_tool_capture(cmd);

    fputs("{\"prompt\":\"", pf);
    json_escaped(pf, raw_prompt ? raw_prompt : "");
    fputs("\",\"n_predict\":512,\"stream\":false,\"stop\":[\"<|eot_id|>\",\"<|end_of_text|>\"]}", pf);
    free(raw_prompt);
}

/* iqabod prompt: plain concatenated turn text, most recent turns first,
 * NO role markers ("user:"/"assistant:") and NO persona/instruction
 * preamble - unlike the other three providers. IQABOD's vocabulary is
 * closed-world/exact-match only (see ROADMAP-models.txt §4), so any
 * token that isn't already in the trained curriculum's vocab becomes
 * <UNK> noise rather than being understood; synthetic markers and
 * english instructions would just dilute the budget below with garbage
 * the model can't use. Only "text"-kind log lines are included -
 * tool_call/tool entries are JSON-shaped and equally meaningless to it.
 *
 * Budget is a rough char-based proxy for generation_module.c's
 * SEQ_LEN=128 (prompt tokens + generated tokens share that one cap) -
 * not exact token accounting, just enough headroom left for the model
 * to actually generate something instead of hitting the cap on prompt
 * alone. */
static char *build_iqabod_prompt(const char *log_path) {
    char *raw_turns[MAX_IQABOD_LOG_LINES];
    int n = 0;

    FILE *lf = fopen(log_path, "r");
    if (lf) {
        char line[MAX_LINE];
        while (n < MAX_IQABOD_LOG_LINES && fgets(line, sizeof(line), lf)) {
            line[strcspn(line, "\n")] = '\0';
            char *p1 = strchr(line, '|'); if (!p1) continue;
            char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            char *p3 = strchr(p2 + 1, '|'); if (!p3) continue;
            *p1 = '\0'; *p2 = '\0'; *p3 = '\0';
            const char *kind = p1 + 1;
            if (strcmp(kind, "text") != 0) continue;
            raw_turns[n++] = pipe_unescape(p3 + 1);
        }
        fclose(lf);
    }

    /* Walk backward from the most recent turn, keeping only what fits
     * the char budget, then re-emit in oldest-first order so the
     * concatenation still reads as a coherent transcript. */
    size_t budget = MAX_IQABOD_PROMPT_CHARS;
    int first_kept = n;
    for (int i = n - 1; i >= 0; i--) {
        size_t tlen = strlen(raw_turns[i]) + 1; /* +1 for the joining space */
        if (tlen > budget) break;
        budget -= tlen;
        first_kept = i;
    }

    size_t out_len = 1;
    for (int i = first_kept; i < n; i++) out_len += strlen(raw_turns[i]) + 1;
    char *out = malloc(out_len);
    out[0] = '\0';
    for (int i = first_kept; i < n; i++) {
        strcat(out, raw_turns[i]);
        if (i < n - 1) strcat(out, " ");
    }
    for (int i = 0; i < n; i++) free(raw_turns[i]);
    return out;
}

int main(void) {
    resolve_root();

    char state_path[PATH_BUF], log_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/pieces/world_01/session_01/chat/state.txt", project_root);
    snprintf(log_path, sizeof(log_path), "%s/pieces/world_01/session_01/chat/context_log.txt", project_root);

    char buffer[MAX_BUFFER];
    read_state_field(state_path, "input_buffer", buffer, sizeof(buffer));
    if (strlen(buffer) == 0) return 0;

    /* This is the single Enter-handler op main_loop.pal calls
     * unconditionally on every Enter keypress (mirrors mutaclsym's own
     * choice.c self-filtering on every tick rather than the .pal script
     * branching on state - prisc+x's beq is exact-int-equality only, it
     * can't compare strings or piece state, so any "what does Enter mean
     * right now" decision has to live in C, not the script). If a tool
     * permission is pending, Enter means y/n, not a new message. */
    char ai_state[MAX_FIELD];
    read_state_field(state_path, "ai_state", ai_state, sizeof(ai_state));
    if (strcmp(ai_state, "PENDING_PERM") == 0) {
        char trimmed[MAX_BUFFER];
        snprintf(trimmed, sizeof(trimmed), "%s", buffer);
        size_t tlen = strlen(trimmed);
        while (tlen > 0 && (trimmed[tlen - 1] == ' ' || trimmed[tlen - 1] == '\n')) trimmed[--tlen] = '\0';
        for (size_t i = 0; trimmed[i]; i++) trimmed[i] = (char)tolower((unsigned char)trimmed[i]);

        write_state_field(state_path, "input_buffer", "");
        if (strcmp(trimmed, "y") == 0 || strcmp(trimmed, "yes") == 0) {
            char cmd[PATH_BUF];
            snprintf(cmd, sizeof(cmd), "'%s/ops/+x/execute_tool.+x'", project_root);
            int rc = system(cmd);
            (void)rc;
        } else if (strcmp(trimmed, "n") == 0 || strcmp(trimmed, "no") == 0) {
            char cmd[PATH_BUF];
            snprintf(cmd, sizeof(cmd), "'%s/ops/+x/deny_tool.+x'", project_root);
            int rc = system(cmd);
            (void)rc;
        } else {
            write_state_field(state_path, "sys_msg", "Invalid response. Type 'y' or 'n'.");
        }
        return 0;
    }

    /* "/model <id>" is a command, not a chat message - delegate entirely
     * to switch_model.+x (including clearing input_buffer) and stop. */
    if (strncmp(buffer, "/model ", 7) == 0) {
        char cmd[PATH_BUF + MAX_BUFFER];
        snprintf(cmd, sizeof(cmd), "'%s/ops/+x/switch_model.+x' '%s'", project_root, buffer + 7);
        int rc = system(cmd);
        (void)rc;
        return 0;
    }

    /* Ordinary chat message. */
    append_log_turn(log_path, "user", "text", NULL, buffer);
    write_state_field(state_path, "input_buffer", "");
    write_state_field(state_path, "ai_state", "THINKING");
    write_state_field(state_path, "sys_msg", "Querying AI...");

    char provider_kind[MAX_FIELD], api_url[MAX_FIELD], model_name[MAX_FIELD];
    read_state_field(state_path, "provider_kind", provider_kind, sizeof(provider_kind));
    read_state_field(state_path, "current_api_url", api_url, sizeof(api_url));
    read_state_field(state_path, "current_model_name", model_name, sizeof(model_name));

    char prompt_path[PATH_BUF];
    snprintf(prompt_path, sizeof(prompt_path), "%s/pieces/world_01/session_01/chat/prompt.json", project_root);
    FILE *pf = fopen(prompt_path, "w");
    if (!pf) return 1;

    char full_url[MAX_FIELD * 2 + 128];

    if (strcmp(provider_kind, "iqabod") == 0) {
        /* Wholly different shape from the other three: no JSON request
         * file (prompt.json stays empty), no connect_op/curl, no HTTP.
         * api_url/model_name are repurposed per model_list.txt's header
         * comment - api_url is IQABOD's own project root (this process
         * must chdir() there since main_orchestrator.c resolves
         * config.txt/curriculum paths/./+x/generation_module.+x all
         * relative to CWD, with no root-resolution of its own), and
         * model_name is the curriculum file path relative to that root. */
        fclose(pf);
        char *prompt_text = build_iqabod_prompt(log_path);

        char iqabod_response_path[PATH_BUF], iqabod_prompt_path[PATH_BUF];
        snprintf(iqabod_response_path, sizeof(iqabod_response_path), "%s/pieces/world_01/session_01/chat/iqabod_response.txt", project_root);
        snprintf(iqabod_prompt_path, sizeof(iqabod_prompt_path), "%s/pieces/world_01/session_01/chat/iqabod_prompt.tmp", project_root);

        /* check_response.c needs the exact prompt text back to strip it
         * off the front of "Final generated text: <prompt> <tokens...>"
         * (generation_module.c builds output as strcpy(output, prompt)
         * before appending generated tokens) - persisted here since
         * ops have no memory between invocations. */
        FILE *ppf = fopen(iqabod_prompt_path, "w");
        if (ppf) { fputs(prompt_text ? prompt_text : "", ppf); fclose(ppf); }

        pid_t iqabod_pid = fork();
        if (iqabod_pid == 0) {
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, STDIN_FILENO); dup2(devnull, STDERR_FILENO); }
            int outfd = open(iqabod_response_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outfd >= 0) dup2(outfd, STDOUT_FILENO);
            if (devnull >= 0 && devnull > STDERR_FILENO) close(devnull);
            if (outfd >= 0 && outfd > STDERR_FILENO) close(outfd);
            if (chdir(api_url) != 0) _exit(127);
            char *orch_path = NULL;
            if (asprintf(&orch_path, "%s/+x/main_orchestrator.+x", api_url) == -1) _exit(127);
            /* Passed as a single raw execl() argv element, NOT through a
             * shell - unlike main_orchestrator.c's own internal re-exec
             * of generation_module.+x (via system(), which embeds this
             * same prompt unescaped inside a quoted string), so quotes/
             * backticks/etc. in the prompt can't break parsing here. */
            execl(orch_path, orch_path, "generate", model_name, "0.8", "60", prompt_text ? prompt_text : "", (char *)NULL);
            _exit(127);
        }
        free(prompt_text);

        char iqabod_pid_str[32];
        snprintf(iqabod_pid_str, sizeof(iqabod_pid_str), "%d", iqabod_pid);
        write_state_field(state_path, "curl_pid", iqabod_pid_str);
        return 0;
    } else if (strcmp(provider_kind, "gemini") == 0) {
        build_gemini_request(pf, log_path);
        char *gemini_key = getenv("GEMINI_API_KEY");
        /* getenv()'s result is unbounded from gcc's static view, so
         * -Wformat-truncation can't be fully satisfied by sizing alone -
         * full_url is already sized with headroom above any real API key
         * length, snprintf truncates safely in the extreme case regardless. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(full_url, sizeof(full_url), "%s/v1beta/models/%s:generateContent?key=%s",
                 api_url, model_name, gemini_key ? gemini_key : "");
#pragma GCC diagnostic pop
    } else if (strcmp(provider_kind, "llamacpp") == 0) {
        build_llamacpp_request(pf, log_path);
        snprintf(full_url, sizeof(full_url), "%s/completion", api_url);
    } else {
        build_ollama_request(pf, log_path, model_name);
        snprintf(full_url, sizeof(full_url), "%s/api/chat", api_url);
    }
    fclose(pf);

    char response_path[PATH_BUF];
    snprintf(response_path, sizeof(response_path), "%s/pieces/world_01/session_01/chat/llm_response.json", project_root);

    pid_t pid = fork();
    if (pid == 0) {
        /* send_message.+x is itself invoked via prisc+x's popen(), so
         * stdout here is the write end of that pipe. Without detaching it,
         * this grandchild (and curl under it) would keep that pipe open
         * long after send_message.+x itself exits, and prisc+x's
         * popen()/fgets() loop would block on it for the whole network
         * call - exactly the blocking behavior this design exists to
         * avoid. Redirect to /dev/null and start a new session so this
         * subprocess is fully independent of the short-lived parent. */
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        char *connect_op_path = NULL;
        if (asprintf(&connect_op_path, "%s/ops/+x/connect_op.+x", project_root) == -1) _exit(127);
        execl(connect_op_path, connect_op_path, full_url, prompt_path, response_path, (char *)NULL);
        _exit(127);
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    write_state_field(state_path, "curl_pid", pid_str);

    return 0;
}
