// 影の頂点シェーダーでスキンの行列・位置・UV の読み出しを共有する。
#ifndef NORVES_SHADOW_SKINNING_GLSL
#define NORVES_SHADOW_SKINNING_GLSL

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

vec3 ShadowSkinPosition(vec3 inPosition)
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
    vec3 result = vec3(0.0);
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
        result += (positionMatrix * vec4(inPosition, 1.0)).xyz * weight;
        totalWeight += weight;
    }
    return totalWeight > 0.000001 ? result / totalWeight : inPosition;
}

// SkinnedMeshVertex の 64 バイトの並びで、UV は 6・7 番目のワードにある。
vec2 ShadowSkinTexCoord()
{
    uint wordOffset = uint(gl_VertexIndex) * 16u;
    return vec2(uintBitsToFloat(skinVertices.words[wordOffset + 6u]),
                uintBitsToFloat(skinVertices.words[wordOffset + 7u]));
}

#endif
