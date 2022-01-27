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

#define SV_IMPLEMENTATION
#include "sv.h"

#define DEFAULT_SCREEN_WIDTH 1600
#define DEFAULT_SCREEN_HEIGHT 900
#define MANUAL_TIME_STEP 0.1

#include "glextloader.c"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
    bool reload_failed;
    GLuint vao;
    GLuint vbo;
    GLuint program;
    GLint uniforms[COUNT_UNIFORMS];
    Vertex vertex_buf[VERTEX_BUF_CAP];
    size_t vertex_buf_sz;
    GLuint texture;
} Renderer;

// Global variables (fragile people with CS degree look away)
static double time = 0.0;
static bool pause = false;
static Renderer global_renderer = {0};

void r_vertex(Renderer *r, V2f pos, V2f uv, V4f color)
{
    assert(r->vertex_buf_sz < VERTEX_BUF_CAP);
    r->vertex_buf[r->vertex_buf_sz].pos = pos;
    r->vertex_buf[r->vertex_buf_sz].uv = uv;
    r->vertex_buf[r->vertex_buf_sz].color = color;
    r->vertex_buf_sz += 1;
}

void r_quad_pp(Renderer *r, V2f p1, V2f p2, V4f color)
{
    V2f a = p1;
    V2f b = v2f(p2.x, p1.y);
    V2f c = v2f(p1.x, p2.y);
    V2f d = p2;

    r_vertex(r, a, v2f(0.0f, 0.0f), color);
    r_vertex(r, b, v2f(1.0f, 0.0f), color);
    r_vertex(r, c, v2f(0.0f, 1.0f), color);

    r_vertex(r, b, v2f(1.0f, 0.0f), color);
    r_vertex(r, c, v2f(0.0f, 1.0f), color);
    r_vertex(r, d, v2f(1.0f, 1.0f), color);
}

void r_quad_cr(Renderer *r, V2f center, V2f radius, V4f color)
{
    r_quad_pp(r, v2f_sub(center, radius), v2f_sum(center, radius), color);
}

void r_sync_buffers(Renderer *r)
{
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,
                    sizeof(Vertex) * r->vertex_buf_sz,
                    r->vertex_buf);
}

void r_sync_uniforms(Renderer *r,
                     GLfloat resolution_width, GLfloat resolution_height,
                     GLfloat time,
                     GLfloat mouse_x, GLfloat mouse_y)
{
    static_assert(COUNT_UNIFORMS == 3, "Exhaustive uniform handling in ");
    glUniform2f(r->uniforms[RESOLUTION_UNIFORM], resolution_width, resolution_height);
    glUniform1f(r->uniforms[TIME_UNIFORM], time);
    glUniform2f(r->uniforms[MOUSE_UNIFORM], mouse_x, mouse_y);
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

static char *render_conf = NULL;
const char *vert_path = NULL;
const char *frag_path = NULL;
const char *texture_path = NULL;

void reload_render_conf(const char *render_conf_path)
{
    if (render_conf) free(render_conf);

    render_conf = slurp_file_into_malloced_cstr(render_conf_path);
    if (render_conf == NULL) {
        fprintf(stderr, "ERROR: could not load %s: %s\n", render_conf_path, strerror(errno));
        exit(1);
    }

    String_View content = sv_from_cstr(render_conf);

    vert_path = NULL;
    frag_path = NULL;
    texture_path = NULL;
    for (int row = 0; content.count > 0; row++) {
        String_View line = sv_chop_by_delim(&content, '\n');
        const char *line_start = line.data;
        line = sv_trim_left(line);

        if (line.count > 0 && line.data[0] != '#') {
            String_View key = sv_trim(sv_chop_by_delim(&line, '='));
            String_View value = sv_trim_left(line);

            ((char*)value.data)[value.count] = '\0';
            // ^^^SAFETY NOTES: this is needed so we can use `value` as a NULL-terminated C-string.
            // This should not cause any problems because the original string `render_conf`
            // that we are processing the `value` from is mutable, NULL-terminated and we are splitting
            // it by newlines which garantees that there is always a character after
            // the end of `value`.
            //
            // Let's consider an example where `render_conf` is equal to this:
            //
            // ```
            // key = value\n
            // key = value\n
            // key = value\0
            // ```
            //
            // There is always something after `value`. It's either `\n` or `\0`. With all of these
            // invariats in place writing to `value.data[value.count]` should be safe.

            if (sv_eq(key, SV("vert"))) {
                vert_path = value.data;
                printf("Vertex Path: %s\n", vert_path);
            } else if (sv_eq(key, SV("frag"))) {
                frag_path = value.data;
                printf("Fragment Path: %s\n", frag_path);
            } else if (sv_eq(key, SV("texture"))) {
                texture_path = value.data;
                printf("Texture Path: %s\n", texture_path);
            } else {
                printf("%s:%d:%ld: ERROR: unsupported key `"SV_Fmt"`\n",
                       render_conf_path, row, key.data - line_start,
                       SV_Arg(key));
            }
        }
    }
}

bool r_reload_textures(Renderer *r)
{
    int texture_width, texture_height;
    unsigned char *texture_pixels = stbi_load(texture_path, &texture_width, &texture_height, NULL, 4);
    if (texture_pixels == NULL) {
        fprintf(stderr, "ERROR: could not load image %s: %s\n",
                texture_path, strerror(errno));
        return false;
    }

    glDeleteTextures(1, &r->texture);
    glGenTextures(1, &r->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 texture_width,
                 texture_height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 texture_pixels);

    stbi_image_free(texture_pixels);

    printf("Successfully reloaded textures\n");
    return true;
}

bool r_reload_shaders(Renderer *r)
{
    glDeleteProgram(r->program);

    if (!load_shader_program(vert_path, frag_path, &r->program)) return false;

    glUseProgram(r->program);

    for (Uniform index = 0; index < COUNT_UNIFORMS; ++index) {
        r->uniforms[index] = glGetUniformLocation(r->program, uniform_names[index]);
    }

    printf("Successfully reloaded the Shaders\n");
    return true;
}

bool r_reload(Renderer *r)
{
    r->reload_failed = true;

    if (!r_reload_shaders(r)) return false;
    if (!r_reload_textures(r)) return false;

    r->reload_failed = false;

    return true;
}

#define COLOR_BLACK_V4F ((V4f){0.0f, 0.0f, 0.0f, 1.0f})
#define COLOR_RED_V4F ((V4f){1.0f, 0.0f, 0.0f, 1.0f})
#define COLOR_GREEN_V4F ((V4f){0.0f, 1.0f, 0.0f, 1.0f})
#define COLOR_BLUE_V4F ((V4f){0.0f, 0.0f, 1.0f, 1.0f})

float t = 0.0f;
float dt = 0.0f;
V4f color = {0};

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void) scancode;
    (void) action;
    (void) mods;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F5) {
            reload_render_conf("render.conf");
            dt = -2.0f;
            t = 1.0f;
            if (r_reload(&global_renderer)) {
                color = COLOR_GREEN_V4F;
            } else {
                color = COLOR_RED_V4F;
            }
        } else if (key == GLFW_KEY_F6) {
#define SCREENSHOT_PNG_PATH "screenshot.png"
            printf("Saving the screenshot at %s\n", SCREENSHOT_PNG_PATH);
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            void *pixels = malloc(4 * width * height);
            if (pixels == NULL) {
                fprintf(stderr, "ERROR: could not allocate memory for pixels to make a screenshot: %s\n",
                        strerror(errno));
                return;
            }
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            if (!stbi_write_png(SCREENSHOT_PNG_PATH, width, height, 4, pixels, width * 4)) {
                fprintf(stderr, "ERROR: could not save %s: %s\n", SCREENSHOT_PNG_PATH, strerror(errno));
            }
            free(pixels);
            color = COLOR_BLUE_V4F;
            dt = -2.0f;
            t = 1.0f;
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

GLuint framebuffer;

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

void r_init(Renderer *r)
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

void r_clear(Renderer *r)
{
    r->vertex_buf_sz = 0;
}



int main(void)
{
    reload_render_conf("render.conf");

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

    GLuint framebuffer_texture;
    glGenTextures(1, &framebuffer_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, framebuffer_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        DEFAULT_SCREEN_WIDTH,
        DEFAULT_SCREEN_HEIGHT,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebuffer_texture, 0);

    GLenum draw_buffers = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &draw_buffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ERROR: Could not complete the framebuffer\n");
        exit(1);
    }

    GLuint framebuffer_program;
    if (!load_shader_program("./shaders/debug.vert", "./shaders/debug.frag", &framebuffer_program)) {
        exit(1);
    }
    glUseProgram(framebuffer_program);

    GLuint framebuffer_tex_uniform = glGetUniformLocation(framebuffer_program, "tex");
    GLuint framebuffer_color_uniform = glGetUniformLocation(framebuffer_program, "color");
    GLuint framebuffer_t_uniform = glGetUniformLocation(framebuffer_program, "t");
    GLuint framebuffer_resolution_uniform = glGetUniformLocation(framebuffer_program, "resolution");

    glUniform1i(framebuffer_tex_uniform, 1);
    glUniform4f(framebuffer_color_uniform, 1.0, 0.0, 0.0, 1.0);

    printf("Successfully created the debug framebuffer\n");

    Renderer *r = &global_renderer;

    r_init(r);
    r_reload(r);

    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, window_size_callback);

    time = glfwGetTime();
    double prev_time = 0.0;
    double delta_time = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        glClear(GL_COLOR_BUFFER_BIT);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        xpos = xpos - width * 0.5f;
        ypos = (height - ypos) - height * 0.5f;

        // if (!r->reload_failed) {
            glUseProgram(r->program);
            r_clear(r);
            r_sync_uniforms(r, width, height, time, xpos, ypos);
            r_quad_cr(
                r,
                v2ff(0.0f),
                v2f_mul(v2f(width, height), v2ff(0.5f)),
                COLOR_BLACK_V4F);
            r_sync_buffers(r);

            glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei) r->vertex_buf_sz, 1);
        // }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(framebuffer_program);
        t += dt * delta_time;
        if (t <= 0.0f) {
            t = 0.0f;
            dt = 0.0f;
        }
        glUniform1f(framebuffer_t_uniform, t);
        glUniform4f(framebuffer_color_uniform, color.x, color.y, color.z, color.w);
        glUniform2f(framebuffer_resolution_uniform, width, height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);
        glfwPollEvents();
        double cur_time = glfwGetTime();
        delta_time = cur_time - prev_time;
        if (!pause) {
            time += delta_time;
        }
        prev_time = cur_time;
    }

    return 0;
}
