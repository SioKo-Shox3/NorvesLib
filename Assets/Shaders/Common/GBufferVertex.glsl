// 静的メッシュの GBuffer 頂点処理を共有し、必要な材質だけディゾルブの値を渡す。
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
    vec4 pomParams;      // x=heightScale, y=hasHeightMap, z=ORMの1枚を張ったか（フラグメント側で使用）, w=法線が2チャンネルか（同）
    vec4 frameParams;     // x=前フレームカメラ履歴の有効フラグ, y=発光に掛けるプリエクスポージャ
    vec4 vtFeedbackParams;
    vec4 shadingParams;
    vec4 shadingModel;
    vec4 vertexAnimInstanceParams;
    vec4 vertexAnimTime;
    vec4 specularAAParams;
    vec4 vertexAnimMaterial0;
    vec4 vertexAnimMaterial1;
#ifdef GBUFFER_DISSOLVE
    vec4 dissolveEdge;
    vec4 dissolveSweep;
    vec4 dissolveParams;
    uvec4 dissolveProtectBones;
#endif
} mvp;

layout(set = 0, binding = 12) uniform VertexAnimationWindFrame
{
    vec4 windA;
    vec4 windB;
} vertexAnimWind;

#include "Common/SceneInstanceData.glsl"
#include "Common/VertexAnimation.glsl"

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
layout(location = 8) flat out vec4 fragInstanceShadingParams;
#ifdef GBUFFER_DISSOLVE
#include "Common/Dissolve.glsl"
layout(location = 9) out vec3 fragRestPosition;
layout(location = 10) out float fragDissolveProtect;
#endif

void main()
{
#ifdef GBUFFER_DISSOLVE
    fragRestPosition = inPosition;
    fragDissolveProtect = 0.0;
#endif
    vec4 params = VertexAnimationParamsForFrame(instances[gl_InstanceIndex].shadingParams, mvp.vertexAnimTime);
    VertexAnimationMaterial material = MakeVertexAnimationMaterial(mvp.vertexAnimMaterial0, mvp.vertexAnimMaterial1);
    VertexAnimationWind wind = MakeVertexAnimationWind(vertexAnimWind.windA, vertexAnimWind.windB);
    vec3 origin = instances[gl_InstanceIndex].world[3].xyz;
    vec4 worldPos = instances[gl_InstanceIndex].world * vec4(inPosition, 1.0);
    worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, inPosition, params, material, wind, origin, mvp.vertexAnimTime.x);
    fragWorldPos = worldPos.xyz;

    vec3 normal = instances[gl_InstanceIndex].normalRows[0].xyz * inNormal.x +
                  instances[gl_InstanceIndex].normalRows[1].xyz * inNormal.y +
                  instances[gl_InstanceIndex].normalRows[2].xyz * inNormal.z;
    fragNormal = normalize(normal);

    fragObjectColor = instances[gl_InstanceIndex].objectColor.rgb;
    fragInstanceShadingParams = instances[gl_InstanceIndex].shadingParams;
    fragEmissiveChromaticityAndLuminanceNits = mvp.emissiveChromaticityAndLuminanceNits;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);

    fragCurrentClip = mvp.projection * mvp.view * worldPos;
    vec4 previousWorldPos = instances[gl_InstanceIndex].previousWorld * vec4(inPosition, 1.0);
    previousWorldPos.xyz = ApplyVertexAnimation(previousWorldPos.xyz, inPosition, params, material, wind, origin, mvp.vertexAnimTime.y);
    if (VertexAnimationIsIdentity(params, material, wind))
    {
        // 恒等のときは従来の行列の乗算順を保ち、丸めの差を出さない。
        fragPreviousClip = mvp.previousProjection * mvp.previousView *
                           instances[gl_InstanceIndex].previousWorld * vec4(inPosition, 1.0);
    }
    else
    {
        fragPreviousClip = mvp.previousProjection * mvp.previousView * previousWorldPos;
    }
    gl_Position = fragCurrentClip;
}
