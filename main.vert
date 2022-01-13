#version 330

layout(location = 0) in vec2 ver_pos;
layout(location = 1) in vec2 ver_uv;
layout(location = 2) in vec4 ver_color;

precision mediump float;

out vec2 uv;
out vec4 color;

void main(void)
{
    gl_Position = vec4(ver_pos, 0.0, 1.0);
    uv = ver_uv;
    color = ver_color;
}
