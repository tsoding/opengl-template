#version 330

precision mediump float;

out vec2 uv;

void main(void)
{
    uv.x = (gl_VertexID & 1);
    uv.y = ((gl_VertexID >> 1) & 1);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
