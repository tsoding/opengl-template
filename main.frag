#version 300 es

precision mediump float;

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;

out vec4 out_color;

#define R 500.0

void main(void) {
    vec2 coord_uv = gl_FragCoord.xy / resolution;
    vec2 mouse_uv = mouse / resolution;

    float t = 1.0 - min(length(coord_uv - mouse_uv), R) / R;

    out_color = vec4(
        (sin(t*(coord_uv.x + time)) + 1.0) / 2.0,
        (cos(t*(coord_uv.y + time)) + 1.0) / 2.0,
        (cos(t*(coord_uv.x + coord_uv.y + time)) + 1.0) / 2.0,
        1.0);
}
