#version 330

precision mediump float;

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;

in vec2 uv;
in vec4 color;
out vec4 out_color;

void main(void) {
    out_color = vec4(
        (sin(uv.x + time) + 1.0) / 2.0,
        (cos(uv.y + time) + 1.0) / 2.0,
        (cos(uv.x + uv.y + time) + 1.0) / 2.0,
        1.0);
}
