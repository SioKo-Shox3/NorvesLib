#version 450

// ========================================
// 撮影の AOV: GBuffer の生の値を浮動小数の 2 枚へ書く（GBufferAovPass。--capture-aov）
//
//   出力 0（表面）: rgb = GBuffer.Albedo（照明が読むのと同じ線形のアルベド）、a = GBuffer.Material の粗さ（g）
//   出力 1（幾何）: xyz = GBuffer.Normal（ワールドの法線。正規化し直さない）、
//                   w = 深度から戻したワールドの位置のカメラの前方向への距離（視線方向の深度 m）
//   空の画素（照明と同じ判定: アルベドの a < 0.01）は表面 0・法線 0・深度 +∞
// 後処理を通さないので、書いた値がそのまま撮影の読み出しに渡る。
// ========================================

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outSurface;
layout(location = 1) out vec4 outGeometry;

layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D gbufferDepth;

layout(std140, set = 0, binding = 4) uniform AovParams
{
    // lighting.frag の ReconstructWorldPosition と同じ逆ビュー投影
    mat4 invViewProjection;
    // xyz = カメラの位置
    vec4 cameraPosition;
    // xyz = カメラの前方向（単位ベクトル）
    vec4 cameraForward;
} params;

void main()
{
    const ivec2 size = textureSize(gbufferAlbedo, 0);
    const ivec2 pixel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - ivec2(1));
    const vec4 albedo = texelFetch(gbufferAlbedo, pixel, 0);
    if (albedo.a < 0.01)
    {
        outSurface = vec4(0.0);
        outGeometry = vec4(0.0, 0.0, 0.0, uintBitsToFloat(0x7F800000u));
        return;
    }
    const vec3 normal = texelFetch(gbufferNormal, pixel, 0).xyz;
    const float roughness = texelFetch(gbufferMaterial, pixel, 0).g;
    const float depth = texelFetch(gbufferDepth, pixel, 0).r;
    const vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
    const vec4 world = params.invViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    const vec3 worldPosition = world.xyz / world.w;
    const float viewDepth = dot(worldPosition - params.cameraPosition.xyz, params.cameraForward.xyz);
    outSurface = vec4(albedo.rgb, roughness);
    outGeometry = vec4(normal, viewDepth);
}
