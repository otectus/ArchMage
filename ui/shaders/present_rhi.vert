#version 440
// Full-viewport triangle for the QRhiWidget canvas surface (S1-1.1). QRhi backends differ in clip
// space orientation; `params.x` is -1 when the backend's NDC y axis points up, +1 otherwise.
layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform Params {
    vec4 params;
};

void main() {
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = v_uv * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y * params.x, 0.0, 1.0);
}
