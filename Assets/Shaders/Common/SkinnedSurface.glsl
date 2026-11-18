// スキンメッシュの表面標本を読み、今と前のパレットで位置・法線を求める部品。
// 取り込む側は GL_EXT_buffer_reference・GL_EXT_buffer_reference_uvec2 を先に有効にし、
// mat4 SkinnedSurfaceMatrix(uint index) を先に定義して、パレットの行列を返すこと。
#ifndef NORVES_SKINNED_SURFACE_GLSL
#define NORVES_SKINNED_SURFACE_GLSL

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer SkinnedSurfaceVertexWords
{
    uint words[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer SkinnedSurfaceSampleRecords
{
    uvec4 records[];
};

// デバイスアドレスから、16 バイトの表面標本を読む。
uvec4 SkinnedSurfaceLoadSample(uvec2 sampleAddress, uint index)
{
    SkinnedSurfaceSampleRecords samples = SkinnedSurfaceSampleRecords(sampleAddress);
    return samples.records[index];
}

// 詰めた 21 ビットの頂点番号と、unorm16 の重心座標を復元する。
void SkinnedSurfaceUnpackSample(uvec4 record, out uvec3 vertices, out vec3 barycentrics)
{
    vertices = uvec3(record.x & 0x1FFFFFu,
                     (record.x >> 21u) | ((record.y & 0x3FFu) << 11u),
                     (record.y >> 10u) & 0x1FFFFFu);
    vec2 encodedBarycentrics = vec2(record.z & 0xFFFFu, record.z >> 16u) / 65535.0;
    barycentrics = vec3(1.0 - encodedBarycentrics.x - encodedBarycentrics.y,
                        encodedBarycentrics.x, encodedBarycentrics.y);
}

// RGBA8 を展開する。RGB は sRGB の符号化を保つ。
vec4 SkinnedSurfaceSampleAlbedo(uvec4 record)
{
    return unpackUnorm4x8(record.w);
}

struct SkinnedSurfaceVertex
{
    vec3 position;
    vec3 normal;
    uvec4 joints;
    vec4 weights;
};

// 64 バイトの頂点から、バインド姿勢の位置・法線と骨の影響を読む。
SkinnedSurfaceVertex SkinnedSurfaceLoadVertex(uvec2 vertexAddress, uint vertexIndex)
{
    SkinnedSurfaceVertexWords vertices = SkinnedSurfaceVertexWords(vertexAddress);
    uint base = vertexIndex * 16u;
    SkinnedSurfaceVertex vertex;
    vertex.position = uintBitsToFloat(uvec3(vertices.words[base],
        vertices.words[base + 1u], vertices.words[base + 2u]));
    vertex.normal = uintBitsToFloat(uvec3(vertices.words[base + 3u],
        vertices.words[base + 4u], vertices.words[base + 5u]));
    vertex.joints = uvec4(vertices.words[base + 8u], vertices.words[base + 9u],
        vertices.words[base + 10u], vertices.words[base + 11u]);
    vertex.weights = uintBitsToFloat(uvec4(vertices.words[base + 12u],
        vertices.words[base + 13u], vertices.words[base + 14u], vertices.words[base + 15u]));
    return vertex;
}

// 行列の全 16 成分に NaN・無限大が無いことを確かめる。
bool SkinnedSurfaceIsFiniteMatrix(mat4 m)
{
    for (uint column = 0u; column < 4u; ++column)
    {
        if (any(isnan(m[column])) || any(isinf(m[column])))
        {
            return false;
        }
    }
    return true;
}

// CPU の SkinPosition と同じ除外条件・重みの合計・閾値で位置を求める。
vec3 SkinnedSurfaceSkinPosition(vec3 position, uvec4 joints, vec4 weights,
    uint firstMatrix, uint matrixStride, uint boneCount)
{
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    for (uint influence = 0u; influence < 4u; ++influence)
    {
        float weight = weights[influence];
        uint joint = joints[influence];
        if (isnan(weight) || isinf(weight) || weight <= 0.0 || joint >= boneCount)
        {
            continue;
        }
        mat4 m = SkinnedSurfaceMatrix(firstMatrix + joint * matrixStride);
        if (!SkinnedSurfaceIsFiniteMatrix(m))
        {
            continue;
        }
        result += (m * vec4(position, 1.0)).xyz * weight;
        totalWeight += weight;
    }
    return totalWeight <= 1.192092896e-07 ? position : result / totalWeight;
}

// 位置と同じ条件で無効な影響を除外し、法線を合成して長さが正なら正規化する。
// 有効な重みの合計が閾値以下なら、元の法線を使う。
vec3 SkinnedSurfaceSkinNormal(vec3 normal, uvec4 joints, vec4 weights,
    uint firstMatrix, uint matrixStride, uint boneCount)
{
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    for (uint influence = 0u; influence < 4u; ++influence)
    {
        float weight = weights[influence];
        uint joint = joints[influence];
        if (isnan(weight) || isinf(weight) || weight <= 0.0 || joint >= boneCount)
        {
            continue;
        }
        mat4 m = SkinnedSurfaceMatrix(firstMatrix + joint * matrixStride);
        if (!SkinnedSurfaceIsFiniteMatrix(m))
        {
            continue;
        }
        result += (mat3(m) * normal) * weight;
        totalWeight += weight;
    }
    result = totalWeight <= 1.192092896e-07 ? normal : result / totalWeight;
    return dot(result, result) > 0.0 ? normalize(result) : result;
}

struct SkinnedSurfacePoint
{
    vec3 restPosition;
    vec3 position;
    vec3 previousPosition;
    vec3 normal;
    uvec4 joints[3];
    vec4 weights[3];
};

// 3 頂点をそれぞれスキニングしてから補間し、今と前のワールド空間へ移す。
// 頂点番号が範囲外なら false を返し、出力は使わない。
bool SkinnedSurfaceEvaluatePoint(uvec2 vertexAddress, uint vertexCount,
    uvec3 vertices, vec3 barycentrics, uint paletteOffset, uint previousPaletteOffset,
    uint boneCount, out SkinnedSurfacePoint point)
{
    if (any(greaterThanEqual(vertices, uvec3(vertexCount))))
    {
        return false;
    }

    point.restPosition = vec3(0.0);
    vec3 localPosition = vec3(0.0);
    vec3 previousLocalPosition = vec3(0.0);
    vec3 localNormal = vec3(0.0);
    for (uint corner = 0u; corner < 3u; ++corner)
    {
        SkinnedSurfaceVertex vertex = SkinnedSurfaceLoadVertex(vertexAddress, vertices[corner]);
        float barycentric = barycentrics[corner];
        point.restPosition += vertex.position * barycentric;
        localPosition += SkinnedSurfaceSkinPosition(vertex.position, vertex.joints, vertex.weights,
            paletteOffset + 2u, 2u, boneCount) * barycentric;
        previousLocalPosition += SkinnedSurfaceSkinPosition(vertex.position, vertex.joints, vertex.weights,
            previousPaletteOffset + 1u, 1u, boneCount) * barycentric;
        localNormal += SkinnedSurfaceSkinNormal(vertex.normal, vertex.joints, vertex.weights,
            paletteOffset + 3u, 2u, boneCount) * barycentric;
        point.joints[corner] = vertex.joints;
        point.weights[corner] = vertex.weights;
    }

    point.position = (SkinnedSurfaceMatrix(paletteOffset) * vec4(localPosition, 1.0)).xyz;
    point.previousPosition = (SkinnedSurfaceMatrix(previousPaletteOffset) * vec4(previousLocalPosition, 1.0)).xyz;
    vec3 worldNormal = mat3(SkinnedSurfaceMatrix(paletteOffset + 1u)) * localNormal;
    point.normal = dot(worldNormal, worldNormal) > 0.0 ? normalize(worldNormal) : vec3(0.0);
    return true;
}

// 指定した位置の行列で点を変換する。骨の変換にもワールド変換にも使える。
vec3 SkinnedSurfaceTransformPoint(uint matrixIndex, vec3 p)
{
    return (SkinnedSurfaceMatrix(matrixIndex) * vec4(p, 1.0)).xyz;
}

#endif // NORVES_SKINNED_SURFACE_GLSL
