#version 330

precision mediump float;

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;
uniform sampler2D tex;

in vec2 uv;
in vec4 color;
out vec4 out_color;

#define BM_RANGE 3.0
#define PI 3.14159265359

void main(void) {
    float k = (sin(time) + 1.0) / 2.0;
    float a = 2.0*PI*k;
    float b = -2.0*k;
    float x = sqrt(b*log(uv.x)) * cos(a * uv.y);
    float y = sqrt(b*log(uv.x)) * sin(a * uv.y);

    if (-BM_RANGE <= x && x < BM_RANGE && -BM_RANGE <= y && y < BM_RANGE) {
        x = (x + BM_RANGE) / (2.0 * BM_RANGE);
        y = (y + BM_RANGE) / (2.0 * BM_RANGE);
        out_color = texture(tex, vec2(x, y));
    } else {
        out_color = vec4(0.0);
    }
}
