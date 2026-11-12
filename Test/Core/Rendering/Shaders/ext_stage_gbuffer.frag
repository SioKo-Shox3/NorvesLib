#version 450

// DefaultLit と DiffuseOnly の契約は ShadingModelIds.h。床の深度は書き換えない。
layout(set = 0, binding = 0, std140) uniform MarkerParams
{
    vec4 Color;
};
layout(location = 0) out vec4 OutAlbedo;
layout(location = 1) out vec4 OutNormal;
layout(location = 2) out vec4 OutMaterial;
layout(location = 3) out vec4 OutEmissive;
layout(location = 4) out vec2 OutVelocity;
void main()
{
    OutAlbedo = Color;
    // 上向きの床、DefaultLit=0、DiffuseOnly=8 を4ビット左へ詰める。
    OutNormal = vec4(0.0, 1.0, 0.0, 128.0);
    OutMaterial = vec4(0.0, 1.0, 1.0, 0.0);
    OutEmissive = vec4(0.0);
    OutVelocity = vec2(0.0);
}
