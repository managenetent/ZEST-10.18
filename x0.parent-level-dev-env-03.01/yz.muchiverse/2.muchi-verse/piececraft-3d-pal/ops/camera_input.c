/* camera_input - one verb, one binary, no shared headers. Applies one
 * keycode to pieces/system/camera_state.txt.
 *
 * Real 1.TPMOS piececraft-3d's own manager (piececraft-3d_manager.c)
 * and piececraft-wraith's ops/src/wraith_project_input.c both intended
 * camera_mode 1/2/3 to mean first-person/third-person/free-camera -
 * confirmed directly by the user ("the wraith 3d pov was meant to show
 * 1rst person 3rd person and free camera but it wasn't working yet, u
 * can fix this local one now, ill fix the reference later"). This file
 * is a genuine implementation of that intent, not a port of
 * (unfinished) reference code - see ops/compose_rgb_frame.c's header
 * for the camera-model writeup.
 *
 * Key mapping (a real departure from wraith_project_input.c's WASD-
 * pans-fixed-world-axes + mouse-drag-orbits scheme, needed because a
 * fixed-axis pan can't express "walk forward" once yaw turning exists -
 * "forward" has to mean "wherever you're currently facing"):
 *   W/S   - move forward/backward along the CURRENT yaw direction
 *   A/D   - strafe left/right (perpendicular to yaw)
 *   Z/X   - move down/up (vertical, yaw-independent)
 *   Q/E   - turn (yaw) left/right, 15 degrees per press
 *   R/F   - look up/down (pitch), 5 degrees per press
 *   1/2/3 - first-person / third-person / free-camera
 *   8     - toggle 2D/3D
 * rig_x/rig_y/rig_z is genuinely a world position (not a pan-from-a-
 * preset offset like the old scheme) - all three camera modes read the
 * SAME rig + yaw + pitch_delta and differ only in how they place the
 * actual camera relative to it (see compose_rgb_frame.c).
 *
 * Usage: camera_input.+x <keycode> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_LINE 512
#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)

static char project_root[MAX_PATH] = ".";

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

typedef struct {
    int display_mode_3d;
    int camera_mode;      /* 1=first-person 2=third-person 3=free-camera */
    double rig_x, rig_y, rig_z;
    double yaw_deg, pitch_delta;
} CameraState;

static void load_state(CameraState *cs, const char *path) {
    cs->display_mode_3d = 1; /* this project's default, unlike the reference */
    cs->camera_mode = 3;     /* free-camera - the most orientation-friendly
                                 first view, since first/third-person start
                                 planted in the middle of the map geometry */
    cs->rig_x = cs->rig_y = cs->rig_z = 0.0;
    cs->yaw_deg = cs->pitch_delta = 0.0;

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

static void save_state(const CameraState *cs, const char *path) {
    char tmp[PATH_BUF + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "display_mode=%s\n", cs->display_mode_3d ? "3d" : "2d");
    fprintf(f, "camera_mode=%d\n", cs->camera_mode);
    fprintf(f, "rig_x=%.3f\n", cs->rig_x);
    fprintf(f, "rig_y=%.3f\n", cs->rig_y);
    fprintf(f, "rig_z=%.3f\n", cs->rig_z);
    fprintf(f, "yaw_deg=%.2f\n", cs->yaw_deg);
    fprintf(f, "pitch_delta=%.2f\n", cs->pitch_delta);
    fclose(f);
    rename(tmp, path);
}

static void apply_key(CameraState *cs, int key) {
    const double move_step = 1.0;
    const double yaw_step = 15.0;
    const double pitch_step = 5.0;
    double yaw_rad = cs->yaw_deg * M_PI / 180.0;
    /* forward/right are expressed in the SAME world axes
     * world_to_camera_space() rotates by +yaw around the camera - see
     * compose_rgb_frame.c's header for why pivot=camera (not a fixed
     * map-center point) is what makes "yaw" mean "which way I'm
     * facing" instead of "orbit around the map." */
    double forward_x = sin(yaw_rad), forward_z = cos(yaw_rad);
    double right_x = cos(yaw_rad), right_z = -sin(yaw_rad);

    switch (key) {
        case 'w': case 'W': cs->rig_x += forward_x * move_step; cs->rig_z += forward_z * move_step; break;
        case 's': case 'S': cs->rig_x -= forward_x * move_step; cs->rig_z -= forward_z * move_step; break;
        case 'd': case 'D': cs->rig_x += right_x * move_step; cs->rig_z += right_z * move_step; break;
        case 'a': case 'A': cs->rig_x -= right_x * move_step; cs->rig_z -= right_z * move_step; break;
        case 'x': case 'X': cs->rig_y += move_step; break;
        case 'z': case 'Z': cs->rig_y -= move_step; break;
        case 'e': case 'E': cs->yaw_deg += yaw_step; break;
        case 'q': case 'Q': cs->yaw_deg -= yaw_step; break;
        case 'r': case 'R': cs->pitch_delta -= pitch_step; break; /* look up */
        case 'f': case 'F': cs->pitch_delta += pitch_step; break; /* look down */
        case '1': cs->camera_mode = 1; break;
        case '2': cs->camera_mode = 2; break;
        case '3': cs->camera_mode = 3; break;
        case '8': cs->display_mode_3d = !cs->display_mode_3d; break;
        default: break;
    }
    if (cs->yaw_deg > 360.0) cs->yaw_deg -= 360.0;
    if (cs->yaw_deg < -360.0) cs->yaw_deg += 360.0;
    if (cs->pitch_delta > 80.0) cs->pitch_delta = 80.0;
    if (cs->pitch_delta < -80.0) cs->pitch_delta = -80.0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int key = atoi(argv[1]);
    resolve_root();

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/pieces/system/camera_state.txt", project_root);

    CameraState cs;
    load_state(&cs, path);
    apply_key(&cs, key);
    save_state(&cs, path);
    return 0;
}
