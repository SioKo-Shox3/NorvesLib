// プログラムの VS（ファーのシェルなど）が include し、同じ binding のスキンのデータを読むための部品。
// 入力の inPosition・inNormal を使う。NORVES_SKINNED_VERTEX_BUFFERS_ONLY を定義すると SSBO だけを先に取り込める。
#ifndef NORVES_SKINNED_VERTEX_BUFFERS_GLSL
#define NORVES_SKINNED_VERTEX_BUFFERS_GLSL

layout(std430, set = 0, binding = 8) readonly buffer SkinningMatrices
{
    mat4 world;
    mat4 worldNormal;
    mat4 paletteMatrices[];
} skinning;

layout(std430, set = 0, binding = 9) readonly buffer SkinVertexWords
{
    uint words[];
} skinVertices;

// 直前のフレームの変換と骨ごとの位置の行列（velocity 用、骨の数は paletteMatrices と同じ）
layout(std430, set = 0, binding = 10) readonly buffer PreviousSkinningMatrices
{
    mat4 world;
    mat4 paletteMatrices[];
} previousSkinning;

#endif

#if !defined(NORVES_SKINNED_VERTEX_BUFFERS_ONLY) && !defined(NORVES_SKINNED_VERTEX_FUNCTIONS_GLSL)
#define NORVES_SKINNED_VERTEX_FUNCTIONS_GLSL

struct SkinnedVertex
{
    vec3 position;
    vec3 normal;
};

SkinnedVertex SkinVertex()
{
    uint wordOffset = uint(gl_VertexIndex) * 16u;
    uvec4 boneIndices = uvec4(skinVertices.words[wordOffset + 8u],
                              skinVertices.words[wordOffset + 9u],
                              skinVertices.words[wordOffset + 10u],
                              skinVertices.words[wordOffset + 11u]);
    vec4 boneWeights = vec4(uintBitsToFloat(skinVertices.words[wordOffset + 12u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 13u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 14u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 15u]));
    SkinnedVertex result;
    result.position = vec3(0.0);
    result.normal = vec3(0.0);
    float totalWeight = 0.0;
    uint boneCount = uint(skinning.paletteMatrices.length()) / 2u;
    for (uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
    {
        uint boneIndex = boneIndices[influenceIndex];
        float weight = boneWeights[influenceIndex];
        if (weight <= 0.0 || boneIndex >= boneCount)
        {
            continue;
        }
        mat4 positionMatrix = skinning.paletteMatrices[boneIndex * 2u];
        mat4 normalMatrix = skinning.paletteMatrices[boneIndex * 2u + 1u];
        result.position += (positionMatrix * vec4(inPosition, 1.0)).xyz * weight;
        result.normal += (mat3(normalMatrix) * inNormal) * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.000001)
    {
        result.position = inPosition;
        result.normal = inNormal;
        return result;
    }

    result.position /= totalWeight;
    result.normal /= totalWeight;
    if (dot(result.normal, result.normal) > 0.000001)
    {
        result.normal = normalize(result.normal);
    }
    return result;
}

// SkinVertex と同じ重みで、直前のフレームの骨の行列から頂点の位置を求める。
vec3 SkinPreviousPosition()
{
    uint wordOffset = uint(gl_VertexIndex) * 16u;
    uvec4 boneIndices = uvec4(skinVertices.words[wordOffset + 8u],
                              skinVertices.words[wordOffset + 9u],
                              skinVertices.words[wordOffset + 10u],
                              skinVertices.words[wordOffset + 11u]);
    vec4 boneWeights = vec4(uintBitsToFloat(skinVertices.words[wordOffset + 12u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 13u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 14u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 15u]));
    vec3 position = vec3(0.0);
    float totalWeight = 0.0;
    uint boneCount = uint(previousSkinning.paletteMatrices.length());
    for (uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
    {
        uint boneIndex = boneIndices[influenceIndex];
        float weight = boneWeights[influenceIndex];
        if (weight <= 0.0 || boneIndex >= boneCount)
        {
            continue;
        }
        position += (previousSkinning.paletteMatrices[boneIndex] * vec4(inPosition, 1.0)).xyz * weight;
        totalWeight += weight;
    }
    return totalWeight <= 0.000001 ? inPosition : position / totalWeight;
}

#endif
