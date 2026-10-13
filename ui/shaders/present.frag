#version 440
// Samples the CPU-rendered canvas frame texel for texel (nearest filtering, texture size equals the
// render target size), so the presented pixels equal the CPU frame exactly.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D frameTexture;

void main() {
    fragColor = texture(frameTexture, v_uv);
}
