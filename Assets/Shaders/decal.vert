#version 450

layout(set = 0, binding = 0, std140) uniform DecalData
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 targetSize;
} decal;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inRotation;
layout(location = 2) in vec3 inHalfExtents;
layout(location = 3) in vec4 inColor;
layout(location = 4) in float inNormalOpacity;
layout(location = 5) in vec3 inLocalNormal;
layout(location = 6) in float inEdgeFade;
layout(location = 7) in uint inFlags;

layout(location = 0) flat out vec3 decalPosition;
layout(location = 1) flat out vec4 decalRotation;
layout(location = 2) flat out vec3 decalHalfExtents;
layout(location = 3) flat out vec4 decalColor;
layout(location = 4) flat out float decalNormalOpacity;
layout(location = 5) flat out vec3 decalLocalNormal;
layout(location = 6) flat out float decalEdgeFade;
layout(location = 7) flat out uint decalFlags;

// 外向きの頂点順を持つ箱。頂点バッファを使わずインデックスから作る
const vec3 cubeVertices[36] = vec3[36](
    vec3( 1,-1,-1), vec3( 1, 1,-1), vec3( 1, 1, 1), vec3( 1,-1,-1), vec3( 1, 1, 1), vec3( 1,-1, 1),
    vec3(-1,-1, 1), vec3(-1, 1, 1), vec3(-1, 1,-1), vec3(-1,-1, 1), vec3(-1, 1,-1), vec3(-1,-1,-1),
    vec3(-1, 1,-1), vec3(-1, 1, 1), vec3( 1, 1, 1), vec3(-1, 1,-1), vec3( 1, 1, 1), vec3( 1, 1,-1),
    vec3(-1,-1, 1), vec3(-1,-1,-1), vec3( 1,-1,-1), vec3(-1,-1, 1), vec3( 1,-1,-1), vec3( 1,-1, 1),
    vec3( 1,-1, 1), vec3( 1, 1, 1), vec3(-1, 1, 1), vec3( 1,-1, 1), vec3(-1, 1, 1), vec3(-1,-1, 1),
    vec3(-1,-1,-1), vec3(-1, 1,-1), vec3( 1, 1,-1), vec3(-1,-1,-1), vec3( 1, 1,-1), vec3( 1,-1,-1)
);

vec3 rotateQuaternion(vec4 rotation, vec3 value)
{
    return value + 2.0 * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

void main()
{
    vec3 worldPosition = inPosition + rotateQuaternion(inRotation, cubeVertices[gl_VertexIndex] * inHalfExtents);
    gl_Position = decal.viewProjection * vec4(worldPosition, 1.0);
    decalPosition = inPosition;
    decalRotation = inRotation;
    decalHalfExtents = inHalfExtents;
    decalColor = inColor;
    decalNormalOpacity = inNormalOpacity;
    decalLocalNormal = inLocalNormal;
    decalEdgeFade = inEdgeFade;
    decalFlags = inFlags;
}
