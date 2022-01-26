#version 330

precision mediump float;

uniform sampler2D tex;
uniform vec4 color;
uniform float t;
uniform vec2 resolution;

in vec2 uv;
out vec4 out_color;

void main(void) {
    out_color = mix(texture(tex, (resolution / vec2(1600.0, 900.0)) * uv), color, t);
}
