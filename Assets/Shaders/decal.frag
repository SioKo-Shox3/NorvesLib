#version 450

layout(set = 0, binding = 0, std140) uniform DecalData
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 targetSize;
} decal;
layout(set = 0, binding = 1) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;

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
    float material = texelFetch(gbufferMaterial, pixel, 0).a;
    if ((decalFlags & 1u) == 0u && material > 0.125 && material < 0.375)
    {
        discard;
    }
    float fade = 1.0;
    if (decalEdgeFade > 0.0)
    {
        vec3 axisFade = clamp((vec3(1.0) - abs(local)) / decalEdgeFade, vec3(0.0), vec3(1.0));
        fade = axisFade.x * axisFade.y * axisFade.z;
    }
    outAlbedo = vec4(decalColor.rgb, decalColor.a * fade);
    outNormal = vec4(normalize(rotateQuaternion(decalRotation, decalLocalNormal)), decalNormalOpacity * fade);
}
