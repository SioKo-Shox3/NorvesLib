#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 world;
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
} mvp;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragObjectColor;

void main()
{
    vec4 worldPos = mvp.world * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(mvp.world);
    fragNormal = normalize(normalMatrix * inNormal);

    fragObjectColor = mvp.objectColor.rgb;

    gl_Position = mvp.projection * mvp.view * worldPos;
}
