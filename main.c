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

#define COLOR_BLACK_V4F ((V4f){0.0f, 0.0f, 0.0f, 1.0f})
#define COLOR_RED_V4F ((V4f){1.0f, 0.0f, 0.0f, 1.0f})
#define COLOR_GREEN_V4F ((V4f){0.0f, 1.0f, 0.0f, 1.0f})
#define COLOR_BLUE_V4F ((V4f){0.0f, 0.0f, 1.0f, 1.0f})

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
    TEX_UNIFORM,
    COUNT_UNIFORMS
} Uniform;

static_assert(COUNT_UNIFORMS == 4, "Update list of uniform names");
static const char *uniform_names[COUNT_UNIFORMS] = {
    [RESOLUTION_UNIFORM] = "resolution",
    [TIME_UNIFORM] = "time",
    [MOUSE_UNIFORM] = "mouse",
    [TEX_UNIFORM] = "tex",
};

typedef enum {
    PROGRAM_SCENE = 0,
    PROGRAM_POST0,
    PROGRAM_POST1,
    COUNT_PROGRAMS
} Program;

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
    GLuint programs[COUNT_PROGRAMS];
    GLint uniforms[COUNT_PROGRAMS][COUNT_UNIFORMS];
    size_t vertex_buf_sz;
    Vertex vertex_buf[VERTEX_BUF_CAP];
} Renderer;

// Global variables (fragile people with CS degree look away)
static double time = 0.0;
static bool pause = false;
static GLuint user_texture = 0;
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
                     GLuint program,
                     GLfloat resolution_width, GLfloat resolution_height,
                     GLfloat time,
                     GLfloat mouse_x, GLfloat mouse_y,
                     GLint tex_unit)
{
    static_assert(COUNT_UNIFORMS == 4, "Exhaustive uniform handling in ");
    glUniform2f(r->uniforms[program][RESOLUTION_UNIFORM], resolution_width, resolution_height);
    glUniform1f(r->uniforms[program][TIME_UNIFORM], time);
    glUniform2f(r->uniforms[program][MOUSE_UNIFORM], mouse_x, mouse_y);
    glUniform1i(r->uniforms[program][TEX_UNIFORM], tex_unit);
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

typedef struct {
    float x, y, dx, dy;
} Object;

#define OBJECTS_CAP 1024
Object objects[OBJECTS_CAP];
size_t objects_count = 0;

static const char *vert_path[COUNT_PROGRAMS] = {0};
static const char *frag_path[COUNT_PROGRAMS] = {0};
static const char *texture_path = NULL;
static float follow_scale = 1.0f;
static float object_size = 100.0f;
static float rotate_radius = 500.0f;
static float rotate_speed = 4.0f;

void object_render(Renderer *r, Object *object)
{
    r_quad_cr(
        r,
        v2f(object->x, object->y),
        v2ff(object_size),
        COLOR_BLACK_V4F);
}

void object_update(Object *obj, float delta_time,
                   float target_x, float target_y)
{
    if (!pause) {
        obj->x += delta_time * obj->dx;
        obj->y += delta_time * obj->dy;
        obj->dx = (target_x - obj->x) * follow_scale;
        obj->dy = (target_y - obj->y) * follow_scale;
    }
}

void reload_render_conf(const char *render_conf_path)
{
    if (render_conf) free(render_conf);

    render_conf = slurp_file_into_malloced_cstr(render_conf_path);
    if (render_conf == NULL) {
        fprintf(stderr, "ERROR: could not load %s: %s\n", render_conf_path, strerror(errno));
        exit(1);
    }

    String_View content = sv_from_cstr(render_conf);

    for (Program p = 0; p < COUNT_PROGRAMS; ++p) {
        vert_path[p] = NULL;
        frag_path[p] = NULL;
    }
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

            static_assert(COUNT_PROGRAMS == 3, "Exhaustive handling of shader programs in config parsing");
            if (sv_eq(key, SV("vert[SCENE]"))) {
                vert_path[PROGRAM_SCENE] = value.data;
            } else if (sv_eq(key, SV("frag[SCENE]"))) {
                frag_path[PROGRAM_SCENE] = value.data;
            } else if (sv_eq(key, SV("vert[POST0]"))) {
                vert_path[PROGRAM_POST0] = value.data;
            } else if (sv_eq(key, SV("frag[POST0]"))) {
                frag_path[PROGRAM_POST0] = value.data;
            } else if (sv_eq(key, SV("vert[POST1]"))) {
                vert_path[PROGRAM_POST1] = value.data;
            } else if (sv_eq(key, SV("frag[POST1]"))) {
                frag_path[PROGRAM_POST1] = value.data;
            } else if (sv_eq(key, SV("texture"))) {
                texture_path = value.data;
            } else if (sv_eq(key, SV("follow_scale"))) {
                follow_scale = strtof(value.data, NULL);
            } else if (sv_eq(key, SV("object_size"))) {
                object_size = strtof(value.data, NULL);
            } else if (sv_eq(key, SV("rotate_radius"))) {
                rotate_radius = strtof(value.data, NULL);
            } else if (sv_eq(key, SV("rotate_speed"))) {
                rotate_speed = strtof(value.data, NULL);
            } else if (sv_eq(key, SV("objects_count"))) {
                objects_count = strtol(value.data, NULL, 10);
                if (objects_count > OBJECTS_CAP) {
                    printf("%s:%d:%ld: WARNING: objects_count overflow\n",
                           render_conf_path, row, key.data - line_start);
                    objects_count = OBJECTS_CAP;
                }
            } else {
                printf("%s:%d:%ld: ERROR: unsupported key `"SV_Fmt"`\n",
                       render_conf_path, row, key.data - line_start,
                       SV_Arg(key));
                continue;
            }

            printf(SV_Fmt" = %s\n", SV_Arg(key), value.data);
        }
    }
}

bool reload_user_textures(void)
{
    int texture_width, texture_height;
    unsigned char *texture_pixels = stbi_load(texture_path, &texture_width, &texture_height, NULL, 4);
    if (texture_pixels == NULL) {
        fprintf(stderr, "ERROR: could not load image %s: %s\n",
                texture_path, strerror(errno));
        return false;
    }

    glDeleteTextures(1, &user_texture);
    glGenTextures(1, &user_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, user_texture);

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
    for (Program p = 0; p < COUNT_PROGRAMS; ++p) {
        glDeleteProgram(r->programs[p]);

        if (!load_shader_program(vert_path[p], frag_path[p], &r->programs[p])) return false;

        glUseProgram(r->programs[p]);

        for (Uniform index = 0; index < COUNT_UNIFORMS; ++index) {
            r->uniforms[p][index] = glGetUniformLocation(r->programs[p], uniform_names[index]);
        }
    }

    printf("Successfully reloaded the Shaders\n");
    return true;
}

bool r_reload(Renderer *r)
{
    r->reload_failed = true;
    if (!r_reload_shaders(r)) return false;
    r->reload_failed = false;

    return true;
}

static GLuint scene_framebuffer = {0};
static GLuint scene_texture = 0;

void scene_framebuffer_init(void)
{
    glGenTextures(1, &scene_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, scene_texture);

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

    glGenFramebuffers(1, &scene_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_texture, 0);

    GLenum draw_buffers = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &draw_buffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ERROR: Could not complete the framebuffer\n");
        exit(1);
    }

    printf("Successfully created the debug framebuffer\n");
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void) scancode;
    (void) action;
    (void) mods;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F5) {
            reload_render_conf("render.conf");
            reload_user_textures();
            r_reload(&global_renderer);
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
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL);
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

    scene_framebuffer_init();

    Renderer *r = &global_renderer;

    reload_user_textures();

    r_init(r);
    r_reload(r);

    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, window_size_callback);

    time = glfwGetTime();
    double prev_time = 0.0;
    double delta_time = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        xpos = xpos - width * 0.5f;
        ypos = (height - ypos) - height * 0.5f;

        if (!r->reload_failed) {
            static_assert(COUNT_PROGRAMS == 3, "Exhaustive handling of shader programs in the event loop");

            glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);
            {
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glUseProgram(r->programs[PROGRAM_SCENE]);
                r_clear(r);
                r_sync_uniforms(r, PROGRAM_SCENE, width, height, time, xpos, ypos, 0);
                for (size_t i = objects_count; i > 0; --i) {
                    object_render(r, &objects[i - 1]);
                }
                r_sync_buffers(r);

                glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei) r->vertex_buf_sz, 1);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            {
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);

#if 0
                glUseProgram(r->programs[PROGRAM_POST0]);
                r_sync_uniforms(r, PROGRAM_POST0, width, height, time, xpos, ypos, 1);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#endif

                glUseProgram(r->programs[PROGRAM_POST1]);
                r_clear(r);
                r_sync_uniforms(r, PROGRAM_POST1, width, height, time, xpos, ypos, 1);
                r_quad_cr(r, v2ff(0.0f), v2f(width * 0.5, height * 0.5), COLOR_BLACK_V4F);
                r_sync_buffers(r);
                glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei) r->vertex_buf_sz, 1);
            }
        } else {
            glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        if (objects_count > 0) {
            float follow_x = xpos + sin(time * rotate_speed) * rotate_radius;
            float follow_y = ypos + cos(time * rotate_speed) * rotate_radius;

            object_update(&objects[0], delta_time, follow_x, follow_y);
            for (size_t i = 1; i < objects_count; ++i) {
                object_update(&objects[i], delta_time, objects[i - 1].x, objects[i - 1].y);
            }
        }

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
