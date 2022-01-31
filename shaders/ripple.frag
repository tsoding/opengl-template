// Applies sinc function to the user texture.
// Stolen from http://adrianboeing.blogspot.com/2011/02/ripple-effect-in-webgl.html
#version 330

precision mediump float;

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;
uniform sampler2D tex;

in vec2 uv;
in vec4 color;
out vec4 out_color;

void main(void) {
    vec2 cPos = 2.0*uv - 1.0;
    float cLength = length(cPos);

    vec2 tex_uv = uv + (cPos/cLength)*mix(cos(cLength*12.0-time*4.0)*0.03, 0.0, cLength / 0.25);

    out_color = texture(tex, tex_uv);
}
