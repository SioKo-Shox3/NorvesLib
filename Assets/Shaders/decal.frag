#version 450

#include "Common/GBufferEncoding.glsl"
#include "Common/WetSurface.glsl"

// Normal は同じパスの色添付なので直接標本せず、描画前のコピーから受け手の印を読む。

layout(set = 0, binding = 0, std140) uniform DecalData
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 targetSize;
} decal;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormalCopy;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;
NORVES_DECLARE_ENVIRONMENT_UBO(0, 3)

layout(location = 0) flat in vec3 decalPosition;
layout(location = 1) flat in vec4 decalRotation;
layout(location = 2) flat in vec3 decalHalfExtents;
layout(location = 3) flat in vec4 decalColor;
layout(location = 4) flat in float decalNormalOpacity;
layout(location = 5) flat in vec3 decalLocalNormal;
layout(location = 6) flat in float decalEdgeFade;
layout(location = 7) flat in uint decalFlags;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

vec3 rotateQuaternion(vec4 rotation, vec3 value)
{
    return value + 2.0 * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / decal.targetSize.xy;
    float depth = texelFetch(gbufferDepth, pixel, 0).r;
    vec4 world = decal.inverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPosition = world.xyz / world.w;
    vec4 inverseRotation = vec4(-decalRotation.xyz, decalRotation.w);
    vec3 local = rotateQuaternion(inverseRotation, worldPosition - decalPosition) / decalHalfExtents;
    if (!all(lessThanEqual(abs(local), vec3(1.0))))
    {
        discard;
    }
    if ((decalFlags & 1u) == 0u &&
        GBufferHasReceiverFlag(texelFetch(gbufferNormalCopy, pixel, 0), GBUFFER_RECEIVER_FLAG_FOLIAGE_CLUMP))
    {
        discard;
    }
    float fade = 1.0;
    if (decalEdgeFade > 0.0)
    {
        vec3 axisFade = clamp((vec3(1.0) - abs(local)) / decalEdgeFade, vec3(0.0), vec3(1.0));
        fade = axisFade.x * axisFade.y * axisFade.z;
    }
    vec3 albedo = decalColor.rgb;
    float roughness = 1.0;
    float metallic = 0.0;
    vec3 n = normalize(rotateQuaternion(decalRotation, decalLocalNormal));
    // 幾何法線を 0 にして水膜を抑え、粗さと法線を書かず暗化だけを使う。
    // デカールに材質の P0 がないため感度は 1。受け手の P0 = 0 では跡だけ暗くなる。
    ApplyWetSurfaceFromEnvironment(albedo, roughness, metallic, n, vec3(0.0), u_Environment.params,
                                   ResolveWetSurfaceResponse(-1.0, -1.0, true));
    outAlbedo = vec4(albedo, decalColor.a * fade);
    outNormal = GBufferEncodeDecalNormalBlend(normalize(rotateQuaternion(decalRotation, decalLocalNormal)), decalNormalOpacity * fade);
}
