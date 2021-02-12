#version 130

void main(void)
{
    int gray = gl_VertexID ^ (gl_VertexID >> 1);

    gl_Position = vec4(
        2 * (gray / 2) - 1,
        2 * (gray % 2) - 1,
        0.0,
        1.0);
};
