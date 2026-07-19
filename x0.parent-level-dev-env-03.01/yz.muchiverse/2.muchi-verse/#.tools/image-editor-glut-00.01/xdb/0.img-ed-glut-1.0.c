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

// Button structure
typedef struct {
    int x, y, w, h;
    const char* text;
    void (*action)();
} Button;

Button buttons[5];

void open_image_action();
void save_image_action();
void reset_zoom();
void zoom_in();
void zoom_out();

void init_buttons() {
    int btn_w = 90;
    int btn_h = 30;
    int start_x = 10;
    int y = 10;

    buttons[0] = (Button){start_x, y, btn_w, btn_h, "Open", open_image_action};
    buttons[1] = (Button){start_x + 100, y, btn_w, btn_h, "Save", save_image_action};
    buttons[2] = (Button){start_x + 200, y, btn_w, btn_h, "Reset", reset_zoom};
    buttons[3] = (Button){start_x + 300, y, btn_w, btn_h, "Zoom +", zoom_in};
    buttons[4] = (Button){start_x + 400, y, btn_w, btn_h, "Zoom -", zoom_out};
}

void draw_button(Button btn) {
    // Background
    glColor3f(0.3f, 0.3f, 0.4f);
    glBegin(GL_QUADS);
        glVertex2f(btn.x, win_h - btn.y - btn.h);
        glVertex2f(btn.x + btn.w, win_h - btn.y - btn.h);
        glVertex2f(btn.x + btn.w, win_h - btn.y);
        glVertex2f(btn.x, win_h - btn.y);
    glEnd();

    // Text
    glColor3f(1,1,1);
    glRasterPos2f(btn.x + 10, win_h - btn.y - 18);
    for (const char* c = btn.text; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

void open_image_action() {
    char filename[512] = {0};
    printf("Enter full image path: ");
    scanf("%511s", filename);
    if (image_data) stbi_image_free(image_data);
    image_data = stbi_load(filename, &img_w, &img_h, &img_channels, 0);
    if (image_data) {
        if (texture) glDeleteTextures(1, &texture);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLenum fmt = (img_channels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, img_w, img_h, 0, fmt, GL_UNSIGNED_BYTE, image_data);
        scale = 1.0f;
    }
}

void save_image_action() {
    if (!image_data) return;
    char filename[512] = {0};
    printf("Enter save name (e.g. output.ppm): ");
    scanf("%511s", filename);
    FILE* f = fopen(filename, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", img_w, img_h);
        for (int i = 0; i < img_w * img_h * img_channels; i += img_channels) {
            fputc(image_data[i], f);
            fputc(image_data[i+1], f);
            fputc(image_data[i+2], f);
        }
        fclose(f);
        printf("Saved!\n");
    }
}

void reset_zoom() { scale = 1.0f; }
void zoom_in()     { scale *= 1.2f; }
void zoom_out()    { scale /= 1.2f; }

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (texture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        float w = img_w * scale;
        float h = img_h * scale;
        float x = (win_w - w) / 2.0f;
        float y = (win_h - h) / 2.0f - 30;   // below buttons

        glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(x, y);
            glTexCoord2f(1, 1); glVertex2f(x + w, y);
            glTexCoord2f(1, 0); glVertex2f(x + w, y + h);
            glTexCoord2f(0, 0); glVertex2f(x, y + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    // Draw buttons
    for (int i = 0; i < 5; i++) {
        draw_button(buttons[i]);
    }

    glutSwapBuffers();
}

void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        for (int i = 0; i < 5; i++) {
            Button b = buttons[i];
            int by = win_h - b.y - b.h;
            if (x > b.x && x < b.x + b.w && y > by && y < by + b.h) {
                b.action();
                glutPostRedisplay();
                return;
            }
        }
    }
}

void reshape(int w, int h) {
    win_w = w; win_h = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(win_w, win_h);
    glutCreateWindow("GLUT Image Editor - Click Buttons");

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    init_buttons();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);

    glutMainLoop();
    return 0;
}
