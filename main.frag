#version 130

uniform vec2 resolution;
uniform float time;

out vec4 color;

void main(void) {
    vec2 coord = gl_FragCoord.xy / resolution;
    color = vec4(
        (sin(coord.x + time) + 1.0) / 2.0,
        (cos(coord.y + time) + 1.0) / 2.0,
        (cos(coord.x + coord.y + time) + 1.0) / 2.0,
        1.0);
}
