/* check_response - polls for the in-flight request send_message.+x
 * kicked off. Called every tick from main_loop.pal (self-filters: no-op
 * unless ai_state=THINKING, same "runs unconditionally, checks its own
 * relevance" convention mutaclsym's choice.c/move_player.c already use).
 *
 * Ops have no persistent memory between invocations, so "is curl done
 * yet" can only be answered by checking whether the PID send_message.+x
 * recorded is still alive - this process isn't that child's real parent
 * (send_message.+x already exited), so waitpid() isn't available; a
 * plain kill(pid, 0) liveness probe is the only option, which is enough
 * since connect_op/curl are bounded one-shot subprocesses, not daemons.
 *
 * Parses the response per provider_kind (ollama/gemini/llamacpp/iqabod) -
 * see send_message.c's header comment for what request shape each one
 * sent. iqabod is the odd one out: its response isn't JSON at all, and
 * (unlike llamacpp) its output is never persona-JSON-shaped either -
 * IQABOD's curricula are raw word-continuation models, not
 * instruction-tuned to emit a tool/args/response schema - so that branch
 * only ever produces plain assistant text.
 *
 * Self-contained: own root resolution, own constants, no shared headers.
 * Usage: check_response.+x (no args) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAX_LINE 4096
#define MAX_FIELD 256

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

static void append_log_turn(const char *log_path, const char *role, const char *kind, const char *tool_name, const char *content) {
    FILE *f = fopen(log_path, "a");
    if (!f) return;
    char *esc_content = pipe_escape(content);
    fprintf(f, "%s|%s|%s|%s\n", role, kind, tool_name ? tool_name : "", esc_content);
    free(esc_content);
    fclose(f);
}

/* popen()'s a compiled op and returns its full stdout, trimmed of a
 * trailing newline - the same op-calls-op-via-subprocess convention
 * choice.c/pdl_reader.c already use, not a library call (no shared
 * headers to link against). */
static char *run_tool_capture(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;
    char *buf = malloc(65536);
    size_t total = 0;
    size_t n;
    while ((n = fread(buf + total, 1, 65536 - total - 1, pipe)) > 0) {
        total += n;
        if (total >= 65535) break;
    }
    buf[total] = '\0';
    pclose(pipe);
    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r')) buf[--total] = '\0';
    return buf;
}

int main(void) {
    resolve_root();

    char state_path[PATH_BUF], log_path[PATH_BUF], response_path[PATH_BUF];
    snprintf(state_path, sizeof(state_path), "%s/pieces/world_01/session_01/chat/state.txt", project_root);
    snprintf(log_path, sizeof(log_path), "%s/pieces/world_01/session_01/chat/context_log.txt", project_root);
    snprintf(response_path, sizeof(response_path), "%s/pieces/world_01/session_01/chat/llm_response.json", project_root);

    char ai_state[MAX_FIELD];
    read_state_field(state_path, "ai_state", ai_state, sizeof(ai_state));
    if (strcmp(ai_state, "THINKING") != 0) return 0;

    char pid_str[MAX_FIELD];
    read_state_field(state_path, "curl_pid", pid_str, sizeof(pid_str));
    pid_t pid = (pid_t)atoi(pid_str);
    if (pid > 0) {
        if (kill(pid, 0) == 0 || errno != ESRCH) return 0; /* still running */
    }

    char provider_kind[MAX_FIELD];
    read_state_field(state_path, "provider_kind", provider_kind, sizeof(provider_kind));

    char json_parser_cmd[PATH_BUF * 2];
    char *content = NULL;      /* plain text to show, if no tool call */
    char *tool_name = NULL;
    char *tool_args = NULL;

    if (strcmp(provider_kind, "iqabod") == 0) {
        /* iqabod: plain-text file, not JSON. generation_module.c prints
         * several stdout lines per run (progress, the final line, then
         * an attention-map confirmation after it) - match specifically
         * on the "Final generated text: " prefix rather than assuming
         * it's the last line. That line is prompt + " " + generated
         * tokens all on one line (output starts as strcpy(output,
         * prompt) - see generation_module.c), so strip the prompt back
         * off the front using the exact prompt text send_message.c
         * persisted alongside it, rather than guessing where it ends. */
        char iqabod_response_path[PATH_BUF], iqabod_prompt_path[PATH_BUF];
        snprintf(iqabod_response_path, sizeof(iqabod_response_path), "%s/pieces/world_01/session_01/chat/iqabod_response.txt", project_root);
        snprintf(iqabod_prompt_path, sizeof(iqabod_prompt_path), "%s/pieces/world_01/session_01/chat/iqabod_prompt.tmp", project_root);

        char *raw_out = read_full_file(iqabod_response_path);
        char *sent_prompt = read_full_file(iqabod_prompt_path);
        const char *marker = "Final generated text: ";
        char *hit = raw_out ? strstr(raw_out, marker) : NULL;
        if (hit) {
            char *line_start = hit + strlen(marker);
            char *line_end = strchr(line_start, '\n');
            size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

            size_t prompt_len = sent_prompt ? strlen(sent_prompt) : 0;
            const char *new_part = line_start;
            size_t new_len = line_len;
            if (prompt_len > 0 && line_len >= prompt_len && strncmp(line_start, sent_prompt, prompt_len) == 0) {
                new_part = line_start + prompt_len;
                new_len = line_len - prompt_len;
                if (new_len > 0 && new_part[0] == ' ') { new_part++; new_len--; }
            }
            content = malloc(new_len + 1);
            memcpy(content, new_part, new_len);
            content[new_len] = '\0';
        }
        free(raw_out);
        free(sent_prompt);
        remove(iqabod_prompt_path);
    } else if (strcmp(provider_kind, "gemini") == 0) {
        /* Gemini shape: candidates[0].content.parts[0].text or
         * .functionCall (name/args directly, not array-indexed the way
         * OpenAI-style tool_calls are - a single object, not a list). */
        snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'candidates[0].content.parts[0].text'", project_root, response_path);
        content = run_tool_capture(json_parser_cmd);

        snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'candidates[0].content.parts[0].functionCall'", project_root, response_path);
        char *func_call = run_tool_capture(json_parser_cmd);
        if (func_call && strlen(func_call) > 2) {
            char tmp_path[PATH_BUF];
            snprintf(tmp_path, sizeof(tmp_path), "%s/pieces/world_01/session_01/chat/tool_calls.tmp", project_root);
            FILE *tf = fopen(tmp_path, "w");
            if (tf) { fputs(func_call, tf); fclose(tf); }
            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'name'", project_root, tmp_path);
            tool_name = run_tool_capture(json_parser_cmd);
            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'args'", project_root, tmp_path);
            tool_args = run_tool_capture(json_parser_cmd);
            remove(tmp_path);
        }
        free(func_call);
    } else if (strcmp(provider_kind, "llamacpp") == 0) {
        /* /completion's response is flat {"content": "...", ...} - the
         * model's own content is itself the persona-JSON blob
         * ({"tool":...,"args":...} or {"response":...}), matching
         * cpp-llm's proven prompt-JSON convention (no native tool schema
         * on this path at all). */
        snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'content'", project_root, response_path);
        char *raw_content = run_tool_capture(json_parser_cmd);
        if (raw_content && strlen(raw_content) > 0) {
            char tmp_path[PATH_BUF];
            snprintf(tmp_path, sizeof(tmp_path), "%s/pieces/world_01/session_01/chat/llm_content.tmp", project_root);
            FILE *tf = fopen(tmp_path, "w");
            if (tf) { fputs(raw_content, tf); fclose(tf); }

            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'tool'", project_root, tmp_path);
            tool_name = run_tool_capture(json_parser_cmd);
            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'args'", project_root, tmp_path);
            tool_args = run_tool_capture(json_parser_cmd);

            if (!tool_name || strlen(tool_name) == 0) {
                snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'response'", project_root, tmp_path);
                content = run_tool_capture(json_parser_cmd);
                if (!content || strlen(content) == 0) { free(content); content = strdup(raw_content); }
            }
            remove(tmp_path);
        }
        free(raw_content);
    } else {
        /* Native Ollama /api/chat: message.content / message.tool_calls
         * (array of {"function":{"name":...,"arguments":...}}) - proven
         * in groq-ollama. */
        snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'message.content'", project_root, response_path);
        content = run_tool_capture(json_parser_cmd);

        snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' 'message.tool_calls'", project_root, response_path);
        char *tool_calls = run_tool_capture(json_parser_cmd);
        if (tool_calls && strlen(tool_calls) > 2) {
            char tmp_path[PATH_BUF];
            snprintf(tmp_path, sizeof(tmp_path), "%s/pieces/world_01/session_01/chat/tool_calls.tmp", project_root);
            FILE *tf = fopen(tmp_path, "w");
            if (tf) { fputs(tool_calls, tf); fclose(tf); }
            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' '[0].function.name'", project_root, tmp_path);
            tool_name = run_tool_capture(json_parser_cmd);
            snprintf(json_parser_cmd, sizeof(json_parser_cmd), "'%s/ops/+x/json_parser.+x' '%s' '[0].function.arguments'", project_root, tmp_path);
            tool_args = run_tool_capture(json_parser_cmd);
            remove(tmp_path);
        }
        free(tool_calls);
    }

    /* Unified from here regardless of provider: same PENDING_PERM /
     * IDLE-with-text / error handling every provider_kind funnels into. */
    if (tool_name && strlen(tool_name) > 0) {
        write_state_field(state_path, "pending_tool_name", tool_name);
        write_state_field(state_path, "pending_tool_args", tool_args ? tool_args : "{}");
        write_state_field(state_path, "ai_state", "PENDING_PERM");
        char msg[MAX_FIELD];
        snprintf(msg, sizeof(msg), "Execute %s? (y/n)", tool_name);
        write_state_field(state_path, "sys_msg", msg);
        append_log_turn(log_path, "assistant", "tool_call", tool_name, tool_args ? tool_args : "{}");
    } else if (content && strlen(content) > 0) {
        append_log_turn(log_path, "assistant", "text", NULL, content);
        write_state_field(state_path, "ai_state", "IDLE");
        write_state_field(state_path, "sys_msg", "Response received.");
    } else {
        write_state_field(state_path, "ai_state", "IDLE");
        write_state_field(state_path, "sys_msg", "API Error: empty or unparseable response.");
    }

    free(content);
    free(tool_name);
    free(tool_args);
    return 0;
}
