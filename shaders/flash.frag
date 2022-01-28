#version 330

precision mediump float;

uniform sampler2D tex;
uniform vec4 color;
uniform float t;

in vec2 uv;
out vec4 out_color;

void main(void) {
    out_color = mix(texture(tex, uv), color, t);
}
