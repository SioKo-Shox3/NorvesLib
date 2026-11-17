// インポスターの各セルに、指定された向きからメッシュを正射影する。
// オブジェクト空間の位置と法線を材質のベイクへ渡す。
#version 450
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform ImpostorBakeParams
{
    vec4 boundsCenterRadius;
    vec4 baseColor;
    vec4 materialFlags;
    vec4 bakeParams;
} bake;

struct ImpostorBakeCell
{
    vec4 right;
    vec4 up;
    vec4 viewDirection;
};
layout(std430, set = 0, binding = 6) readonly buffer ImpostorBakeCells
{
    ImpostorBakeCell cells[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragObjectPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) flat out vec3 fragViewDirection;

void main()
{
    ImpostorBakeCell cell = cells[gl_InstanceIndex];
    vec3 rel = inPosition - bake.boundsCenterRadius.xyz;
    float radius = bake.boundsCenterRadius.w;
    float x = dot(rel, cell.right.xyz) / radius;
    float y = dot(rel, cell.up.xyz) / radius;
    float depth01 = (radius - dot(rel, cell.viewDirection.xyz)) / (2.0 * radius);

    // セルの先頭行を物体の上に合わせる。
    gl_Position = vec4(x, -y, depth01, 1.0);
    fragObjectPos = inPosition;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;
    fragViewDirection = cell.viewDirection.xyz;
}
