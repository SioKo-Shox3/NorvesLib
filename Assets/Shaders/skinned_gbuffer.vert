#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 emissiveChromaticityAndLuminanceNits;
    vec4 pomParams;
} mvp;

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

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragObjectColor;
layout(location = 3) out vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out vec3 fragViewDir;

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

void main()
{
    SkinnedVertex skinned = SkinVertex();
    vec4 worldPos = skinning.world * vec4(skinned.position, 1.0);
    fragWorldPos = worldPos.xyz;
    vec3 worldNormal = mat3(skinning.worldNormal) * skinned.normal;
    fragNormal = dot(worldNormal, worldNormal) > 0.000001 ? normalize(worldNormal) : vec3(0.0);
    fragObjectColor = vec3(1.0);
    fragEmissiveChromaticityAndLuminanceNits = mvp.emissiveChromaticityAndLuminanceNits;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);
    gl_Position = mvp.projection * mvp.view * worldPos;
}
