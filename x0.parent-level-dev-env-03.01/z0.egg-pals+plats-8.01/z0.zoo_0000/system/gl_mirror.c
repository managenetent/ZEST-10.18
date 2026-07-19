/* gl_mirror - the ONLY file in zoo_0000 allowed to call GL/GLUT
 * primitives, direct port of mutaclsym's own system/gl_mirror.c (read
 * in full before writing this - see that file's own header comment
 * for the complete GOVERNING CONSTRAINT citation and the real
 * wraith_gl.c lineage this whole pattern traces back to). Its entire
 * job: poll ops/compose_rgb_frame.+x's output file
 * (pieces/display/rgb_frame.raw, a raw RGBA32 buffer - zero GL calls
 * went into producing those bytes) and blit it as one textured quad.
 *
 * Renders the LEVEL ONLY (map + xlector) - see ops/compose_rgb_frame.c's
 * own header comment for why pets are deliberately not drawn here.
 *
 * Differences from mutaclsym's own copy:
 *   - WIDTH/HEIGHT match THIS project's own compose_rgb_frame.c
 *     dimensions (20x12 tiles at 16px + 1 header/1 footer text row,
 *     not mutaclsym's 40x16-viewport/2-footer-row layout).
 *   - NO gl_focus.lock mechanism. mutaclsym's own copy re-added a
 *     focus-lock file after finding real 1.TPMOS's joystick_input.c
 *     needs one (a joystick reading a raw device node has no window-
 *     focus concept at all) - zoo_0000 has no joystick reader, so
 *     there is no load-bearing reason for the lock here; OS window
 *     focus alone correctly arbitrates keyboard-vs-keyboard between
 *     this window and system/keyboard_input.c, matching the SAME
 *     conclusion mutaclsym itself reached for plain keyboard-vs-
 *     keyboard arbitration (see that project's own dox/00-HANDOFF.md
 *     for the real regression this caused there when over-applied -
 *     not repeating it here from the start). If a joystick reader is
 *     ever built for this project, port the lock mechanism back in
 *     THEN, matching mutaclsym's own real 1.TPMOS-sourced precedent -
 *     not preemptively.
 *   - Window title "zoo_0000 RGB mirror". Everything else (pulse-file
 *     watch by size not mtime, checksum/receipt pattern, texture
 *     upload/display/reshape shape) is kept byte-for-byte, since none
 *     of it is mutaclsym-specific. */
#define _GNU_SOURCE
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)

/* REAL FIX (direct user report, same as mutaclsym's own gl_mirror.c -
 * see that file's own comment here for the full account): WIDTH/HEIGHT
 * used to be hardcoded, matching only ops/compose_rgb_frame.c's own
 * fixed size - shared-ops/chtpm_rgb_render.c writes a DIFFERENT, taller
 * size to the same rgb_frame.raw path, and hardcoding one renderer's
 * size here silently clipped the other's. Now read dynamically from
 * rgb_frame.receipt.txt's own frame_w/frame_h every load (see
 * read_kv_int()/load_texture() below) - same shared-ops/dump_rgb_png.c
 * pattern. DEFAULT_WIDTH/DEFAULT_HEIGHT are only the startup fallback. */
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 224

#define ARROW_LEFT  1000
#define ARROW_RIGHT 1001
#define ARROW_UP    1002
#define ARROW_DOWN  1003

static char project_root[MAX_PATH] = ".";
static char frame_source[PATH_BUF];
static char frame_pulse[PATH_BUF];
static char source_receipt[PATH_BUF];
static char display_receipt[PATH_BUF];
static char keyboard_history[PATH_BUF];
static char chtpm_keyboard_history[PATH_BUF];
/* TEMPORARY diagnostic - see mutaclsym's own gl_mirror.c for the full
 * why (direct user report of arrow-up incorrectly activating a
 * button, not reproducible via file-injection testing). Remove once
 * root-caused. */
static char key_debug_log[PATH_BUF];

static GLuint texture_id;
static unsigned char *frame_buffer = NULL;
static long last_pulse_size = -1;
static volatile sig_atomic_t g_shutdown_requested = 0;
static int g_frame_w = DEFAULT_WIDTH;
static int g_frame_h = DEFAULT_HEIGHT;
static int g_window_w = DEFAULT_WIDTH;
static int g_window_h = DEFAULT_HEIGHT;
static int g_texture_view_x = 0;
static int g_texture_view_y = 0;
static int g_texture_view_w = DEFAULT_WIDTH;
static int g_texture_view_h = DEFAULT_HEIGHT;
static unsigned long long g_loaded_frame_checksum = 0;
static size_t g_loaded_frame_bytes = 0;
static int g_loaded_frame_partial = 0;

/* PAL-NET (see yz.muchiverse/2.muchi-verse/PAL-NET-STANDARD.txt - read
 * that doc before touching any of this) - gl_mirror IS the real "zoo
 * window" a player drags a pet onto, so it's the presence/geometry
 * SERVER side: binds a listen socket, tells any connected client
 * (an egg-pals-13/zoo_0000 pet window mid-drag) this window's own
 * live screen rect via GEOM lines. */
#define PALNET_ZOO_BASE_PORT 9900
#define PALNET_BIND_ATTEMPTS 200
#define PALNET_MAX_CLIENTS 16
#define PALNET_HEARTBEAT_SEC 5

static char g_presence_path[PATH_BUF] = "";
static char g_node_id[128] = "";
static int g_listen_fd = -1;
static int g_bound_port = 0;
static int g_client_fds[PALNET_MAX_CLIENTS];
static int g_last_geom_x = 0, g_last_geom_y = 0, g_last_geom_w = 0, g_last_geom_h = 0;
static int g_have_geom = 0;
static time_t g_last_heartbeat = 0;

static int read_kv_int(const char *path, const char *key, int def) {
    FILE *f = fopen(path, "r");
    if (!f) return def;
    char line[256];
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

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
    snprintf(frame_source, sizeof(frame_source), "%s/pieces/display/rgb_frame.raw", project_root);
    snprintf(source_receipt, sizeof(source_receipt), "%s/pieces/display/rgb_frame.receipt.txt", project_root);
    /* REAL FIX - see mutaclsym's own gl_mirror.c for the full account
     * (same shared bug, same fix): this used to watch pieces/display/
     * frame_changed.txt, the UPSTREAM trigger shared-ops/
     * chtpm_rgb_render.c/ops/compose_rgb_frame.c themselves react to -
     * real wraith_gl.c watches a SEPARATE, DOWNSTREAM
     * rgb_frame_changed.txt instead, grown only by its own rgb daemon
     * AFTER output is complete. Confirmed via live checksum trace this
     * window could otherwise "consume" a pulse before the real
     * renderer had finished, permanently stalling until an unrelated
     * later trigger fired. */
    snprintf(frame_pulse, sizeof(frame_pulse), "%s/pieces/display/rgb_frame_changed.txt", project_root);
    snprintf(display_receipt, sizeof(display_receipt), "%s/pieces/display/gl_display.receipt.txt", project_root);
    snprintf(keyboard_history, sizeof(keyboard_history), "%s/pieces/apps/player_app/history.txt", project_root);
    snprintf(chtpm_keyboard_history, sizeof(chtpm_keyboard_history), "%s/pieces/keyboard/history.txt", project_root);
    snprintf(key_debug_log, sizeof(key_debug_log), "%s/pieces/display/gl_key_debug.log", project_root);
}

/* PAL-NET-STANDARD.txt sec. 1 - mirrors shared-ops/pet_export.c's own
 * resolve_exchange_root() exactly (same reasoning: a shared location
 * siblings can all find without either project needing to know the
 * other's own internal layout) - default is one level above this
 * project's own root, PRISC_NET_ROOT overrides it. */
static void resolve_presence_root(char *out, size_t out_sz) {
    const char *env = getenv("PRISC_NET_ROOT");
    if (env && env[0]) { snprintf(out, out_sz, "%s", env); return; }
    char parent[MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", project_root);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = '\0';
    snprintf(out, out_sz, "%s/net/presence", parent);
}

/* mkdir -p, one level at a time - this family's own convention
 * elsewhere is a plain mkdir() call assuming the parent already
 * exists, but net/presence/ is a genuinely NEW shared directory
 * nothing else creates first. */
static void mkdir_p(const char *path) {
    char tmp[PATH_BUF];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* PAL-NET-STANDARD.txt sec. 2 - REAL, NEW retry-scan logic, NOT a port
 * of p2p-net's own bind_server_socket() (that function binds once to a
 * single fixed, human-pre-configured port and just fails otherwise -
 * checked directly, see this project's own PAL-NET-STANDARD.txt sec. 2
 * for why that doesn't fit this task). Returns the bound, listening fd
 * on success (and writes the actual bound port into *out_port), or -1
 * if no port in the whole scan range was free. */
static int bind_with_retry(int base_port, int *out_port) {
    for (int attempt = 0; attempt < PALNET_BIND_ATTEMPTS; attempt++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons((uint16_t)(base_port + attempt));

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            if (listen(fd, 8) == 0) {
                int flags = fcntl(fd, F_GETFL, 0);
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                *out_port = base_port + attempt;
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

/* PAL-NET-STANDARD.txt sec. 1 - written once at startup after a
 * successful bind, refreshed on a heartbeat (see maybe_heartbeat()),
 * removed on clean exit (see palnet_shutdown()). */
static void write_presence_file(void) {
    if (!g_presence_path[0]) return;
    FILE *f = fopen(g_presence_path, "w");
    if (!f) return;
    fprintf(f, "kind=zoo\n");
    fprintf(f, "project_id=zoo_0000\n");
    fprintf(f, "host=127.0.0.1\n");
    fprintf(f, "port=%d\n", g_bound_port);
    fprintf(f, "pid=%d\n", (int)getpid());
    fprintf(f, "last_seen=%ld\n", (long)time(NULL));
    fclose(f);
}

static void palnet_init(void) {
    char presence_dir[PATH_BUF];
    resolve_presence_root(presence_dir, sizeof(presence_dir));
    mkdir_p(presence_dir);

    for (int i = 0; i < PALNET_MAX_CLIENTS; i++) g_client_fds[i] = -1;

    g_listen_fd = bind_with_retry(PALNET_ZOO_BASE_PORT, &g_bound_port);
    if (g_listen_fd < 0) {
        fprintf(stderr, "gl_mirror: PAL-NET bind failed after %d attempts - drag-and-drop discovery disabled this run\n", PALNET_BIND_ATTEMPTS);
        return;
    }

    snprintf(g_node_id, sizeof(g_node_id), "zoo_0000-zoo-%d", (int)getpid());
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(g_presence_path, sizeof(g_presence_path), "%s/%s.txt", presence_dir, g_node_id);
#pragma GCC diagnostic pop
    write_presence_file();
    g_last_heartbeat = time(NULL);
}

static void palnet_shutdown(void) {
    for (int i = 0; i < PALNET_MAX_CLIENTS; i++) {
        if (g_client_fds[i] >= 0) close(g_client_fds[i]);
    }
    if (g_listen_fd >= 0) close(g_listen_fd);
    if (g_presence_path[0]) unlink(g_presence_path);
}

static void maybe_heartbeat(void) {
    time_t now = time(NULL);
    if (now - g_last_heartbeat >= PALNET_HEARTBEAT_SEC) {
        g_last_heartbeat = now;
        write_presence_file();
    }
}

static void palnet_accept_clients(void) {
    if (g_listen_fd < 0) return;
    for (;;) {
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0) return;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int slot = -1;
        for (int i = 0; i < PALNET_MAX_CLIENTS; i++) {
            if (g_client_fds[i] < 0) { slot = i; break; }
        }
        if (slot < 0) { close(fd); continue; }
        g_client_fds[slot] = fd;
        /* Immediately tell a newly-connected client the CURRENT
         * geometry, if we already have one - it shouldn't have to wait
         * for the next real change to learn where this window is. */
        if (g_have_geom) {
            char line[128];
            int n = snprintf(line, sizeof(line), "GEOM|%d|%d|%d|%d\n", g_last_geom_x, g_last_geom_y, g_last_geom_w, g_last_geom_h);
            ssize_t sent = send(fd, line, (size_t)n, MSG_NOSIGNAL);
            (void)sent;
        }
    }
}

/* Detect a client that closed its own end (recv()==0) so its slot can
 * be freed - we never expect real data FROM a client in this task (see
 * PAL-NET-STANDARD.txt sec. 3, HELLO is client-to-server info-only,
 * not consulted here yet), just its own disconnect. */
static void palnet_reap_clients(void) {
    char buf[64];
    for (int i = 0; i < PALNET_MAX_CLIENTS; i++) {
        if (g_client_fds[i] < 0) continue;
        ssize_t n = recv(g_client_fds[i], buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(g_client_fds[i]);
            g_client_fds[i] = -1;
        }
    }
}

/* Broadcasts a fresh GEOM line to every connected client ONLY when the
 * geometry actually changed since last call - PAL-NET-STANDARD.txt
 * sec. 3's own "never send unconditionally on a fixed timer" rule,
 * matching this same session's own render-throttling lesson. */
static void palnet_maybe_broadcast_geom(int x, int y, int w, int h) {
    if (g_have_geom && x == g_last_geom_x && y == g_last_geom_y && w == g_last_geom_w && h == g_last_geom_h) return;
    g_have_geom = 1;
    g_last_geom_x = x; g_last_geom_y = y; g_last_geom_w = w; g_last_geom_h = h;

    char line[128];
    int n = snprintf(line, sizeof(line), "GEOM|%d|%d|%d|%d\n", x, y, w, h);
    for (int i = 0; i < PALNET_MAX_CLIENTS; i++) {
        if (g_client_fds[i] < 0) continue;
        ssize_t sent = send(g_client_fds[i], line, (size_t)n, MSG_NOSIGNAL);
        (void)sent;
    }
}

/* CHTPM-BRIDGE ADDITION (see chtpm-to-pal-layout-plan.txt and
 * !.handoff-doc-j17.txt's own wraith-alpha research for the why): also
 * appends "KEY_PRESSED: N" to pieces/keyboard/history.txt - confirmed,
 * via direct read of real 1.TPMOS's own wraith_gl.c
 * (append_keyboard_event()), that this is the REAL mechanism a GL
 * window uses to feed chtpm's own input pipeline: wraith_gl.c writes
 * into the EXACT SAME file chtpm_parser.c itself reads, nothing more
 * elaborate. Before this fix, this window's own keyboard capture was a
 * dead end in chtpm mode. */
static void append_key(int key) {
    FILE *f = fopen(keyboard_history, "a");
    if (f) { fprintf(f, "%d\n", key); fclose(f); }

    FILE *cf = fopen(chtpm_keyboard_history, "a");
    if (cf) { fprintf(cf, "KEY_PRESSED: %d\n", key); fclose(cf); }
}

static int map_special_key(int key) {
    if (key == GLUT_KEY_LEFT) return ARROW_LEFT;
    if (key == GLUT_KEY_RIGHT) return ARROW_RIGHT;
    if (key == GLUT_KEY_UP) return ARROW_UP;
    if (key == GLUT_KEY_DOWN) return ARROW_DOWN;
    return 0;
}

static unsigned long long checksum_buffer(const unsigned char *buffer, size_t len) {
    unsigned long long hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (unsigned long long)buffer[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

static void update_texture_viewport(int win_w, int win_h) {
    double texture_aspect = (double)g_frame_w / (double)g_frame_h;
    double window_aspect;

    if (win_w <= 0) win_w = g_frame_w;
    if (win_h <= 0) win_h = g_frame_h;

    g_window_w = win_w;
    g_window_h = win_h;
    window_aspect = (double)win_w / (double)win_h;

    if (window_aspect > texture_aspect) {
        g_texture_view_h = win_h;
        g_texture_view_w = (int)((double)win_h * texture_aspect + 0.5);
        g_texture_view_x = (win_w - g_texture_view_w) / 2;
        g_texture_view_y = 0;
    } else {
        g_texture_view_w = win_w;
        g_texture_view_h = (int)((double)win_w / texture_aspect + 0.5);
        g_texture_view_x = 0;
        g_texture_view_y = (win_h - g_texture_view_h) / 2;
    }
}

static void write_gl_display_receipt(const char *event) {
    FILE *f = fopen(display_receipt, "w");
    time_t now;
    struct tm *tm_now;
    char stamp[64];
    double scale_x = 0.0, scale_y = 0.0;

    if (!f) return;
    now = time(NULL);
    tm_now = gmtime(&now);
    if (tm_now) strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", tm_now);
    else snprintf(stamp, sizeof(stamp), "unknown");
    if (g_frame_w > 0) scale_x = (double)g_texture_view_w / (double)g_frame_w;
    if (g_frame_h > 0) scale_y = (double)g_texture_view_h / (double)g_frame_h;

    fprintf(f, "receipt_type=gl_display_upload\n");
    fprintf(f, "generated_by=gl_mirror\n");
    fprintf(f, "generated_at_epoch=%ld\n", (long)now);
    fprintf(f, "generated_at_iso_utc=%s\n", stamp);
    fprintf(f, "event=%s\n", event ? event : "unknown");
    fprintf(f, "source_rgba32=%s\n", frame_source);
    fprintf(f, "texture_width_px=%d\n", g_frame_w);
    fprintf(f, "texture_height_px=%d\n", g_frame_h);
    fprintf(f, "expected_rgba_bytes=%d\n", g_frame_w * g_frame_h * 4);
    fprintf(f, "loaded_rgba_bytes=%lu\n", (unsigned long)g_loaded_frame_bytes);
    fprintf(f, "loaded_frame_partial=%d\n", g_loaded_frame_partial);
    fprintf(f, "loaded_rgba_checksum_fnv1a64=0x%016llX\n", g_loaded_frame_checksum);
    fprintf(f, "window_w=%d\n", g_window_w);
    fprintf(f, "window_h=%d\n", g_window_h);
    fprintf(f, "texture_view_x=%d\n", g_texture_view_x);
    fprintf(f, "texture_view_y=%d\n", g_texture_view_y);
    fprintf(f, "texture_view_w=%d\n", g_texture_view_w);
    fprintf(f, "texture_view_h=%d\n", g_texture_view_h);
    fprintf(f, "display_scale_x=%.6f\n", scale_x);
    fprintf(f, "display_scale_y=%.6f\n", scale_y);
    fprintf(f, "render_origin=top_left_texture_to_gl_viewport\n");
    fclose(f);
}

static void load_texture(void) {
    int new_w = read_kv_int(source_receipt, "frame_w", DEFAULT_WIDTH);
    int new_h = read_kv_int(source_receipt, "frame_h", DEFAULT_HEIGHT);
    if (new_w > 0 && new_h > 0 && (new_w != g_frame_w || new_h != g_frame_h)) {
        g_frame_w = new_w;
        g_frame_h = new_h;
        free(frame_buffer);
        frame_buffer = malloc((size_t)g_frame_w * g_frame_h * 4);
        glutReshapeWindow(g_frame_w, g_frame_h);
    }

    FILE *f = fopen(frame_source, "rb");

    if (!frame_buffer) frame_buffer = malloc((size_t)g_frame_w * g_frame_h * 4);
    if (!frame_buffer) {
        if (f) fclose(f);
        return;
    }

    if (!f) {
        memset(frame_buffer, 0, (size_t)g_frame_w * g_frame_h * 4);
        g_loaded_frame_bytes = 0;
        g_loaded_frame_partial = 1;
    } else {
        size_t bytes_read = fread(frame_buffer, 1, (size_t)g_frame_w * g_frame_h * 4, f);
        fclose(f);
        g_loaded_frame_bytes = bytes_read;
        g_loaded_frame_partial = (bytes_read < (size_t)g_frame_w * g_frame_h * 4);
        if (g_loaded_frame_partial) {
            memset(frame_buffer + bytes_read, 0, ((size_t)g_frame_w * g_frame_h * 4) - bytes_read);
        }
    }
    g_loaded_frame_checksum = checksum_buffer(frame_buffer, (size_t)g_frame_w * g_frame_h * 4);

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_frame_w, g_frame_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_buffer);
    write_gl_display_receipt("texture_upload");
}

static void display(void) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(g_texture_view_x, g_window_h - g_texture_view_y - g_texture_view_h,
               g_texture_view_w, g_texture_view_h);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(0, 0); glVertex2f(-1, 1);
    glEnd();

    glutSwapBuffers();
    write_gl_display_receipt("display_swap");
}

static void reshape(int width, int height) {
    update_texture_viewport(width, height);
    write_gl_display_receipt("reshape");
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glutPostRedisplay();
}

static void timer(int value) {
    struct stat st;
    (void)value;

    if (g_shutdown_requested) {
        palnet_shutdown();
        exit(0);
    }

    if (stat(frame_pulse, &st) == 0) {
        if (st.st_size != last_pulse_size) {
            last_pulse_size = st.st_size;
            load_texture();
            glutPostRedisplay();
        }
    }

    /* PAL-NET-STANDARD.txt sec. 1/3/4 - all of this is non-blocking
     * (bind_with_retry() already set O_NONBLOCK on the listen fd, and
     * palnet_accept_clients()/palnet_reap_clients() only ever use
     * accept()/recv() with MSG_DONTWAIT-equivalent semantics), so it's
     * safe to run every 16ms tick alongside the existing pulse-file
     * check without risking a freeze. glutGet() reads this window's
     * OWN live absolute screen position/size - the same values a
     * dragging pet window needs to know to hit-test against. */
    palnet_accept_clients();
    palnet_reap_clients();
    palnet_maybe_broadcast_geom(glutGet(GLUT_WINDOW_X), glutGet(GLUT_WINDOW_Y),
                                 glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    maybe_heartbeat();

    glutTimerFunc(16, timer, 0);
}

static void log_key_debug(const char *source, int raw, int mapped) {
    FILE *f = fopen(key_debug_log, "a");
    if (f) { fprintf(f, "%s raw=%d mapped=%d\n", source, raw, mapped); fclose(f); }
}

static void keyboard(unsigned char key, int x, int y) {
    (void)x;
    (void)y;
    int raw = (int)key;
    if (key == 3) key = 'q';
    log_key_debug("keyboard", raw, (int)key);
    append_key((int)key);
    if (key == 'q' || key == 'Q') {
        g_shutdown_requested = 1;
    }
}

static void special_keyboard(int key, int x, int y) {
    (void)x;
    (void)y;
    int mapped = map_special_key(key);
    log_key_debug("special", key, mapped);
    if (mapped > 0) append_key(mapped);
}

int main(int argc, char **argv) {
    struct stat st;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    resolve_root();
    palnet_init();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(g_frame_w, g_frame_h);
    glutCreateWindow("zoo_0000 RGB mirror");

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special_keyboard);
    glutTimerFunc(16, timer, 0);
    glutIgnoreKeyRepeat(1);

    if (stat(frame_pulse, &st) == 0) {
        last_pulse_size = st.st_size;
    }

    load_texture();

    glutMainLoop();
    return 0;
}
