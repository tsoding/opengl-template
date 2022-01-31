// Single triangle strip quad generated entirely on the vertex shader.
// Simply do glDrawArrays(GL_TRIANGLE_STRIP, 0, 4) and the shader
// generates 4 points from gl_VertexID. No Vertex Attributes are
// required.
#version 330

precision mediump float;

out vec2 uv;

void main(void)
{
    uv.x = (gl_VertexID & 1);
    uv.y = ((gl_VertexID >> 1) & 1);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
