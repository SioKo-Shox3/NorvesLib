#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform ShadowMVP
{
    mat4 lightView;
    mat4 lightProjection;
} shadowMVP;

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

vec3 SkinPosition()
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

void main()
{
    vec4 worldPosition = skinning.world * vec4(SkinPosition(), 1.0);
    gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * worldPosition;
}
