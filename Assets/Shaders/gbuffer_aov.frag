#version 450

// ========================================
// 撮影の AOV: GBuffer の生の値と照明の結果を浮動小数の 3 枚へ書く（GBufferAovPass。--capture-aov）
//
//   出力 0（表面）: rgb = GBuffer.Albedo（照明が読むのと同じ線形のアルベド）、a = GBuffer.Material の粗さ（g）
//   出力 1（幾何）: xyz = GBuffer.Normal（ワールドの法線。正規化し直さない）、
//                   w = 深度から戻したワールドの位置のカメラの前方向への距離（視線方向の深度 m）
//   出力 2（照明）: r = 太陽の可視率（lighting.frag が Lighting.SpecularReflectance の a に書いた影の結果。n·l は掛けない）。
//                   照明の出力が無い構成（params.lighting.x = 0）は -1
//                   g = RTGI の時間の蓄積の後の出力の a（遮蔽の診断。2 × 符号 + 空の分、符号 = 生の空 + 2 × 読み飛ばしの分類。
//                   RTGI/DiffuseIndirect.comp の StoreTemporalSample）、b = 空間のフィルタの後の空の分。
//                   RTGI が無い構成（params.lighting.y = 0）は g・b とも -1
//                   a = 可視バッファの解決が書いた三角形の面の幾何の法線を八面体の 12 + 12 ビットに詰めた整数 + 1
//                   （1 + u × 4096 + v。float32 で正確に表せる 2^24 以下）。このフレームに解決が書かなかった画素
//                   （w が params.lighting.z と違う・ラスタの GBuffer の物）と、幾何の法線の無い構成（lighting.z = 0）は -1
//   空の画素（照明と同じ判定: アルベドの a < 0.01）は表面 0・法線 0・深度 +∞・可視率 0
// 後処理を通さないので、書いた値がそのまま撮影の読み出しに渡る。
// ========================================

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outSurface;
layout(location = 1) out vec4 outGeometry;
layout(location = 2) out vec4 outLighting;

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
    // x = 1 なら lightingReflectance が照明の出力（a = 太陽の可視率）、y = 1 なら rtgiTemporal・rtgiFiltered が同じフレームの RTGI、
    // z = geometricNormal の w がこの値の画素だけが、このフレームに解決が書いた面の法線（0 なら無い）
    vec4 lighting;
} params;
layout(set = 0, binding = 5) uniform sampler2D lightingReflectance;
layout(set = 0, binding = 6) uniform sampler2D rtgiTemporal;
layout(set = 0, binding = 7) uniform sampler2D rtgiFiltered;
layout(set = 0, binding = 8) uniform sampler2D geometricNormal;

// 単位ベクトルを八面体の 12 + 12 ビットの整数に詰めて 1 を足す（読めない法線は -1）
float PackGeometricNormal(vec3 n)
{
    const float len = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(len > 0.5))
    {
        return -1.0;
    }
    vec2 p = n.xy / len;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    const uvec2 q = uvec2(clamp(round((p * 0.5 + 0.5) * 4095.0), vec2(0.0), vec2(4095.0)));
    return float(q.x * 4096u + q.y + 1u);
}

void main()
{
    const ivec2 size = textureSize(gbufferAlbedo, 0);
    const ivec2 pixel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - ivec2(1));
    const vec4 albedo = texelFetch(gbufferAlbedo, pixel, 0);
    if (albedo.a < 0.01)
    {
        outSurface = vec4(0.0);
        outGeometry = vec4(0.0, 0.0, 0.0, uintBitsToFloat(0x7F800000u));
        outLighting = vec4(0.0);
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
    const float sunVisibility = params.lighting.x > 0.5 ? texelFetch(lightingReflectance, pixel, 0).a : -1.0;
    const bool bRTGI = params.lighting.y > 0.5;
    const float rtgiTemporalCode = bRTGI ? texelFetch(rtgiTemporal, pixel, 0).a : -1.0;
    const float rtgiFilteredSky = bRTGI ? texelFetch(rtgiFiltered, pixel, 0).a : -1.0;
    float packedGeometricNormal = -1.0;
    if (params.lighting.z > 0.5)
    {
        const vec4 geometric = texelFetch(geometricNormal, pixel, 0);
        if (geometric.w == params.lighting.z)
        {
            packedGeometricNormal = PackGeometricNormal(geometric.xyz);
        }
    }
    outLighting = vec4(sunVisibility, rtgiTemporalCode, rtgiFilteredSky, packedGeometricNormal);
}
