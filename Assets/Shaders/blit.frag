#version 450

// ========================================
// Blit (Composite) Fragment Shader
// ========================================
// ToneMappedColorテクスチャをそのままスワップチェーンに出力

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

void main()
{
    outColor = texture(inputTexture, fragUV);
}
