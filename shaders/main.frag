#version 330

precision mediump float;

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;

in vec2 uv;
in vec4 color;
out vec4 out_color;

#define R 500.0

void main(void) {
#if 0
    out_color = color;
#else
    vec2 coord_uv = gl_FragCoord.xy / resolution;
    vec2 mouse_uv = mouse / resolution;

    float t = 1.0 - min(length(uv - mouse_uv), R) / R;

    out_color = vec4(
        (sin(t*(uv.x + time)) + 1.0) / 2.0,
        (cos(t*(uv.y + time)) + 1.0) / 2.0,
        (cos(t*(uv.x + coord_uv.y + time)) + 1.0) / 2.0,
        1.0);
#endif
}
