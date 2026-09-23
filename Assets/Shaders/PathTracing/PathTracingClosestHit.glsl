// FramePacketに値コピーされた形状と材質から交差を評価する。
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 1, std140) uniform PathTracingParameters
{
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    uvec4 imageState;
} parameters;

struct PathInstance
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    vec4 baseColor;
    vec4 emission;
    uvec4 geometry; // x=頂点幅、y=頂点数、z=索引数、w=インスタンス番号
};
layout(set = 0, binding = 2, std430) readonly buffer PathInstances
{
    PathInstance values[];
} instances;
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexData
{
    float values[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer IndexData
{
    uint values[];
};

struct PathPayload
{
    vec3 Position;
    vec3 Normal;
    vec3 BaseColor;
    vec3 Emission;
    uint Hit;
};
layout(location = 0) rayPayloadInEXT PathPayload payload;

vec3 ReadPosition(VertexData vertices, uint index, uint stride)
{
    uint offset = index * stride;
    return vec3(vertices.values[offset], vertices.values[offset + 1u],
                vertices.values[offset + 2u]);
}

void main()
{
    uint instanceIndex = gl_InstanceCustomIndexEXT;
    if (instanceIndex >= parameters.imageState.w)
    {
        payload.Hit = 0u;
        return;
    }
    PathInstance instance = instances.values[instanceIndex];
    uint firstIndex = uint(gl_PrimitiveID) * 3u;
    if (instance.geometry.x < 12u || firstIndex + 2u >= instance.geometry.z)
    {
        payload.Hit = 0u;
        return;
    }
    IndexData indices = IndexData(instance.indexAddress);
    uint a = indices.values[firstIndex];
    uint b = indices.values[firstIndex + 1u];
    uint c = indices.values[firstIndex + 2u];
    if (a >= instance.geometry.y || b >= instance.geometry.y || c >= instance.geometry.y)
    {
        payload.Hit = 0u;
        return;
    }
    VertexData vertices = VertexData(instance.vertexAddress);
    uint stride = instance.geometry.x / 4u;
    vec3 p0 = gl_ObjectToWorldEXT * vec4(ReadPosition(vertices, a, stride), 1.0);
    vec3 p1 = gl_ObjectToWorldEXT * vec4(ReadPosition(vertices, b, stride), 1.0);
    vec3 p2 = gl_ObjectToWorldEXT * vec4(ReadPosition(vertices, c, stride), 1.0);
    vec3 geometricNormal = cross(p1 - p0, p2 - p0);
    float normalLengthSquared = dot(geometricNormal, geometricNormal);
    if (normalLengthSquared < 0.000000000001)
    {
        payload.Hit = 0u;
        return;
    }
    vec3 normal = geometricNormal * inversesqrt(normalLengthSquared);
    if (dot(normal, gl_WorldRayDirectionEXT) > 0.0)
    {
        normal = -normal;
    }
    payload.Position = gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
    payload.Normal = normal;
    payload.BaseColor = instance.baseColor.rgb;
    payload.Emission = instance.emission.rgb * instance.emission.a;
    payload.Hit = 1u;
}
