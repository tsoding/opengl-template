// General purpose vertex shader that maps screen coordinates to NDC.
// Supports position, uv and color of the vertex.
#version 330

layout(location = 0) in vec2 ver_pos;
layout(location = 1) in vec2 ver_uv;
layout(location = 2) in vec4 ver_color;

uniform vec2 resolution;

precision mediump float;

out vec2 uv;
out vec4 color;

vec2 screen_project(vec2 pos)
{
    return (pos / resolution) * 2.0 - 1.0;
}

void main(void)
{
    float scale = 300.0;
    gl_Position = vec4(screen_project(ver_pos + resolution * 0.5), 0.0, 1.0);
    uv = ver_uv;
    color = ver_color;
}
