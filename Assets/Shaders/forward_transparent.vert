#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 emissiveColor;
    vec4 pomParams;
} mvp;

struct InstanceData
{
    mat4 world;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragObjectColor;

void main()
{
    vec4 worldPos = instances[gl_InstanceIndex].world * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    vec3 normal = instances[gl_InstanceIndex].normalRows[0].xyz * inNormal.x +
                  instances[gl_InstanceIndex].normalRows[1].xyz * inNormal.y +
                  instances[gl_InstanceIndex].normalRows[2].xyz * inNormal.z;
    fragNormal = normalize(normal);

    fragTexCoord = inTexCoord;
    fragObjectColor = instances[gl_InstanceIndex].objectColor;

    gl_Position = mvp.projection * mvp.view * worldPos;
}
