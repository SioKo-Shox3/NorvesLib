#version 450

// ========================================
// Blit (Composite) Fragment Shader
// ========================================
// ToneMappedColorをpresentation surfaceのtransfer責務へ渡す

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(std140, set = 0, binding = 1) uniform PresentationParams
{
    uint encodePath;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} params;

float EncodeSrgb(float L)
{
    return L <= 0.0031308 ? 12.92 * L : 1.055 * pow(L, 1.0 / 2.4) - 0.055;
}

void main()
{
    vec4 color = texture(inputTexture, fragUV);
    if (params.encodePath == 1u)
    {
        color.rgb = round(vec3(EncodeSrgb(color.r), EncodeSrgb(color.g), EncodeSrgb(color.b)) * 255.0) /
                    255.0;
    }
    outColor = color;
}
