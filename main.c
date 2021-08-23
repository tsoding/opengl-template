#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <GLFW/glfw3.h>

#define DEFAULT_SCREEN_WIDTH 1600
#define DEFAULT_SCREEN_HEIGHT 900
#define MANUAL_TIME_STEP 0.1

#include "glextloader.c"

char *slurp_file(const char *file_path)
{
#define SLURP_FILE_PANIC \
    do { \
        fprintf(stderr, "Could not read file `%s`: %s\n", file_path, strerror(errno)); \
        exit(1); \
    } while (0)

    FILE *f = fopen(file_path, "r");
    if (f == NULL) SLURP_FILE_PANIC;
    if (fseek(f, 0, SEEK_END) < 0) SLURP_FILE_PANIC;

    long size = ftell(f);
    if (size < 0) SLURP_FILE_PANIC;

    char *buffer = malloc(size + 1);
    if (buffer == NULL) SLURP_FILE_PANIC;

    if (fseek(f, 0, SEEK_SET) < 0) SLURP_FILE_PANIC;

    fread(buffer, 1, size, f);
    if (ferror(f) < 0) SLURP_FILE_PANIC;

    buffer[size] = '\0';

    if (fclose(f) < 0) SLURP_FILE_PANIC;

    return buffer;
#undef SLURP_FILE_PANIC
}

const char *shader_type_as_cstr(GLuint shader)
{
    switch (shader) {
    case GL_VERTEX_SHADER:
        return "GL_VERTEX_SHADER";
    case GL_FRAGMENT_SHADER:
        return "GL_FRAGMENT_SHADER";
    default:
        return "(Unknown)";
    }
}

bool compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader)
{
    *shader = glCreateShader(shader_type);
    glShaderSource(*shader, 1, &source, NULL);
    glCompileShader(*shader);

    GLint compiled = 0;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLchar message[1024];
        GLsizei message_size = 0;
        glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
        fprintf(stderr, "ERROR: could not compile %s\n", shader_type_as_cstr(shader_type));
        fprintf(stderr, "%.*s\n", message_size, message);
        return false;
    }

    return true;
}

bool compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader)
{
    char *source = slurp_file(file_path);
    bool ok = compile_shader_source(source, shader_type, shader);
    if (!ok) {
        fprintf(stderr, "ERROR: failed to compile `%s` shader file\n", file_path);
    }
    free(source);
    return ok;
}

bool link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program)
{
    *program = glCreateProgram();

    glAttachShader(*program, vert_shader);
    glAttachShader(*program, frag_shader);
    glLinkProgram(*program);

    GLint linked = 0;
    glGetProgramiv(*program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLsizei message_size = 0;
        GLchar message[1024];

        glGetProgramInfoLog(*program, sizeof(message), &message_size, message);
        fprintf(stderr, "Program Linking: %.*s\n", message_size, message);
    }

    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    return program;
}

typedef enum {
    RESOLUTION_UNIFORM = 0,
    TIME_UNIFORM,
    MOUSE_UNIFORM,
    COUNT_UNIFORMS
} Uniform;

static_assert(COUNT_UNIFORMS == 3, "Update list of uniform names");
static const char *uniform_names[COUNT_UNIFORMS] = {
    [RESOLUTION_UNIFORM] = "resolution",
    [TIME_UNIFORM] = "time",
    [MOUSE_UNIFORM] = "mouse",
};

// Global variables (fragile people with CS degree look away)
bool program_failed = false;
double time = 0.0;
GLuint main_program = 0;
GLint main_uniforms[COUNT_UNIFORMS];
bool pause = false;

bool load_shader_program(const char *vertex_file_path,
                         const char *fragment_file_path,
                         GLuint *program)
{
    GLuint vert = 0;
    if (!compile_shader_file(vertex_file_path, GL_VERTEX_SHADER, &vert)) {
        return false;
    }

    GLuint frag = 0;
    if (!compile_shader_file(fragment_file_path, GL_FRAGMENT_SHADER, &frag)) {
        return false;
    }

    if (!link_program(vert, frag, program)) {
        return false;
    }

    return true;
}

void reload_shaders(void)
{
    glDeleteProgram(main_program);

    program_failed = true;
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);

    {
        if (!load_shader_program("main.vert", "main.frag", &main_program)) {
            return;
        }

        glUseProgram(main_program);

        for (Uniform index = 0; index < COUNT_UNIFORMS; ++index) {
            main_uniforms[index] = glGetUniformLocation(main_program, uniform_names[index]);
        }
    }

    program_failed = false;
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    printf("Successfully Reload the Shaders\n");
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void) window;
    (void) scancode;
    (void) action;
    (void) mods;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F5) {
            reload_shaders();
        } else if (key == GLFW_KEY_SPACE) {
            pause = !pause;
        } else if (key == GLFW_KEY_Q) {
            exit(1);
        }

        if (pause) {
            if (key == GLFW_KEY_LEFT) {
                time -= MANUAL_TIME_STEP;
            } else if (key == GLFW_KEY_RIGHT) {
                time += MANUAL_TIME_STEP;
            }
        }
    }
}

void window_size_callback(GLFWwindow* window, int width, int height)
{
    (void) window;
    glViewport(0, 0, width, height);
}

void MessageCallback(GLenum source,
                     GLenum type,
                     GLuint id,
                     GLenum severity,
                     GLsizei length,
                     const GLchar* message,
                     const void* userParam)
{
    (void) source;
    (void) id;
    (void) length;
    (void) userParam;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}

int main()
{
    if (!glfwInit()) {
        fprintf(stderr, "ERROR: could not initialize GLFW\n");
        exit(1);
    }

    GLFWwindow * const window = glfwCreateWindow(
                                    DEFAULT_SCREEN_WIDTH,
                                    DEFAULT_SCREEN_HEIGHT,
                                    "OpenGL Template",
                                    NULL,
                                    NULL);
    if (window == NULL) {
        fprintf(stderr, "ERROR: could not create a window.\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);

    load_gl_extensions();

    if (glDrawArraysInstanced == NULL) {
        fprintf(stderr, "Support for EXT_draw_instanced is required!\n");
        exit(1);
    }

    if (glDebugMessageCallback != NULL) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, 0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    reload_shaders();

    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, window_size_callback);

    time = glfwGetTime();
    double prev_time;
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        if (!program_failed) {
            static_assert(COUNT_UNIFORMS == 3, "Update the uniform sync");
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            glUniform2f(main_uniforms[RESOLUTION_UNIFORM], width, height);
            glUniform1f(main_uniforms[TIME_UNIFORM], time);
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            glUniform2f(main_uniforms[MOUSE_UNIFORM], xpos, height - ypos);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
        double cur_time = glfwGetTime();
        if (!pause) {
            time += cur_time - prev_time;
        }
        prev_time = cur_time;
    }

    return 0;
}
