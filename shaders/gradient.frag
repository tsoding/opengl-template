// Applies animated over time gradient to the user texture.
#version 330

precision mediump float;

uniform float time;
uniform sampler2D tex;

in vec2 uv;
in vec4 color;
out vec4 out_color;

void main(void) {
    out_color = texture(tex, vec2(uv.x, 1.0 - uv.y)) * vec4(
        (sin(uv.x + time) + 1.0) / 2.0,
        (cos(uv.y + time) + 1.0) / 2.0,
        (cos(uv.x + uv.y + time) + 1.0) / 2.0,
        1.0);
}
