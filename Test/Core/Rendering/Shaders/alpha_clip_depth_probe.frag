#version 450

// 深度が空の画素へ試しの発光を置く。発光以外の添付はパイプラインの書き込みマスクで保つ。
layout(set = 0, binding = 0, std140) uniform DepthProbeParams
{
    vec4 Emissive;
    vec4 Depth;
};

layout(location = 0) out vec4 OutAlbedo;
layout(location = 1) out vec4 OutNormal;
layout(location = 2) out vec4 OutMaterial;
layout(location = 3) out vec4 OutEmissive;
layout(location = 4) out vec2 OutVelocity;

void main()
{
    OutAlbedo = vec4(0.0);
    OutNormal = vec4(0.0);
    OutMaterial = vec4(0.0);
    OutEmissive = Emissive;
    OutVelocity = vec2(0.0);
}
