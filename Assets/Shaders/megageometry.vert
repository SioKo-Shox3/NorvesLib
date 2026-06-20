#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 world;
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
    vec4 emissiveColor;  // rgb=エミッシブカラー, a=エミッシブ強度
    vec4 pomParams;      // x=heightScale, y=hasHeightMap, z=debugMode, w=debugPayloadSupported
} mvp;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragObjectColor;
layout(location = 3) out vec4 fragEmissiveColor;
layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out vec3 fragViewDir;  // ワールド空間でのカメラ方向
layout(location = 6) flat out uint fragDebugPayload;

void main()
{
    vec4 worldPos = mvp.world * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(mvp.world);
    fragNormal = normalize(normalMatrix * inNormal);

    fragObjectColor = mvp.objectColor.rgb;
    fragEmissiveColor = mvp.emissiveColor;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);
    fragDebugPayload = gl_InstanceIndex;

    gl_Position = mvp.projection * mvp.view * worldPos;
}
