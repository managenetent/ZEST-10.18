/* gl_mirror - the ONLY file in mutaclsym allowed to call GL/GLUT
 * primitives, per 2.muchi-verse/GRAND-ARCHITECTURE.md's GOVERNING
 * CONSTRAINT (prisc+x's long-term RISC-V-compilation goal means our own
 * code must never depend on GL primitives - only this one minimal
 * reader, ported near-unmodified from real 1.TPMOS
 * projects/wraith-alpha/ops/wraith_gl.c, is exempt). Its entire job:
 * poll ops/compose_rgb_frame.+x's output file
 * (pieces/display/rgb_frame.raw, a raw RGBA32 buffer - zero GL calls
 * went into producing those bytes) and blit it as one textured quad.
 *
 * Deliberately stripped down from wraith_gl.c's 799 lines: dropped
 * everything specific to wraith-alpha-desktop's multi-window manager -
 * mouse hit-testing (hit_test_semantic_action), click-to-activate
 * (emit_mouse_event/pending-action), window-drag calibration
 * (#.mouse-offset.txt / apply_mouse_offset), project command history.
 * mutaclsym has no window manager and no semantic-object click targets
 * to hit-test against - this window is a mirror, not a desktop.
 *
 * Keyboard forwarding and the focus-lock file (see below) were
 * INITIALLY dropped too, on the assumption OS window-focus alone would
 * keep the terminal and this GL window from double-writing into the
 * same input stream. Corrected after finding real 1.TPMOS's
 * pieces/joystick/plugins/joystick_input.c - both it AND
 * pieces/keyboard/plugins/keyboard_input.c explicitly check a shared
 * pieces/apps/gl_os/session/input_focus.lock file
 * (gl_os_has_focus()) before writing to the shared history file,
 * rather than trusting window-manager focus routing - a joystick
 * reading a raw /dev/input/js0 device has no concept of "window focus"
 * at all, so the lock file is the actual, load-bearing mechanism, not
 * a redundant belt-and-suspenders check. gl_desktop.c also has a real
 * glutJoystickFunc(joystick, 50) - so the full pattern is: GL owns
 * joystick/keyboard directly via GLUT callbacks while it holds the
 * lock; standalone terminal/joystick-reader processes back off while
 * it's held. This file now does its half of that: writes the lock on
 * startup, removes it via atexit(), and forwards keyboard input the
 * same way wraith_gl.c's own keyboard()/special_keyboard() do (see
 * this file's own append_key()/map_special_key()).
 * system/keyboard_input.c was updated to check the same lock.
 * Joystick itself (a real, separate process in 1.TPMOS, not part of
 * the GL file) is not yet ported here - tracked as later work, real
 * hardware needed to test against.
 *
 * What's KEPT, byte-for-byte in shape:
 *   - checksum_buffer() - identical FNV-1a-64 algorithm to
 *     ops/compose_rgb_frame.c's own copy (see that file's header) and
 *     to wraith_gl.c's, so a checksum computed by any of the three is
 *     directly comparable.
 *   - load_texture() / display() - the actual "mirror" content: read
 *     the raw file, upload via glTexImage2D, draw ONE glBegin(GL_QUADS)
 *     textured quad, glutSwapBuffers(). No other GL drawing anywhere.
 *   - timer() - polls the source file's mtime/size every 16ms and
 *     reloads on change. wraith_gl.c polls a SEPARATE trigger file
 *     (WRAITH_FRAME_TRIGGER) that its rgb daemon touches on every new
 *     frame; mutaclsym has no such pulse-file convention yet, so this
 *     polls rgb_frame.raw's own stat() directly instead (mtime OR size
 *     change) - one file fewer to keep in sync, same detection effect.
 *   - the receipt-writing pattern the user directly asked be ported:
 *     write_gl_display_receipt(), called from the same three sites
 *     wraith_gl.c calls it from (texture_upload, display_swap,
 *     reshape) - so this pipeline's correctness (did a real texture
 *     upload happen, what did it check-sum to, does that match what
 *     compose_rgb_frame.c said it wrote) can be confirmed by reading a
 *     text file, never by looking at the window. */
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

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)

/* Matches ops/compose_rgb_frame.c's FRAME_W/FRAME_H exactly (20x10 map
 * tiles at 16px each, plus one 16px header text row - piececraft-3d-pal's
 * v0 flat top-down render, no footer row yet) - this file doesn't
 * independently pick a texture size, it reads whatever that op
 * documents it writes. This file is otherwise UNCHANGED from
 * mutaclsym/system/gl_mirror.c - proves the "one shared rendering
 * daemon, many projects" bet from GRAND-ARCHITECTURE.md §0: a second,
 * different project's GL mirror needed zero new GL code, only a
 * dimension constant. */
#define WIDTH 320
#define HEIGHT 176

/* Same bare-decimal-keycode-per-line convention system/keyboard_input.c
 * writes to pieces/apps/player_app/history.txt (prisc+x's read_history
 * opcode fseeks/fscanf's this file, so the format is load-bearing, not
 * cosmetic). Forwarding real keys here (not in the earlier stripped-
 * down version of this port) turns the GL window into a second live
 * input source usable simultaneously with the terminal - same
 * ARROW_LEFT/RIGHT/UP/DOWN = 1000-1003 sentinel values
 * keyboard_input.c/move_player.c already use, which happen to be the
 * exact same values wraith_gl.c's own map_special_key() already
 * produces (not a coincidence to preserve - GLUT special-key handling
 * is the same shape everywhere in this project family). */
#define ARROW_LEFT  1000
#define ARROW_RIGHT 1001
#define ARROW_UP    1002
#define ARROW_DOWN  1003

static char project_root[MAX_PATH] = ".";
static char frame_source[PATH_BUF];
static char display_receipt[PATH_BUF];
static char keyboard_history[PATH_BUF];
static char focus_lock[PATH_BUF];

static GLuint texture_id;
static unsigned char *frame_buffer = NULL;
static off_t last_size = -1;
static time_t last_mtime = -1;
static volatile sig_atomic_t g_shutdown_requested = 0;
static int g_window_w = WIDTH;
static int g_window_h = HEIGHT;
static int g_texture_view_x = 0;
static int g_texture_view_y = 0;
static int g_texture_view_w = WIDTH;
static int g_texture_view_h = HEIGHT;
static unsigned long long g_loaded_frame_checksum = 0;
static size_t g_loaded_frame_bytes = 0;
static int g_loaded_frame_partial = 0;

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
    snprintf(frame_source, sizeof(frame_source), "%s/pieces/display/rgb_frame.raw", project_root);
    snprintf(display_receipt, sizeof(display_receipt), "%s/pieces/display/gl_display.receipt.txt", project_root);
    snprintf(keyboard_history, sizeof(keyboard_history), "%s/pieces/apps/player_app/history.txt", project_root);
    snprintf(focus_lock, sizeof(focus_lock), "%s/pieces/system/gl_focus.lock", project_root);
}

/* Same shape as wraith_gl.c's update_focus_lock()/remove_focus_lock() -
 * as long as this file exists, this window is the authoritative input
 * source and any cooperating standalone input reader (system/
 * keyboard_input.c, and a future ported joystick reader) should back
 * off rather than also writing to the shared history file. See this
 * file's header comment for why this matters more than OS window-focus
 * alone (a joystick device has no window-focus concept at all). */
static void update_focus_lock(void) {
    FILE *f = fopen(focus_lock, "w");
    if (!f) return;
    fprintf(f, "owner=gl_mirror\n");
    fprintf(f, "project=piececraft-3d-pal\n");
    fclose(f);
}

static void remove_focus_lock(void) {
    remove(focus_lock);
}

/* Identical append format to system/keyboard_input.c's own append_key()
 * - one bare decimal int per line, appended (never overwritten, never
 * seeked into - prisc+x's read_history opcode owns the read cursor). */
static void append_key(int key) {
    FILE *f = fopen(keyboard_history, "a");
    if (!f) return;
    fprintf(f, "%d\n", key);
    fclose(f);
}

/* Same GLUT_KEY_* -> 1000-1003 mapping as real wraith_gl.c's own
 * map_special_key() (kept, not reinvented - see this file's header). */
static int map_special_key(int key) {
    if (key == GLUT_KEY_LEFT) return ARROW_LEFT;
    if (key == GLUT_KEY_RIGHT) return ARROW_RIGHT;
    if (key == GLUT_KEY_UP) return ARROW_UP;
    if (key == GLUT_KEY_DOWN) return ARROW_DOWN;
    return 0;
}

/* Same FNV-1a-64 algorithm as ops/compose_rgb_frame.c's checksum_buffer()
 * (and real wraith_gl.c's) - see that file's header for why matching
 * checksums across the pipeline is the whole point. */
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
    double texture_aspect = (double)WIDTH / (double)HEIGHT;
    double window_aspect;

    if (win_w <= 0) win_w = WIDTH;
    if (win_h <= 0) win_h = HEIGHT;

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

/* Per direct instruction: mirrors wraith_gl.c's write_gl_display_receipt()
 * closely enough that its own field names still mean the same thing -
 * this is the artifact that lets correctness be confirmed without eyes
 * on the screen. event is one of "texture_upload"/"display_swap"/
 * "reshape", matching the three call sites wraith_gl.c uses. */
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
    if (WIDTH > 0) scale_x = (double)g_texture_view_w / (double)WIDTH;
    if (HEIGHT > 0) scale_y = (double)g_texture_view_h / (double)HEIGHT;

    fprintf(f, "receipt_type=gl_display_upload\n");
    fprintf(f, "generated_by=gl_mirror\n");
    fprintf(f, "generated_at_epoch=%ld\n", (long)now);
    fprintf(f, "generated_at_iso_utc=%s\n", stamp);
    fprintf(f, "event=%s\n", event ? event : "unknown");
    fprintf(f, "source_rgba32=%s\n", frame_source);
    fprintf(f, "texture_width_px=%d\n", WIDTH);
    fprintf(f, "texture_height_px=%d\n", HEIGHT);
    fprintf(f, "expected_rgba_bytes=%d\n", WIDTH * HEIGHT * 4);
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
    FILE *f = fopen(frame_source, "rb");

    if (!frame_buffer) frame_buffer = malloc((size_t)WIDTH * HEIGHT * 4);
    if (!frame_buffer) {
        if (f) fclose(f);
        return;
    }

    if (!f) {
        memset(frame_buffer, 0, (size_t)WIDTH * HEIGHT * 4);
        g_loaded_frame_bytes = 0;
        g_loaded_frame_partial = 1;
    } else {
        size_t bytes_read = fread(frame_buffer, 1, (size_t)WIDTH * HEIGHT * 4, f);
        fclose(f);
        g_loaded_frame_bytes = bytes_read;
        g_loaded_frame_partial = (bytes_read < (size_t)WIDTH * HEIGHT * 4);
        if (g_loaded_frame_partial) {
            memset(frame_buffer + bytes_read, 0, ((size_t)WIDTH * HEIGHT * 4) - bytes_read);
        }
    }
    g_loaded_frame_checksum = checksum_buffer(frame_buffer, (size_t)WIDTH * HEIGHT * 4);

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_buffer);
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

    if (g_shutdown_requested) exit(0);

    if (stat(frame_source, &st) == 0) {
        if (st.st_size != last_size || st.st_mtime != last_mtime) {
            last_size = st.st_size;
            last_mtime = st.st_mtime;
            load_texture();
            glutPostRedisplay();
        }
    }

    glutTimerFunc(16, timer, 0);
}

/* Forwards every printable key verbatim (wasd, digits 2-9 for the
 * pickup/drop/eat/craft/examine/save menu, Enter to commit a choice.c
 * selection) - same "append everything, let the pal script/ops decide
 * what matters" shape as keyboard_input.c's own main loop, not a
 * curated allowlist here that could drift from move_player.c/choice.c's
 * own key handling. Ctrl+C (ETX, byte 3) is translated to 'q' before
 * appending, matching keyboard_input.c's own ETX-to-quit translation -
 * the quit key needs to actually reach history.txt so prisc+x's pal
 * script (x9=113) sees it and halts itself, not just close this
 * window. */
static void keyboard(unsigned char key, int x, int y) {
    (void)x;
    (void)y;
    if (key == 3) key = 'q';
    append_key((int)key);
    if (key == 'q' || key == 'Q') {
        g_shutdown_requested = 1;
    }
}

static void special_keyboard(int key, int x, int y) {
    (void)x;
    (void)y;
    int mapped = map_special_key(key);
    if (mapped > 0) append_key(mapped);
}

int main(int argc, char **argv) {
    struct stat st;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    resolve_root();
    atexit(remove_focus_lock);
    update_focus_lock();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WIDTH, HEIGHT);
    glutCreateWindow("piececraft-3d-pal RGB mirror");

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special_keyboard);
    glutTimerFunc(16, timer, 0);
    /* Same freeglut auto-repeat rationale as wraith_gl.c's own call -
     * irrelevant to this read-only mirror's single 'q' quit key today,
     * kept anyway since it's a one-line, zero-cost precedent to carry
     * forward rather than silently drop during the port. */
    glutIgnoreKeyRepeat(1);

    if (stat(frame_source, &st) == 0) {
        last_size = st.st_size;
        last_mtime = st.st_mtime;
    }

    load_texture();

    glutMainLoop();
    return 0;
}
