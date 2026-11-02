#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(std140, set = 0, binding = 0) uniform MeshParticleView
{
    mat4 viewProjection;
} view;

// std430 の配置: world=0、color=64、edgeColor=80、dissolve=96、要素間隔=112 バイト。
struct MeshParticleInstance
{
    mat4 world;
    vec4 color;
    vec4 edgeColor;
    vec4 dissolve; // x=閾値、y=縁の幅、zw=予約（0）
};

layout(std430, set = 0, binding = 1) readonly buffer MeshParticleInstances
{
    MeshParticleInstance instances[];
};

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out vec4 fragColor;
layout(location = 2) flat out vec4 fragEdgeColor;
layout(location = 3) flat out vec4 fragDissolve;

void main()
{
    vec4 worldPosition = instances[gl_InstanceIndex].world * vec4(inPosition, 1.0);
    gl_Position = view.viewProjection * worldPosition;
    fragTexCoord = inTexCoord;
    fragColor = instances[gl_InstanceIndex].color;
    fragEdgeColor = instances[gl_InstanceIndex].edgeColor;
    fragDissolve = instances[gl_InstanceIndex].dissolve;
}
