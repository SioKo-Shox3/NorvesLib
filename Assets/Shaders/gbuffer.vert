#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPosition;
    vec4 emissiveChromaticityAndLuminanceNits;
    vec4 pomParams;      // x=heightScale, y=hasHeightMap, z=unused, w=unused
    vec4 velocityParams;  // x=前フレームカメラ履歴の有効フラグ
} mvp;

struct InstanceData
{
    mat4 world;
    mat4 previousWorld;
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
layout(location = 2) out vec3 fragObjectColor;
layout(location = 3) out vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out vec3 fragViewDir;  // ワールド空間でのカメラ方向
layout(location = 6) out vec4 fragCurrentClip;
layout(location = 7) out vec4 fragPreviousClip;

void main()
{
    vec4 worldPos = instances[gl_InstanceIndex].world * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    vec3 normal = instances[gl_InstanceIndex].normalRows[0].xyz * inNormal.x +
                  instances[gl_InstanceIndex].normalRows[1].xyz * inNormal.y +
                  instances[gl_InstanceIndex].normalRows[2].xyz * inNormal.z;
    fragNormal = normalize(normal);

    fragObjectColor = instances[gl_InstanceIndex].objectColor.rgb;
    fragEmissiveChromaticityAndLuminanceNits = mvp.emissiveChromaticityAndLuminanceNits;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);

    fragCurrentClip = mvp.projection * mvp.view * worldPos;
    fragPreviousClip = mvp.previousProjection * mvp.previousView *
                       instances[gl_InstanceIndex].previousWorld * vec4(inPosition, 1.0);
    gl_Position = fragCurrentClip;
}
