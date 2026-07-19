#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

unsigned char* image_data = NULL;
int img_w = 0, img_h = 0, img_channels = 0;
GLuint texture = 0;

int win_w = 1024, win_h = 768;
float scale = 1.0f;

// Button
typedef struct {
    int x, y, w, h;
    const char* label;
    void (*action)();
} Button;

Button buttons[7];

void load_texture();   // Declaration

void open_action();
void save_action();
void reset_action();
void zoom_in_action();
void zoom_out_action();
void flip_h_action();
void flip_v_action();

void init_buttons() {
    int bw = 100, bh = 35, y = 15, x = 15;
    buttons[0] = (Button){x, y, bw, bh, "Open", open_action};      x += 110;
    buttons[1] = (Button){x, y, bw, bh, "Save", save_action};      x += 110;
    buttons[2] = (Button){x, y, bw, bh, "Reset", reset_action};    x += 110;
    buttons[3] = (Button){x, y, bw, bh, "Zoom +", zoom_in_action}; x += 110;
    buttons[4] = (Button){x, y, bw, bh, "Zoom -", zoom_out_action};x += 110;
    buttons[5] = (Button){x, y, bw, bh, "Flip H", flip_h_action};  x += 110;
    buttons[6] = (Button){x, y, bw, bh, "Flip V", flip_v_action};
}

void load_texture() {
    if (!image_data) return;
    if (texture) glDeleteTextures(1, &texture);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum fmt = (img_channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, img_w, img_h, 0, fmt, GL_UNSIGNED_BYTE, image_data);
}

void draw_button(Button b) {
    glColor3f(0.25f, 0.4f, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(b.x, win_h - b.y - b.h);
        glVertex2f(b.x + b.w, win_h - b.y - b.h);
        glVertex2f(b.x + b.w, win_h - b.y);
        glVertex2f(b.x, win_h - b.y);
    glEnd();

    glColor3f(1,1,1);
    glRasterPos2f(b.x + 15, win_h - b.y - 22);
    for (const char* c = b.label; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

void open_action() {
    char path[512] = {0};
    printf("Enter full image path: ");
    scanf("%511s", path);
    if (image_data) stbi_image_free(image_data);
    image_data = stbi_load(path, &img_w, &img_h, &img_channels, 0);
    if (image_data) {
        load_texture();
        scale = 1.0f;
        glutPostRedisplay();
    }
}

void save_action() {
    if (!image_data) return;
    char path[512] = {0};
    printf("Enter save name (e.g. output.ppm): ");
    scanf("%511s", path);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", img_w, img_h);
        for (int i = 0; i < img_w*img_h*img_channels; i += img_channels) {
            fputc(image_data[i], f);
            fputc(image_data[i+1], f);
            fputc(image_data[i+2], f);
        }
        fclose(f);
        printf("Saved!\n");
    }
}

void reset_action() { scale = 1.0f; glutPostRedisplay(); }
void zoom_in_action() { scale *= 1.2f; glutPostRedisplay(); }
void zoom_out_action() { scale /= 1.2f; glutPostRedisplay(); }

void flip_h_action() {
    if (!image_data) return;
    unsigned char* temp = malloc(img_w * img_channels);
    for (int y = 0; y < img_h; y++) {
        unsigned char* row = image_data + y * img_w * img_channels;
        for (int x = 0; x < img_w/2; x++) {
            for (int c = 0; c < img_channels; c++) {
                unsigned char t = row[x * img_channels + c];
                row[x * img_channels + c] = row[(img_w-1-x) * img_channels + c];
                row[(img_w-1-x) * img_channels + c] = t;
            }
        }
    }
    free(temp);
    load_texture();
    glutPostRedisplay();
}

void flip_v_action() {
    if (!image_data) return;
    unsigned char* temp = malloc(img_w * img_channels);
    for (int y = 0; y < img_h/2; y++) {
        unsigned char* top = image_data + y * img_w * img_channels;
        unsigned char* bottom = image_data + (img_h-1-y) * img_w * img_channels;
        memcpy(temp, top, img_w * img_channels);
        memcpy(top, bottom, img_w * img_channels);
        memcpy(bottom, temp, img_w * img_channels);
    }
    free(temp);
    load_texture();
    glutPostRedisplay();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (texture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        float w = img_w * scale;
        float h = img_h * scale;
        float x = (win_w - w) / 2;
        float y = (win_h - h) / 2 - 50;

        glBegin(GL_QUADS);
            glTexCoord2f(0,1); glVertex2f(x, y);
            glTexCoord2f(1,1); glVertex2f(x+w, y);
            glTexCoord2f(1,0); glVertex2f(x+w, y+h);
            glTexCoord2f(0,0); glVertex2f(x, y+h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    for (int i = 0; i < 7; i++) draw_button(buttons[i]);

    glutSwapBuffers();
}

void mouse(int btn, int state, int mx, int my) {
    if (btn == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        for (int i = 0; i < 7; i++) {
            Button b = buttons[i];
            int by_top = win_h - b.y - b.h;
            int by_bottom = win_h - b.y;
            if (mx >= b.x && mx <= b.x + b.w && my >= by_top && my <= by_bottom) {
                b.action();
                return;
            }
        }
    }
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0,w,0,h);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(win_w, win_h);
    glutCreateWindow("GLUT Image Editor - Click Buttons");

    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    init_buttons();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);

    glutMainLoop();
    return 0;
}
