#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#define LA_IMPLEMENTATION
#include "la.h"

#define DEFAULT_SCREEN_WIDTH 1600
#define DEFAULT_SCREEN_HEIGHT 900
#define MANUAL_TIME_STEP 0.1

#include "glextloader.c"

char *slurp_file_into_malloced_cstr(const char *file_path)
{
    FILE *f = NULL;
    char *buffer = NULL;

    f = fopen(file_path, "r");
    if (f == NULL) goto fail;
    if (fseek(f, 0, SEEK_END) < 0) goto fail;

    long size = ftell(f);
    if (size < 0) goto fail;

    buffer = malloc(size + 1);
    if (buffer == NULL) goto fail;

    if (fseek(f, 0, SEEK_SET) < 0) goto fail;

    fread(buffer, 1, size, f);
    if (ferror(f)) goto fail;

    buffer[size] = '\0';

    if (f) {
        fclose(f);
        errno = 0;
    }
    return buffer;
fail:
    if (f) {
        int saved_errno = errno;
        fclose(f);
        errno = saved_errno;
    }
    if (buffer) {
        free(buffer);
    }
    return NULL;
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
    char *source = slurp_file_into_malloced_cstr(file_path);
    if (source == NULL) {
        fprintf(stderr, "ERROR: failed to read file `%s`: %s\n", file_path, strerror(errno));
        errno = 0;
        return false;
    }
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

typedef enum {
    VA_POS = 0,
    VA_UV,
    VA_COLOR,
    COUNT_VAS,
} Vertex_Attrib;

typedef struct {
    V2f pos;
    V2f uv;
    V4f color;
} Vertex;

#define VERTEX_BUF_CAP (8 * 1024)
typedef struct {
    GLuint vao;
    GLuint vbo;
    bool program_failed;
    GLuint program;
    GLint uniforms[COUNT_UNIFORMS];
    Vertex vertex_buf[VERTEX_BUF_CAP];
    size_t vertex_buf_sz;
} Renderer;

// Global variables (fragile people with CS degree look away)
static double time = 0.0;
static bool pause = false;
static Renderer global_renderer = {0};

void renderer_push_vertex(Renderer *r, V2f pos, V2f uv, V4f color)
{
    assert(r->vertex_buf_sz < VERTEX_BUF_CAP);
    r->vertex_buf[r->vertex_buf_sz].pos = pos;
    r->vertex_buf[r->vertex_buf_sz].uv = uv;
    r->vertex_buf[r->vertex_buf_sz].color = color;
    r->vertex_buf_sz += 1;
}

void renderer_push_quad(Renderer *r, V2f p1, V2f p2, V4f color)
{
    V2f a = p1;
    V2f b = v2f(p2.x, p1.y);
    V2f c = v2f(p1.x, p2.y);
    V2f d = p2;

    renderer_push_vertex(r, a, v2f(0.0f, 0.0f), color);
    renderer_push_vertex(r, b, v2f(1.0f, 0.0f), color);
    renderer_push_vertex(r, c, v2f(0.0f, 1.0f), color);

    renderer_push_vertex(r, b, v2f(1.0f, 0.0f), color);
    renderer_push_vertex(r, c, v2f(0.0f, 1.0f), color);
    renderer_push_vertex(r, d, v2f(1.0f, 1.0f), color);
}

void renderer_push_checker_board(Renderer *r, int grid_size)
{
    float cell_width = 2.0f/grid_size;
    float cell_height = 2.0f/grid_size;
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            renderer_push_quad(
                r,
                v2f(-1.0f + x*cell_width, -1.0f + y*cell_height),
                v2f(-1.0f + (x + 1)*cell_width, -1.0f + (y + 1)*cell_height),
                (x + y)%2 == 0 ? v4f(1.0f, 0.0f, 0.0f, 1.0f) : v4f(0.0f, 0.0f, 0.0f, 1.0f));
        }
    }
}

void renderer_sync(Renderer *r)
{
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,
                    sizeof(Vertex) * r->vertex_buf_sz,
                    r->vertex_buf);
}

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

void renderer_reload_shaders(Renderer *r)
{
    glDeleteProgram(r->program);

    r->program_failed = true;
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);

    {
        if (!load_shader_program("main.vert", "main.frag", &r->program)) {
            return;
        }

        glUseProgram(r->program);

        for (Uniform index = 0; index < COUNT_UNIFORMS; ++index) {
            r->uniforms[index] = glGetUniformLocation(r->program, uniform_names[index]);
        }
    }

    r->program_failed = false;
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
            renderer_reload_shaders(&global_renderer);
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

void renderer_init(Renderer *r)
{
    glGenVertexArrays(1, &r->vao);
    glBindVertexArray(r->vao);

    glGenBuffers(1, &r->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(r->vertex_buf), r->vertex_buf, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(VA_POS);
    glVertexAttribPointer(VA_POS,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          (void*) offsetof(Vertex, pos));

    glEnableVertexAttribArray(VA_UV);
    glVertexAttribPointer(VA_UV,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          (void*) offsetof(Vertex, uv));

    glEnableVertexAttribArray(VA_COLOR);
    glVertexAttribPointer(VA_COLOR,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          (void*) offsetof(Vertex, color));
}

int main()
{
    if (!glfwInit()) {
        fprintf(stderr, "ERROR: could not initialize GLFW\n");
        exit(1);
    }

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

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

    int gl_ver_major = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MAJOR);
    int gl_ver_minor = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MINOR);
    printf("OpenGL %d.%d\n", gl_ver_major, gl_ver_minor);

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

    renderer_init(&global_renderer);


    renderer_push_quad(&global_renderer, v2f(-1.0f, -1.0f), v2f(1.0f, 1.0f), (V4f) {
        0
    });
    renderer_sync(&global_renderer);
    renderer_reload_shaders(&global_renderer);

    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, window_size_callback);

    time = glfwGetTime();
    double prev_time = 0.0;
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        if (!global_renderer.program_failed) {
            static_assert(COUNT_UNIFORMS == 3, "Update the uniform sync");
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            glUniform2f(global_renderer.uniforms[RESOLUTION_UNIFORM], (GLfloat) width, (GLfloat) height);
            glUniform1f(global_renderer.uniforms[TIME_UNIFORM], (GLfloat) time);
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            glUniform2f(global_renderer.uniforms[MOUSE_UNIFORM], (GLfloat) xpos, (GLfloat) (height - ypos));
            glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei) global_renderer.vertex_buf_sz, 1);
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
