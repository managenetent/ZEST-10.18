#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Image data
unsigned char* image_data = NULL;
int img_width = 0, img_height = 0, img_channels = 0;
GLuint texture_id = 0;

int window_width = 800;
int window_height = 600;

// Current tool / state
int flip_vertical = 0;
int flip_horizontal = 0;
float scale = 1.0f;

void load_image(const char* filename) {
    if (image_data) stbi_image_free(image_data);
    if (texture_id) glDeleteTextures(1, &texture_id);

    image_data = stbi_load(filename, &img_width, &img_height, &img_channels, 0);
    if (!image_data) {
        printf("Failed to load %s\n", filename);
        return;
    }

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum format = (img_channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, img_width, img_height, 0, format, GL_UNSIGNED_BYTE, image_data);

    printf("Loaded %s (%dx%d, channels=%d)\n", filename, img_width, img_height, img_channels);
}

void save_image(const char* filename) {
    if (!image_data) return;
    // Simple PPM save for now (easy, no extra libs)
    FILE* f = fopen(filename, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", img_width, img_height);
        for (int i = 0; i < img_width * img_height * 3; i += 3) {
            // Handle RGBA -> RGB if needed
            fputc(image_data[i], f);
            fputc(image_data[i+1], f);
            fputc(image_data[i+2], f);
        }
        fclose(f);
        printf("Saved as %s\n", filename);
    }
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw image centered and scaled
    if (texture_id) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        float w = img_width * scale;
        float h = img_height * scale;
        float x = (window_width - w) / 2;
        float y = (window_height - h) / 2;

        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex2f(x, y);
        glTexCoord2f(1, 1); glVertex2f(x + w, y);
        glTexCoord2f(1, 0); glVertex2f(x + w, y + h);
        glTexCoord2f(0, 0); glVertex2f(x, y + h);
        glEnd();

        glDisable(GL_TEXTURE_2D);
    }

    // Simple "Header Bar"
    glColor3f(0.2f, 0.2f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(0, window_height - 40);
    glVertex2f(window_width, window_height - 40);
    glVertex2f(window_width, window_height);
    glVertex2f(0, window_height);
    glEnd();

    glColor3f(1,1,1);
    // You can use glutBitmapString for text (simple)
    glRasterPos2f(10, window_height - 25);
    const char* title = "Simple GLUT Image Editor - Right click for menu";
    for (const char* c = title; *c; c++) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    }

    glutSwapBuffers();
}

void reshape(int w, int h) {
    window_width = w;
    window_height = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int x, int y) {
    switch (key) {
        case 'o': case 'O': // Open (hardcoded for now)
            load_image("test.jpg"); // Change to your image
            glutPostRedisplay();
            break;
        case 's': case 'S':
            save_image("output.ppm");
            break;
        case 'r': case 'R': // Resize example (double size)
            scale *= 1.5f;
            glutPostRedisplay();
            break;
        case 'f': case 'F': // Flip horizontal
            flip_horizontal = !flip_horizontal;
            // TODO: Actually flip pixel data
            glutPostRedisplay();
            break;
        case 'v': case 'V': // Flip vertical
            flip_vertical = !flip_vertical;
            glutPostRedisplay();
            break;
        case 27: // ESC
            exit(0);
    }
}

// Menu callback
void menu(int value) {
    switch (value) {
        case 1: load_image("test.jpg"); break;   // Change filename
        case 2: save_image("output.ppm"); break;
        case 3: scale = 1.0f; break;             // Reset view
        case 4: scale *= 1.5f; break;            // Resize +
        case 5: scale /= 1.5f; break;            // Resize -
        case 6: flip_horizontal = !flip_horizontal; break;
        case 7: flip_vertical = !flip_vertical; break;
    }
    glutPostRedisplay();
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(window_width, window_height);
    glutCreateWindow("GLUT Image Editor");

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);

    // Create right-click menu
    int file_menu = glutCreateMenu(menu);
    glutAddMenuEntry("Open (test.jpg)", 1);
    glutAddMenuEntry("Save as output.ppm", 2);

    int edit_menu = glutCreateMenu(menu);
    glutAddMenuEntry("Reset Scale", 3);
    glutAddMenuEntry("Resize +50%", 4);
    glutAddMenuEntry("Resize -33%", 5);
    glutAddMenuEntry("Flip Horizontal", 6);
    glutAddMenuEntry("Flip Vertical", 7);

    glutCreateMenu(menu);
    glutAddSubMenu("File", file_menu);
    glutAddSubMenu("Edit", edit_menu);
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    glutMainLoop();
    return 0;
}
