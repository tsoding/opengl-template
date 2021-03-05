#version 130

uniform vec2 resolution;

out vec4 color;

void main(void) {
    color = vec4(gl_FragCoord.xy / resolution, 0.0, 1.0);
};
