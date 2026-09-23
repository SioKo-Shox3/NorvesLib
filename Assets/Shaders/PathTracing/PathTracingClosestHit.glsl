// FramePacketに値コピーされた形状と材質から交差面の材質値を評価する。
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "PathTracing/PathTracingCommon.glsl"
#include "Common/PbrMaterialEvaluation.glsl"

struct PathInstance
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    vec4 baseColor;
    vec4 emission;
    uvec4 geometry; // x=頂点幅、y=頂点数、z=索引数、w=インスタンス番号
    vec4 objectColor; // GBufferと同じ規則のinstance色
    uvec4 textures; // x=アルベド、y=法線、z=metallic、w=roughnessのtexture配列番号
};
layout(set = 0, binding = 2, std430) readonly buffer PathInstances
{
    PathInstance values[];
} instances;
layout(set = 0, binding = 8) uniform sampler2D materialTextures[256];
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexData
{
    float values[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer IndexData
{
    uint values[];
};

layout(location = 0) rayPayloadInEXT PathPayload payload;
hitAttributeEXT vec2 hitBarycentrics;

// Mesh3DVertexの並び（位置3・法線3・UV2の32 byte）。これより短い頂点は位置だけを持つ。
const uint MESH3D_VERTEX_BYTES = 32u;

vec3 ReadFloat3(VertexData vertices, uint index, uint stride, uint component)
{
    uint offset = index * stride + component;
    return vec3(vertices.values[offset], vertices.values[offset + 1u],
                vertices.values[offset + 2u]);
}

vec2 ReadFloat2(VertexData vertices, uint index, uint stride, uint component)
{
    uint offset = index * stride + component;
    return vec2(vertices.values[offset], vertices.values[offset + 1u]);
}

vec4 SampleMaterialTexture(uint textureIndex, vec2 uv)
{
    // レイトレーシング段には画面微分がないため、LOD 0を標本化する。
    return textureLod(materialTextures[nonuniformEXT(textureIndex)], uv, 0.0);
}

void main()
{
    uint instanceIndex = gl_InstanceCustomIndexEXT;
    if (instanceIndex >= parameters.imageState.w)
    {
        payload.Hit = 0u;
        return;
    }
    PathInstance instance = instances.values[instanceIndex];
    uint firstIndex = uint(gl_PrimitiveID) * 3u;
    if (instance.geometry.x < 12u || firstIndex + 2u >= instance.geometry.z)
    {
        payload.Hit = 0u;
        return;
    }
    IndexData indices = IndexData(instance.indexAddress);
    uint a = indices.values[firstIndex];
    uint b = indices.values[firstIndex + 1u];
    uint c = indices.values[firstIndex + 2u];
    if (a >= instance.geometry.y || b >= instance.geometry.y || c >= instance.geometry.y)
    {
        payload.Hit = 0u;
        return;
    }
    VertexData vertices = VertexData(instance.vertexAddress);
    uint stride = instance.geometry.x / 4u;
    vec3 p0 = gl_ObjectToWorldEXT * vec4(ReadFloat3(vertices, a, stride, 0u), 1.0);
    vec3 p1 = gl_ObjectToWorldEXT * vec4(ReadFloat3(vertices, b, stride, 0u), 1.0);
    vec3 p2 = gl_ObjectToWorldEXT * vec4(ReadFloat3(vertices, c, stride, 0u), 1.0);
    vec3 edge1 = p1 - p0;
    vec3 edge2 = p2 - p0;
    vec3 geometricNormal = cross(edge1, edge2);
    float normalLengthSquared = dot(geometricNormal, geometricNormal);
    if (normalLengthSquared < 0.000000000001)
    {
        payload.Hit = 0u;
        return;
    }
    float normalLength = sqrt(normalLengthSquared);
    geometricNormal /= normalLength;
    if (dot(geometricNormal, gl_WorldRayDirectionEXT) > 0.0)
    {
        geometricNormal = -geometricNormal;
    }

    vec3 weights = vec3(1.0 - hitBarycentrics.x - hitBarycentrics.y,
                        hitBarycentrics.x, hitBarycentrics.y);
    vec3 shadingNormal = geometricNormal;
    vec2 uv = vec2(0.0);
    mat3 tangentBasis = mat3(1.0);
    bool bHasAttributes = instance.geometry.x >= MESH3D_VERTEX_BYTES;
    if (bHasAttributes)
    {
        // 法線は物体→ワールド行列の逆転置で運ぶ（非一様スケールでも面に垂直を保つ）。
        mat3 normalMatrix = transpose(mat3(gl_WorldToObjectEXT));
        vec3 interpolatedNormal = normalMatrix *
            (ReadFloat3(vertices, a, stride, 3u) * weights.x +
             ReadFloat3(vertices, b, stride, 3u) * weights.y +
             ReadFloat3(vertices, c, stride, 3u) * weights.z);
        float interpolatedLengthSquared = dot(interpolatedNormal, interpolatedNormal);
        if (interpolatedLengthSquared > 1.0e-12)
        {
            shadingNormal = interpolatedNormal * inversesqrt(interpolatedLengthSquared);
        }
        vec2 uv0 = ReadFloat2(vertices, a, stride, 6u);
        vec2 uv1 = ReadFloat2(vertices, b, stride, 6u);
        vec2 uv2 = ReadFloat2(vertices, c, stride, 6u);
        uv = uv0 * weights.x + uv1 * weights.y + uv2 * weights.z;

        // 三角形の位置とUVからdP/du・dP/dvを求め、ラスタの余接フレームと同じ向きの基底を作る。
        vec2 deltaUv1 = uv1 - uv0;
        vec2 deltaUv2 = uv2 - uv0;
        float determinant = deltaUv1.x * deltaUv2.y - deltaUv2.x * deltaUv1.y;
        vec3 tangent;
        vec3 bitangent;
        if (abs(determinant) > 1.0e-12)
        {
            float inverseDeterminant = 1.0 / determinant;
            tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * inverseDeterminant;
            bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * inverseDeterminant;
        }
        else
        {
            vec3 up = abs(shadingNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            tangent = cross(up, shadingNormal);
            bitangent = cross(shadingNormal, tangent);
        }
        tangent -= shadingNormal * dot(shadingNormal, tangent);
        bitangent -= shadingNormal * dot(shadingNormal, bitangent);
        float tangentLength = length(tangent);
        float bitangentLength = length(bitangent);
        if (tangentLength > 1.0e-8 && bitangentLength > 1.0e-8)
        {
            tangentBasis = mat3(tangent / tangentLength, bitangent / bitangentLength, shadingNormal);
        }
        else
        {
            tangentBasis = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), shadingNormal);
        }
    }

    PbrMaterialTextureSamples samples = DecodePbrMaterialTextureSamples(
        SampleMaterialTexture(instance.textures.x, uv),
        SampleMaterialTexture(instance.textures.y, uv),
        SampleMaterialTexture(instance.textures.z, uv).r,
        SampleMaterialTexture(instance.textures.w, uv).r,
        1.0);
    if (bHasAttributes)
    {
        shadingNormal = ApplyTangentSpaceNormal(tangentBasis, samples.TangentNormal);
    }
    // シェーディング法線はレイ側（面法線と同じ側）へ向ける。
    if (dot(shadingNormal, geometricNormal) < 0.0)
    {
        shadingNormal = -shadingNormal;
    }

    payload.Position = gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
    payload.GeometricNormal = geometricNormal;
    payload.ShadingNormal = shadingNormal;
    payload.Albedo = ComposePbrSurfaceAlbedo(instance.objectColor.rgb, samples);
    payload.Metallic = clamp(samples.Material.x, 0.0, 1.0);
    payload.Roughness = clamp(samples.Material.y, 0.0, 1.0);
    payload.Emission = instance.emission.rgb * instance.emission.a;
    payload.InstanceIndex = instanceIndex;
    payload.PrimitiveIndex = uint(gl_PrimitiveID);
    payload.TriangleArea = 0.5 * normalLength;
    payload.Hit = 1u;
}
