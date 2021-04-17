#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#define GLEW_STATIC
#include <GL/glew.h>

#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

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
    case GL_VERTEX_SHADER:   return "GL_VERTEX_SHADER";
    case GL_FRAGMENT_SHADER: return "GL_FRAGMENT_SHADER";
    default:                 return "(Unknown)";
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
    bool err = compile_shader_source(source, shader_type, shader);
    free(source);
    return err;
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

// Global variables (fragile people with CS degree look away)
bool program_failed = false;
GLuint program = 0;
GLint resolution_uniform = 0;
GLint time_uniform = 0;
bool pause = false;

void reload_shaders(void)
{
    glDeleteProgram(program);

    program_failed = false;
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    GLuint vert = 0;
    if (!compile_shader_file("./main.vert", GL_VERTEX_SHADER, &vert)) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        program_failed = true;
        return;
    }

    GLuint frag = 0;
    if (!compile_shader_file("./main.frag", GL_FRAGMENT_SHADER, &frag)) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        program_failed = true;
        return;
    }

    if (!link_program(vert, frag, &program)) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        program_failed = true;
        return;
    }

    glUseProgram(program);

    resolution_uniform = glGetUniformLocation(program, "resolution");
    time_uniform = glGetUniformLocation(program, "time");

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
        }
    }
}

void window_size_callback(GLFWwindow* window, int width, int height)
{
    (void) window;
    glViewport(
        width / 2 - SCREEN_WIDTH / 2,
        height / 2 - SCREEN_HEIGHT / 2,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
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
                                    SCREEN_WIDTH,
                                    SCREEN_HEIGHT,
                                    "OpenGL Template",
                                    NULL,
                                    NULL);
    if (window == NULL) {
        fprintf(stderr, "ERROR: could not create a window.\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);

    if (GLEW_OK != glewInit()) {
        fprintf(stderr, "Could not initialize GLEW!\n");
        exit(1);
    }

    if (!GLEW_EXT_draw_instanced) {
        fprintf(stderr, "Support for EXT_draw_instanced is required!\n");
        exit(1);
    }

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);

    reload_shaders();

    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, window_size_callback);

    double time = glfwGetTime();
    double prev_time;
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        if (!program_failed) {
            glUniform2f(resolution_uniform,
                        SCREEN_WIDTH,
                        SCREEN_HEIGHT);
            glUniform1f(time_uniform, time);

            glDrawArraysInstancedEXT(GL_TRIANGLE_STRIP, 0, 4, 1);
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
