#version 440
// Full-viewport triangle for the Vulkan canvas surface (S1-1.1). Vulkan clip space has y pointing
// down, so uv (0, 0) lands on the top-left texel of the frame texture.
layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
