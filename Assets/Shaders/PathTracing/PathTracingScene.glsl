// FramePacketから値コピーしたinstanceの表と頂点・索引の読み取り。命中段とレイ生成段が共有する。
// 取り込む側でGL_EXT_buffer_reference2とGL_EXT_shader_explicit_arithmetic_types_int64を有効にする。

struct PathInstance
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    vec4 baseColor;
    vec4 emission; // rgb=発光色、a=nits
    uvec4 geometry; // x=頂点幅、y=頂点数、z=索引数、w=インスタンス番号
    vec4 objectColor; // GBufferと同じ規則のinstance色
    uvec4 textures; // x=アルベド、y=法線、z=metallic、w=roughnessのtexture配列番号
    vec4 objectToWorld[3]; // TLASと同じ物体→ワールド変換（行優先3x4）
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

vec3 ReadFloat3(VertexData vertices, uint index, uint stride, uint component)
{
    uint offset = index * stride + component;
    return vec3(vertices.values[offset], vertices.values[offset + 1u],
                vertices.values[offset + 2u]);
}

vec2 ReadFloat2(VertexData vertices, uint index, uint stride, uint component)
{
    uint offset = index * stride + component;
    return vec2(vertices.values[offset], vertices.values[offset + 1u]);
}

// 三角形の3頂点の索引を読む。範囲外なら偽。
bool ReadTriangleIndices(PathInstance instance, uint primitive, out uvec3 outIndices)
{
    outIndices = uvec3(0u);
    uint firstIndex = primitive * 3u;
    if (instance.geometry.x < 12u || firstIndex + 2u >= instance.geometry.z)
    {
        return false;
    }
    IndexData indices = IndexData(instance.indexAddress);
    outIndices = uvec3(indices.values[firstIndex], indices.values[firstIndex + 1u],
                       indices.values[firstIndex + 2u]);
    return all(lessThan(outIndices, uvec3(instance.geometry.y)));
}

vec3 TransformInstancePoint(PathInstance instance, vec3 point)
{
    vec4 homogeneous = vec4(point, 1.0);
    return vec3(dot(instance.objectToWorld[0], homogeneous),
                dot(instance.objectToWorld[1], homogeneous),
                dot(instance.objectToWorld[2], homogeneous));
}

// 発光三角形の光源標本用に、ワールド空間の3頂点を読む。
bool ReadWorldTriangle(PathInstance instance, uint primitive,
                       out vec3 p0, out vec3 p1, out vec3 p2)
{
    p0 = vec3(0.0);
    p1 = vec3(0.0);
    p2 = vec3(0.0);
    uvec3 triangleIndices;
    if (!ReadTriangleIndices(instance, primitive, triangleIndices))
    {
        return false;
    }
    VertexData vertices = VertexData(instance.vertexAddress);
    uint stride = instance.geometry.x / 4u;
    p0 = TransformInstancePoint(instance, ReadFloat3(vertices, triangleIndices.x, stride, 0u));
    p1 = TransformInstancePoint(instance, ReadFloat3(vertices, triangleIndices.y, stride, 0u));
    p2 = TransformInstancePoint(instance, ReadFloat3(vertices, triangleIndices.z, stride, 0u));
    return true;
}
