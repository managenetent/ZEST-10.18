/* compose_rgb_frame - piececraft-3d-pal's content renderer. Same job,
 * same shape, same GOVERNING CONSTRAINT (zero GL calls) as mutaclsym's
 * own compose_rgb_frame.c. Reads real 1.TPMOS piececraft-wraith's own
 * two file formats as-is (registry.txt: "glyph=id"; <id>.tile.txt:
 * "key=value" per line - rgb_top/rgb_side/extrude/walkable).
 *
 * DEFAULTS TO 3D per direct user instruction ("piececraft should
 * default to 3d") - unlike the real piececraft-wraith reference
 * (ops/src/wraith_project_input.c's set_defaults(): display_mode=0,
 * i.e. 2D), which only reaches 3D via the '8' toggle. camera_state.txt
 * (written by ops/camera_input.c, this project's own port of that same
 * file's apply_key()) defaults display_mode=3d here instead. The '8'
 * toggle still works, now flipping 3D -> 2D instead of the reverse.
 *
 * The 3D renderer is a real, scoped port of wraith_rgb_daemon.c's
 * actual camera pipeline (NOT a raymarch - that file's tile_zmap 3D
 * path is proper perspective-projected extruded boxes with near-plane
 * clipping and a depth buffer, confirmed by reading it end to end):
 *   - world_to_camera_space()/project_world_point_ex() (~line 950-1008
 *     there): yaw-orbit the world point around the map-center pivot,
 *     translate into camera space, pitch-rotate, perspective-divide.
 *   - clip_poly_near() (~1150): Sutherland-Hodgman clip against
 *     NEAR_PLANE, inserting real vertices rather than discarding whole
 *     faces.
 *   - fill_poly_px() (~1054): scanline polygon fill with a real
 *     per-pixel depth test (nearer-wins), ported without this file's
 *     alpha-blending branch - none of piececraft-wraith's current
 *     tiles set alpha<255, so that path has nothing to exercise yet.
 *   - draw_box()/unrotate_by_yaw() (~1352-1461): per-face visibility
 *     test (is the camera on the outward side of each face's plane, in
 *     the box's own unrotated frame) so only actually-visible faces of
 *     each tile get drawn - not a fixed "always these 2 faces" guess.
 * Camera MODEL, however, is NOT ported from the reference - direct
 * user correction: "the wraith 3d pov was meant to show 1rst person
 * 3rd person and free camera but it wasn't working yet, u can fix this
 * local one now, ill fix the reference later." The reference's
 * camera_mode 1/2/3 were three hardcoded (pitch,cam_y,cam_z) presets
 * orbiting a FIXED map-center pivot - camera_mode=2's preset (pitch=45,
 * cam_y=10, cam_z=-12) was confirmed broken here too (projects
 * entirely off-frame - verified via the debug PNG, not assumed), and
 * none of the three actually behaved like first-person/third-person/
 * free-camera regardless. This file implements that real intent
 * instead: g_cam_pivot is set to the CAMERA'S OWN position (not a
 * fixed map-center point) so yaw is a genuine look-direction rotation
 * (turn in place), not an orbit-around-the-map effect - the same
 * world_to_camera_space() math ported from the reference produces
 * exactly this when pivot=camera (verified algebraically: with
 * pivot_x=cam_x, pivot_z=cam_z, the px/pz-relative-to-pivot terms
 * collapse to "point relative to camera, then rotated," which is
 * standard look-direction rotation). ops/camera_input.c drives a
 * single yaw-relative rig position (forward/strafe on W/A/S/D, turn on
 * Q/E, vertical on Z/X, look up/down on R/F); camera_mode only changes
 * how the actual camera is placed RELATIVE to that rig:
 *   1 (first-person): camera IS the rig, at eye height.
 *   2 (third-person): camera is pulled back behind the rig along
 *     -forward and raised, with a downward pitch bias, so it looks
 *     toward the rig - classic chase-cam.
 *   3 (free-camera, the default view): camera IS the rig too, but
 *     starts pulled back further with no subject-tracking bias - full
 *     unconstrained fly-around.
 *
 * Ground grid (draw_tile_zmap_preview_3d()'s own wireframe, ~1910+ in
 * the reference) IS ported as-is - cheap, and the fastest way to
 * visually confirm camera orientation is actually working.
 *
 * Scope deliberately NOT ported: voxel_source sub-voxel grids (no tile
 * here uses one), alpha/translucency pass, mouse-driven look (keyboard-
 * only for now, matching mutaclsym's own gl_mirror input path - R/F
 * cover pitch, Q/E cover yaw, so look control exists without a mouse). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)
#define MAP_W 20
#define MAP_H 10
#define TILE_PX 16
#define GLYPH_W 8
#define GLYPH_H 16
#define HEADER_ROWS 1
#define FRAME_W (MAP_W * TILE_PX)
#define FRAME_H (HEADER_ROWS * GLYPH_H + MAP_H * TILE_PX)
#define MAX_TEXT_COLS (FRAME_W / GLYPH_W)
#define MAX_TILE_TYPES 32
#define NEAR_PLANE 0.5

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

typedef struct {
    char glyph;
    char id[64];
    int rgb_top[3];
    int rgb_side[3];
    double extrude;
    int walkable;
    int found;
} TileType;

static TileType tile_types[MAX_TILE_TYPES];
static int tile_type_count = 0;

/* Loads pieces/registry/tiles/registry.txt ("glyph=id" rows) then, for
 * each id, pieces/registry/tiles/<id>.tile.txt's key=value fields -
 * two real 1.TPMOS file formats, read as-is. */
static void load_tile_types(void) {
    char reg_path[PATH_BUF];
    snprintf(reg_path, sizeof(reg_path), "%s/pieces/registry/tiles/registry.txt", project_root);
    FILE *f = fopen(reg_path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (tile_type_count < MAX_TILE_TYPES && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        /* '#' is both the wall glyph and this file's own comment marker -
         * only a real comment if NOT immediately followed by '=' (the
         * "#=wall" data-row shape). Same fix already applied to
         * mutaclsym's registries AND confirmed as the real, canonical
         * fix by reading wraith_rgb_daemon.c's own load_tile_meta() -
         * it has the identical guard, independently arrived at here
         * first via the debug-PNG bug hunt (see GRAND-ARCHITECTURE.md
         * §0c), then confirmed correct against the source. */
        if (line[0] == '\0' || (line[0] == '#' && line[1] != '=')) continue;
        char *eq = strchr(line, '=');
        if (!eq || eq != line + 1) continue;
        TileType *t = &tile_types[tile_type_count];
        t->glyph = line[0];
        t->extrude = 1.0;
        t->walkable = 0;
        t->rgb_top[0] = 180; t->rgb_top[1] = 180; t->rgb_top[2] = 180;
        t->rgb_side[0] = 120; t->rgb_side[1] = 120; t->rgb_side[2] = 120;
        t->found = 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(t->id, sizeof(t->id), "%s", eq + 1);
#pragma GCC diagnostic pop

        char tile_path[PATH_BUF];
        snprintf(tile_path, sizeof(tile_path), "%s/pieces/registry/tiles/%s.tile.txt", project_root, t->id);
        FILE *tf = fopen(tile_path, "r");
        if (tf) {
            char tline[MAX_LINE];
            while (fgets(tline, sizeof(tline), tf)) {
                tline[strcspn(tline, "\r\n")] = '\0';
                char *teq = strchr(tline, '=');
                if (!teq) continue;
                *teq = '\0';
                if (strcmp(tline, "rgb_top") == 0) {
                    sscanf(teq + 1, "%d,%d,%d", &t->rgb_top[0], &t->rgb_top[1], &t->rgb_top[2]);
                } else if (strcmp(tline, "rgb_side") == 0) {
                    sscanf(teq + 1, "%d,%d,%d", &t->rgb_side[0], &t->rgb_side[1], &t->rgb_side[2]);
                } else if (strcmp(tline, "extrude") == 0) {
                    t->extrude = atof(teq + 1);
                } else if (strcmp(tline, "walkable") == 0) {
                    t->walkable = atoi(teq + 1);
                }
            }
            fclose(tf);
        }
        tile_type_count++;
    }
    fclose(f);
}

static TileType *find_tile_type(char glyph) {
    for (int i = 0; i < tile_type_count; i++) {
        if (tile_types[i].glyph == glyph) return &tile_types[i];
    }
    return NULL;
}

static void glyph_to_rgb(char glyph, int *r, int *g, int *b) {
    TileType *t = find_tile_type(glyph);
    if (t) { *r = t->rgb_top[0]; *g = t->rgb_top[1]; *b = t->rgb_top[2]; return; }
    if (glyph == ' ' || glyph == '\0') { *r = 0; *g = 0; *b = 0; return; }
    *r = 255; *g = 0; *b = 255;
}

static unsigned char glyphs[127][GLYPH_H][GLYPH_W];

static void load_glyphs(void) {
    memset(glyphs, 0, sizeof(glyphs));
    for (int c = 32; c < 127; c++) {
        char path[PATH_BUF];
        snprintf(path, sizeof(path), "%s/pieces/registry/fonts/ascii/%d/glyph.txt", project_root, c);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[64];
        int y = 0;
        while (y < GLYPH_H && fgets(line, sizeof(line), f)) {
            for (int x = 0; x < GLYPH_W && line[x] != '\0' && line[x] != '\n'; x++) {
                glyphs[c][y][x] = (line[x] == '#') ? 1 : 0;
            }
            y++;
        }
        fclose(f);
    }
}

static void blit_char(unsigned char fb[FRAME_H][FRAME_W][4], int px, int py, unsigned char c,
                       unsigned char r, unsigned char g, unsigned char b) {
    if (c < 32 || c > 126) return;
    for (int y = 0; y < GLYPH_H; y++) {
        int fy = py + y;
        if (fy < 0 || fy >= FRAME_H) continue;
        for (int x = 0; x < GLYPH_W; x++) {
            int fx = px + x;
            if (fx < 0 || fx >= FRAME_W) continue;
            if (!glyphs[c][y][x]) continue;
            fb[fy][fx][0] = r; fb[fy][fx][1] = g; fb[fy][fx][2] = b; fb[fy][fx][3] = 255;
        }
    }
}

static void blit_text(unsigned char fb[FRAME_H][FRAME_W][4], int px, int py, const char *text,
                       unsigned char r, unsigned char g, unsigned char b) {
    int col = 0;
    for (const char *p = text; *p && col < MAX_TEXT_COLS; p++, col++) {
        blit_char(fb, px + col * GLYPH_W, py, (unsigned char)*p, r, g, b);
    }
}

/* ---- camera state (written by ops/camera_input.c) ---- */
typedef struct {
    int display_mode_3d; /* 1=3d (this project's default), 0=2d */
    int camera_mode;     /* 1=first-person 2=third-person 3=free-camera -
                             a real implementation of what the reference
                             project's camera_mode was INTENDED to mean
                             but never finished (direct user confirmation,
                             2026-07-16) - see ops/camera_input.c's header. */
    double rig_x, rig_y, rig_z; /* a genuine world position, driven
                                    yaw-relative by camera_input.c - not
                                    a pan-from-a-fixed-preset offset. */
    double yaw_deg, pitch_delta;
} CameraState;

static void load_camera_state(CameraState *cs) {
    cs->display_mode_3d = 1; /* THE default flip vs. the real reference */
    cs->camera_mode = 3;
    cs->rig_x = cs->rig_y = cs->rig_z = 0.0;
    cs->yaw_deg = cs->pitch_delta = 0.0;

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/system/camera_state.txt", project_root);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *val = eq + 1;
        if (strcmp(line, "display_mode") == 0) cs->display_mode_3d = (strcmp(val, "3d") == 0);
        else if (strcmp(line, "camera_mode") == 0) cs->camera_mode = atoi(val);
        else if (strcmp(line, "rig_x") == 0) cs->rig_x = atof(val);
        else if (strcmp(line, "rig_y") == 0) cs->rig_y = atof(val);
        else if (strcmp(line, "rig_z") == 0) cs->rig_z = atof(val);
        else if (strcmp(line, "yaw_deg") == 0) cs->yaw_deg = atof(val);
        else if (strcmp(line, "pitch_delta") == 0) cs->pitch_delta = atof(val);
    }
    fclose(f);
}

/* ---- 3D projection pipeline - ported from wraith_rgb_daemon.c ---- */
static double g_cam_yaw = 0.0;
static double g_cam_pivot_x = 0.0, g_cam_pivot_z = 0.0;
static double g_depth_buf[FRAME_H][FRAME_W];

static void world_to_camera_space(double wx, double wy, double wz,
                                   double cam_x, double cam_y, double cam_z,
                                   double pitch_deg,
                                   double *out_x, double *out_y, double *out_z) {
    double px = wx - g_cam_pivot_x;
    double pz = wz - g_cam_pivot_z;
    double cyaw = cos(g_cam_yaw), syaw = sin(g_cam_yaw);
    double wxr = g_cam_pivot_x + (px * cyaw - pz * syaw);
    double wzr = g_cam_pivot_z + (px * syaw + pz * cyaw);
    double rx = wxr - cam_x;
    double ry = wy - cam_y;
    double rz = wzr - cam_z;
    double ax = pitch_deg * M_PI / 180.0;
    double cpitch = cos(ax), spitch = sin(ax);
    *out_x = rx;
    *out_y = ry * cpitch - rz * spitch;
    *out_z = ry * spitch + rz * cpitch;
}

static void project_world_point_ex(double wx, double wy, double wz,
                                    double cam_x, double cam_y, double cam_z,
                                    double pitch_deg, double focal,
                                    int screen_cx, int screen_cy, double scale,
                                    int *out_x, int *out_y, double *out_z2) {
    double rx, y2, z2, persp;
    world_to_camera_space(wx, wy, wz, cam_x, cam_y, cam_z, pitch_deg, &rx, &y2, &z2);
    if (z2 <= NEAR_PLANE) z2 = NEAR_PLANE;
    persp = focal / z2;
    *out_x = screen_cx + (int)(rx * scale * persp);
    *out_y = screen_cy - (int)(y2 * scale * persp);
    if (out_z2) *out_z2 = z2;
}

static void unrotate_by_yaw(double x, double z, double *out_x, double *out_z) {
    double px = x - g_cam_pivot_x;
    double pz = z - g_cam_pivot_z;
    double cyaw = cos(-g_cam_yaw), syaw = sin(-g_cam_yaw);
    *out_x = g_cam_pivot_x + (px * cyaw - pz * syaw);
    *out_z = g_cam_pivot_z + (px * syaw + pz * cyaw);
}

typedef struct { double x, y, z; } ClipVert;

static int clip_poly_near(const ClipVert *in, int n_in, ClipVert *out) {
    int n = 0;
    for (int i = 0; i < n_in; i++) {
        const ClipVert *cur = &in[i];
        const ClipVert *nxt = &in[(i + 1) % n_in];
        int cur_in = (cur->z >= NEAR_PLANE);
        int nxt_in = (nxt->z >= NEAR_PLANE);
        if (cur_in) out[n++] = *cur;
        if (cur_in != nxt_in) {
            double t = (NEAR_PLANE - cur->z) / (nxt->z - cur->z);
            out[n].x = cur->x + t * (nxt->x - cur->x);
            out[n].y = cur->y + t * (nxt->y - cur->y);
            out[n].z = NEAR_PLANE;
            n++;
        }
    }
    return n;
}

/* Scanline fill with a real per-pixel depth test (nearer z wins) -
 * ported from fill_poly_px() without the alpha-blend branch (nothing
 * here uses alpha<255 yet). */
static void fill_poly_px(unsigned char fb[FRAME_H][FRAME_W][4], const int *px, const int *py,
                          const double *pz, int n, const int rgb[3]) {
    if (n < 3) return;
    int miny = py[0], maxy = py[0];
    for (int i = 1; i < n; i++) {
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    if (miny < 0) miny = 0;
    if (maxy > FRAME_H - 1) maxy = FRAME_H - 1;
    for (int y = miny; y <= maxy; y++) {
        double xs[16], zs[16];
        int cross = 0;
        for (int i = 0; i < n; i++) {
            int ax_ = px[i], ay_ = py[i];
            int bx_ = px[(i + 1) % n], by_ = py[(i + 1) % n];
            if ((ay_ <= y && by_ > y) || (by_ <= y && ay_ > y)) {
                double t = (double)(y - ay_) / (double)(by_ - ay_);
                if (cross < 16) {
                    xs[cross] = ax_ + t * (bx_ - ax_);
                    zs[cross] = pz[i] + t * (pz[(i + 1) % n] - pz[i]);
                    cross++;
                }
            }
        }
        if (cross < 2) continue;
        for (int a2 = 1; a2 < cross; a2++) {
            double kx = xs[a2], kz = zs[a2];
            int b2 = a2 - 1;
            while (b2 >= 0 && xs[b2] > kx) { xs[b2 + 1] = xs[b2]; zs[b2 + 1] = zs[b2]; b2--; }
            xs[b2 + 1] = kx; zs[b2 + 1] = kz;
        }
        for (int pair = 0; pair + 1 < cross; pair += 2) {
            int sx0 = (int)xs[pair], sx1 = (int)xs[pair + 1] + 1;
            double sz0 = zs[pair], sz1 = zs[pair + 1];
            if (sx0 < 0) sx0 = 0;
            if (sx1 > FRAME_W) sx1 = FRAME_W;
            for (int x = sx0; x < sx1; x++) {
                double t = (sx1 > sx0) ? (double)(x - sx0) / (double)(sx1 - sx0) : 0.0;
                double z = sz0 + t * (sz1 - sz0);
                if (z >= g_depth_buf[y][x]) continue;
                g_depth_buf[y][x] = z;
                fb[y][x][0] = (unsigned char)rgb[0];
                fb[y][x][1] = (unsigned char)rgb[1];
                fb[y][x][2] = (unsigned char)rgb[2];
                fb[y][x][3] = 255;
            }
        }
    }
}

static void draw_clipped_face(unsigned char fb[FRAME_H][FRAME_W][4],
                               double p0x, double p0y, double p0z, double p1x, double p1y, double p1z,
                               double p2x, double p2y, double p2z, double p3x, double p3y, double p3z,
                               double cam_x, double cam_y, double cam_z, double pitch, double focal,
                               int screen_cx, int screen_cy, double scale, const int rgb[3]) {
    ClipVert cs[4], clipped[5];
    int screen_x[5], screen_y[5];
    double screen_z[5];
    world_to_camera_space(p0x, p0y, p0z, cam_x, cam_y, cam_z, pitch, &cs[0].x, &cs[0].y, &cs[0].z);
    world_to_camera_space(p1x, p1y, p1z, cam_x, cam_y, cam_z, pitch, &cs[1].x, &cs[1].y, &cs[1].z);
    world_to_camera_space(p2x, p2y, p2z, cam_x, cam_y, cam_z, pitch, &cs[2].x, &cs[2].y, &cs[2].z);
    world_to_camera_space(p3x, p3y, p3z, cam_x, cam_y, cam_z, pitch, &cs[3].x, &cs[3].y, &cs[3].z);
    int n = clip_poly_near(cs, 4, clipped);
    if (n < 3) return;
    for (int i = 0; i < n; i++) {
        double persp = focal / clipped[i].z;
        screen_x[i] = screen_cx + (int)(clipped[i].x * scale * persp);
        screen_y[i] = screen_cy - (int)(clipped[i].y * scale * persp);
        screen_z[i] = clipped[i].z;
    }
    fill_poly_px(fb, screen_x, screen_y, screen_z, n, rgb);
}

/* Draws whichever of a box's 5 above-ground faces (top + up to 4 sides;
 * no bottom face needed here - the camera never goes below wy=0 in any
 * POV preset) are actually front-facing, tested per-face against the
 * camera's position in the box's own unrotated frame - ported from
 * draw_box(). */
static void draw_box(unsigned char fb[FRAME_H][FRAME_W][4],
                      double wx0, double wx1, double wz0, double wz1, double wy_top,
                      double cam_x, double cam_y, double cam_z, double pitch, double focal,
                      int screen_cx, int screen_cy, double scale,
                      const int rgb_top[3], const int rgb_side[3]) {
    if (wy_top > 0.02) {
        double ecam_x, ecam_z;
        unrotate_by_yaw(cam_x, cam_z, &ecam_x, &ecam_z);
        int wx0_vis = (ecam_x < wx0), wx1_vis = (ecam_x > wx1);
        int wz0_vis = (ecam_z < wz0), wz1_vis = (ecam_z > wz1);
        if (wz0_vis) draw_clipped_face(fb, wx0,wy_top,wz0, wx1,wy_top,wz0, wx1,0,wz0, wx0,0,wz0,
                                        cam_x,cam_y,cam_z,pitch,focal,screen_cx,screen_cy,scale,rgb_side);
        if (wz1_vis) draw_clipped_face(fb, wx0,wy_top,wz1, wx1,wy_top,wz1, wx1,0,wz1, wx0,0,wz1,
                                        cam_x,cam_y,cam_z,pitch,focal,screen_cx,screen_cy,scale,rgb_side);
        if (wx0_vis) draw_clipped_face(fb, wx0,wy_top,wz0, wx0,wy_top,wz1, wx0,0,wz1, wx0,0,wz0,
                                        cam_x,cam_y,cam_z,pitch,focal,screen_cx,screen_cy,scale,rgb_side);
        if (wx1_vis) draw_clipped_face(fb, wx1,wy_top,wz0, wx1,wy_top,wz1, wx1,0,wz1, wx1,0,wz0,
                                        cam_x,cam_y,cam_z,pitch,focal,screen_cx,screen_cy,scale,rgb_side);
    }
    if (cam_y > wy_top) {
        draw_clipped_face(fb, wx0,wy_top,wz0, wx1,wy_top,wz0, wx1,wy_top,wz1, wx0,wy_top,wz1,
                           cam_x,cam_y,cam_z,pitch,focal,screen_cx,screen_cy,scale,rgb_top);
    }
}

static void draw_line_px(unsigned char fb[FRAME_H][FRAME_W][4], int x0, int y0, int x1, int y1, const int rgb[3]) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < FRAME_W && y0 >= 0 && y0 < FRAME_H) {
            fb[y0][x0][0] = (unsigned char)rgb[0];
            fb[y0][x0][1] = (unsigned char)rgb[1];
            fb[y0][x0][2] = (unsigned char)rgb[2];
            fb[y0][x0][3] = 255;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Renders the whole fixed map_01 grid in real 3D. camera_mode places
 * the actual camera relative to ops/camera_input.c's yaw-relative rig
 * position - see this file's header for why pivot=camera (not a fixed
 * map-center point) and the per-mode placement rationale. rig_x=0 is
 * the map's own horizontal center (world x is already centered via
 * half_w below); MAP_H/2.0 is the map's depth center - both used as
 * the rig's starting-position baseline before any WASD movement. */
static void render_3d(unsigned char fb[FRAME_H][FRAME_W][4], char grid[MAP_H][MAP_W + 1], int rows,
                       const CameraState *cs) {
    const double EYE_HEIGHT = 1.6;
    const double THIRD_PERSON_BACK = 6.0;
    const double THIRD_PERSON_UP = 3.0;
    const double FREE_CAM_BACK = 8.0;
    const double FREE_CAM_UP = 2.5;
    double focal = 1.0;
    int screen_cx = FRAME_W / 2, screen_cy = (int)(FRAME_H * 0.42);
    double scale = 105.0; /* half of piececraft-wraith's 210.0 - that constant
                              was tuned for a ~48x14-cell (~384x224px) widget;
                              this frame is 320x176, scaled proportionally
                              rather than reused verbatim, then adjusted
                              against the actual debug PNG output. */

    double yaw_rad = cs->yaw_deg * M_PI / 180.0;
    double forward_x = sin(yaw_rad), forward_z = cos(yaw_rad);
    double rig_x = cs->rig_x;
    double rig_z = MAP_H / 2.0 + cs->rig_z;
    double rig_y_base = cs->rig_y;

    double cam_x, cam_y, cam_z, pitch;
    switch (cs->camera_mode) {
        case 2: /* third-person: pulled back + up, biased to look at the rig */
            cam_x = rig_x - forward_x * THIRD_PERSON_BACK;
            cam_z = rig_z - forward_z * THIRD_PERSON_BACK;
            cam_y = EYE_HEIGHT + rig_y_base + THIRD_PERSON_UP;
            pitch = 15.0 + cs->pitch_delta;
            break;
        case 1: /* first-person: camera IS the rig, at eye height */
            cam_x = rig_x;
            cam_z = rig_z;
            cam_y = EYE_HEIGHT + rig_y_base;
            pitch = cs->pitch_delta;
            break;
        default: /* 3: free-camera - the rig itself, starting pulled back
                     for a wide default vantage, full free look */
            cam_x = rig_x;
            cam_z = rig_z - FREE_CAM_BACK;
            cam_y = FREE_CAM_UP + rig_y_base;
            pitch = 10.0 + cs->pitch_delta;
            break;
    }

    /* Pivot = camera's own position -> yaw rotates world points around
     * the CAMERA (a real look-direction turn), not around a fixed map
     * point. See this file's header for the algebra. */
    g_cam_yaw = yaw_rad;
    g_cam_pivot_x = cam_x;
    g_cam_pivot_z = cam_z;

    for (int y = 0; y < FRAME_H; y++)
        for (int x = 0; x < FRAME_W; x++)
            g_depth_buf[y][x] = 1e9;

    const int grid_rgb[3] = {90, 90, 90};
    double half_w = MAP_W / 2.0;
    for (int gline = 0; gline <= MAP_W; gline++) {
        int ax, ay, bx, by;
        double az, bz;
        project_world_point_ex(gline - half_w, 0.0, 0.0, cam_x, cam_y, cam_z, pitch, focal, screen_cx, screen_cy, scale, &ax, &ay, &az);
        project_world_point_ex(gline - half_w, 0.0, (double)MAP_H, cam_x, cam_y, cam_z, pitch, focal, screen_cx, screen_cy, scale, &bx, &by, &bz);
        draw_line_px(fb, ax, ay, bx, by, grid_rgb);
    }
    for (int gline = 0; gline <= MAP_H; gline++) {
        int ax, ay, bx, by;
        double az, bz;
        project_world_point_ex(-half_w, 0.0, (double)gline, cam_x, cam_y, cam_z, pitch, focal, screen_cx, screen_cy, scale, &ax, &ay, &az);
        project_world_point_ex(half_w, 0.0, (double)gline, cam_x, cam_y, cam_z, pitch, focal, screen_cx, screen_cy, scale, &bx, &by, &bz);
        draw_line_px(fb, ax, ay, bx, by, grid_rgb);
    }

    for (int r = 0; r < MAP_H; r++) {
        int len = (r < rows) ? (int)strlen(grid[r]) : 0;
        for (int c = 0; c < MAP_W; c++) {
            char glyph = (c < len) ? grid[r][c] : ' ';
            TileType *t = find_tile_type(glyph);
            if (!t || t->walkable) continue;
            double wx0 = c - half_w, wx1 = wx0 + 1.0;
            double wz0 = (double)r, wz1 = wz0 + 1.0;
            draw_box(fb, wx0, wx1, wz0, wz1, t->extrude,
                     cam_x, cam_y, cam_z, pitch, focal, screen_cx, screen_cy, scale,
                     t->rgb_top, t->rgb_side);
        }
    }
}

static void render_topdown(unsigned char fb[FRAME_H][FRAME_W][4], char grid[MAP_H][MAP_W + 1], int rows, int tile_y0) {
    for (int r = 0; r < MAP_H; r++) {
        int len = (r < rows) ? (int)strlen(grid[r]) : 0;
        for (int c = 0; c < MAP_W; c++) {
            char glyph = (c < len) ? grid[r][c] : ' ';
            int rr, gg, bb;
            glyph_to_rgb(glyph, &rr, &gg, &bb);
            for (int py = 0; py < TILE_PX; py++) {
                int fy = tile_y0 + r * TILE_PX + py;
                for (int px = 0; px < TILE_PX; px++) {
                    int fx = c * TILE_PX + px;
                    fb[fy][fx][0] = (unsigned char)rr;
                    fb[fy][fx][1] = (unsigned char)gg;
                    fb[fy][fx][2] = (unsigned char)bb;
                }
            }
        }
    }
}

static uint64_t checksum_buffer(const unsigned char *buf, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) { hash ^= buf[i]; hash *= 1099511628211ULL; }
    return hash;
}

static void write_receipt(const char *path, size_t byte_count, uint64_t checksum, const CameraState *cs) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "op=compose_rgb_frame\n");
    fprintf(f, "project=piececraft-3d-pal\n");
    fprintf(f, "frame_w=%d\n", FRAME_W);
    fprintf(f, "frame_h=%d\n", FRAME_H);
    fprintf(f, "tile_px=%d\n", TILE_PX);
    fprintf(f, "map_w=%d\n", MAP_W);
    fprintf(f, "map_h=%d\n", MAP_H);
    fprintf(f, "bytes_per_pixel=4\n");
    fprintf(f, "byte_count=%zu\n", byte_count);
    fprintf(f, "expected_byte_count=%d\n", FRAME_W * FRAME_H * 4);
    fprintf(f, "checksum_fnv1a64=%016llx\n", (unsigned long long)checksum);
    fprintf(f, "render_mode=%s\n", cs->display_mode_3d ? "3d_box_v1" : "flat_topdown_v0");
    fprintf(f, "camera_mode=%d\n", cs->camera_mode);
    fprintf(f, "rig_x=%.2f\nrig_y=%.2f\nrig_z=%.2f\n", cs->rig_x, cs->rig_y, cs->rig_z);
    fprintf(f, "yaw_deg=%.2f\npitch_delta=%.2f\n", cs->yaw_deg, cs->pitch_delta);
    fprintf(f, "written_at=%ld\n", (long)now);
    fclose(f);
}

int main(void) {
    resolve_root();
    load_tile_types();
    load_glyphs();
    CameraState cs;
    load_camera_state(&cs);

    char map_path[PATH_BUF], out_path[PATH_BUF], receipt_path[PATH_BUF];
    snprintf(map_path, sizeof(map_path), "%s/pieces/world_01/map_01/map.txt", project_root);
    snprintf(out_path, sizeof(out_path), "%s/pieces/display/rgb_frame.raw", project_root);
    snprintf(receipt_path, sizeof(receipt_path), "%s/pieces/display/rgb_frame.receipt.txt", project_root);

    char grid[MAP_H][MAP_W + 1];
    FILE *mf = fopen(map_path, "r");
    int rows = 0;
    if (mf) {
        char line[MAP_W + 4];
        while (rows < MAP_H && fgets(line, sizeof(line), mf)) {
            line[strcspn(line, "\r\n")] = '\0';
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(grid[rows], sizeof(grid[0]), "%s", line);
#pragma GCC diagnostic pop
            rows++;
        }
        fclose(mf);
    }

    static unsigned char framebuf[FRAME_H][FRAME_W][4];
    for (int fy = 0; fy < FRAME_H; fy++)
        for (int fx = 0; fx < FRAME_W; fx++) {
            framebuf[fy][fx][3] = 255;
        }

    int tile_y0 = HEADER_ROWS * GLYPH_H;
    if (cs.display_mode_3d) {
        render_3d(framebuf, grid, rows, &cs);
    } else {
        render_topdown(framebuf, grid, rows, tile_y0);
    }

    char header[64];
    snprintf(header, sizeof(header), "PIECECRAFT-3D map_01 (%s v%d)",
             cs.display_mode_3d ? "3d" : "2d", cs.camera_mode);
    blit_text(framebuf, 0, 0, header, 255, 255, 255);

    size_t byte_count = (size_t)FRAME_W * FRAME_H * 4;
    FILE *out = fopen(out_path, "wb");
    if (!out) return 1;
    size_t written = fwrite(framebuf, 1, byte_count, out);
    fclose(out);
    if (written != byte_count) return 1;

    uint64_t checksum = checksum_buffer((const unsigned char *)framebuf, byte_count);
    write_receipt(receipt_path, byte_count, checksum, &cs);
    return 0;
}
