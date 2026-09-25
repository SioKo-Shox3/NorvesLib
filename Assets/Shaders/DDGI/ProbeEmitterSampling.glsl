// DDGIのprobeとprobe rayのhit面で共有する、発光面（発光するinstanceの三角形）の直接照度の見積もり。
// 取り込む側で ProbeRayInstanceData・ProbeRayVertexData・ProbeRayIndexData・instances・sceneTopLevel・
// parameters.rayLimits・Pi を定義しておく。

bool TryGetLocalTriangle(ProbeRayInstanceData instanceData,
                         uint primitiveIndex,
                         out vec3 vertex0,
                         out vec3 vertex1,
                         out vec3 vertex2,
                         out vec3 localNormal,
                         out bool bHasLocalNormal)
{
    localNormal = vec3(0.0);
    bHasLocalNormal = false;
    uint vertexStrideBytes = instanceData.geometry.x;
    uint vertexCount = instanceData.geometry.y;
    uint indexCount = instanceData.geometry.z;
    if (vertexStrideBytes < 12u || (vertexStrideBytes & 3u) != 0u ||
        primitiveIndex >= indexCount / 3u)
    {
        return false;
    }

    ProbeRayIndexData indexBuffer = ProbeRayIndexData(instanceData.indexAddress);
    uint primitiveOffset = primitiveIndex * 3u;
    uvec3 vertexIndices = uvec3(indexBuffer.values[primitiveOffset],
                                indexBuffer.values[primitiveOffset + 1u],
                                indexBuffer.values[primitiveOffset + 2u]);
    if (vertexIndices.x >= vertexCount || vertexIndices.y >= vertexCount ||
        vertexIndices.z >= vertexCount)
    {
        return false;
    }

    ProbeRayVertexData vertexBuffer = ProbeRayVertexData(instanceData.vertexAddress);
    uint stride = vertexStrideBytes / 4u;
    uint vertex0Offset = vertexIndices.x * stride;
    uint vertex1Offset = vertexIndices.y * stride;
    uint vertex2Offset = vertexIndices.z * stride;
    vertex0 = vec3(vertexBuffer.values[vertex0Offset],
                   vertexBuffer.values[vertex0Offset + 1u],
                   vertexBuffer.values[vertex0Offset + 2u]);
    vertex1 = vec3(vertexBuffer.values[vertex1Offset],
                   vertexBuffer.values[vertex1Offset + 1u],
                   vertexBuffer.values[vertex1Offset + 2u]);
    vertex2 = vec3(vertexBuffer.values[vertex2Offset],
                   vertexBuffer.values[vertex2Offset + 1u],
                   vertexBuffer.values[vertex2Offset + 2u]);
    const bool bFinitePositions = !any(isnan(vertex0)) && !any(isinf(vertex0)) &&
                                  !any(isnan(vertex1)) && !any(isinf(vertex1)) &&
                                  !any(isnan(vertex2)) && !any(isinf(vertex2));
    if (!bFinitePositions)
    {
        return false;
    }

    if (vertexStrideBytes >= 24u)
    {
        vec3 normal0 = vec3(vertexBuffer.values[vertex0Offset + 3u],
                            vertexBuffer.values[vertex0Offset + 4u],
                            vertexBuffer.values[vertex0Offset + 5u]);
        vec3 normal1 = vec3(vertexBuffer.values[vertex1Offset + 3u],
                            vertexBuffer.values[vertex1Offset + 4u],
                            vertexBuffer.values[vertex1Offset + 5u]);
        vec3 normal2 = vec3(vertexBuffer.values[vertex2Offset + 3u],
                            vertexBuffer.values[vertex2Offset + 4u],
                            vertexBuffer.values[vertex2Offset + 5u]);
        vec3 normalSum = normal0 + normal1 + normal2;
        float normalLengthSquared = dot(normalSum, normalSum);
        if (!any(isnan(normalSum)) && !any(isinf(normalSum)) &&
            normalLengthSquared > 1.0e-12)
        {
            localNormal = normalize(normalSum);
            bHasLocalNormal = true;
        }
    }
    return true;
}

vec3 TransformInstancePoint(ProbeRayInstanceData instanceData, vec3 point)
{
    return vec3(dot(instanceData.transform0.xyz, point) + instanceData.transform0.w,
                dot(instanceData.transform1.xyz, point) + instanceData.transform1.w,
                dot(instanceData.transform2.xyz, point) + instanceData.transform2.w);
}

// 発光面の影のrayを発光面の手前で止める距離。
const float EmitterShadowEndOffset = 0.004;

// 発光面の小片（位置samplePosition・面積sampleArea）から、receiverPositionで向きnormalの面が受ける照度。
// 可視はshadowOriginからの影のrayで確かめる。
vec3 EvaluateEmitterSample(vec3 receiverPosition,
                           vec3 shadowOrigin,
                           vec3 normal,
                           vec3 samplePosition,
                           vec3 sourceNormal,
                           vec3 sourceRadiance,
                           float sampleArea)
{
    vec3 toSource = samplePosition - receiverPosition;
    float distanceSquared = dot(toSource, toSource);
    if (isnan(distanceSquared) || isinf(distanceSquared) || distanceSquared <= 1.0e-6)
    {
        return vec3(0.0);
    }
    float distanceToSource = sqrt(distanceSquared);
    vec3 lightDirection = toSource / distanceToSource;
    float receiverCosine = max(dot(normal, lightDirection), 0.0);
    float sourceCosine = max(dot(sourceNormal, -lightDirection), 0.0);
    if (receiverCosine <= 0.0 || sourceCosine <= 0.0)
    {
        return vec3(0.0);
    }

    float shadowDistance = max(distanceToSource - EmitterShadowEndOffset, 0.0);
    if (shadowDistance > parameters.rayLimits.x)
    {
        rayQueryEXT shadowQuery;
        rayQueryInitializeEXT(shadowQuery,
                              sceneTopLevel,
                              gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                              RayTracingInstanceMaskShadowCaster,
                              shadowOrigin,
                              parameters.rayLimits.x,
                              lightDirection,
                              shadowDistance);
        while (rayQueryProceedEXT(shadowQuery))
        {
        }
        if (rayQueryGetIntersectionTypeEXT(shadowQuery, true) ==
            gl_RayQueryCommittedIntersectionTriangleEXT)
        {
            return vec3(0.0);
        }
    }

    // 小片を点光源とみなす近似は小片のすぐ近くで発散するため、立体角を半球（2π）までに抑える。
    float solidAngle = min(sourceCosine * sampleArea / distanceSquared, 2.0 * Pi);
    return sourceRadiance * (receiverCosine * solidAngle);
}

// 発光するinstance（先頭instanceCount個、excludedCustomIndexは除く）の三角形から、receiverPositionで
// 向きnormalの面が受ける直接照度を求める。三角形を辺ごとにsubdivision個へ分けた合同な小三角形
// （subdivisionの2乗個）の重心を、面積の等しい点光源とみなす（固定の配置なのでframe間で揺らがない）。
// subdivisionが1なら三角形の重心1点になる。
vec3 ComputeEmitterIrradiance(vec3 receiverPosition,
                              vec3 shadowOrigin,
                              vec3 normal,
                              uint excludedCustomIndex,
                              uint instanceCount,
                              uint subdivision)
{
    vec3 irradiance = vec3(0.0);
    float subdivisionScale = float(max(subdivision, 1u));
    for (uint instanceIndex = 0u; instanceIndex < instanceCount; ++instanceIndex)
    {
        ProbeRayInstanceData emitter = instances.values[instanceIndex];
        if (emitter.geometry.w == excludedCustomIndex ||
            emitter.emissiveChromaticityAndLuminance.w <= 0.0 ||
            all(lessThanEqual(emitter.emissiveChromaticityAndLuminance.rgb, vec3(0.0))))
        {
            continue;
        }
        vec3 sourceRadiance = emitter.emissiveChromaticityAndLuminance.rgb *
                              emitter.emissiveChromaticityAndLuminance.a;

        uint triangleCount = emitter.geometry.z / 3u;
        for (uint triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex)
        {
            vec3 localVertex0;
            vec3 localVertex1;
            vec3 localVertex2;
            vec3 localNormal;
            bool bHasLocalNormal = false;
            if (!TryGetLocalTriangle(emitter,
                                     triangleIndex,
                                     localVertex0,
                                     localVertex1,
                                     localVertex2,
                                     localNormal,
                                     bHasLocalNormal))
            {
                continue;
            }

            vec3 vertex0 = TransformInstancePoint(emitter, localVertex0);
            vec3 edge1 = TransformInstancePoint(emitter, localVertex1) - vertex0;
            vec3 edge2 = TransformInstancePoint(emitter, localVertex2) - vertex0;
            vec3 crossEdges = cross(edge1, edge2);
            float doubleArea = length(crossEdges);
            if (isnan(doubleArea) || isinf(doubleArea) || doubleArea <= 1.0e-6)
            {
                continue;
            }

            // 頂点法線がない三角形は、ラスタの裏面カリング（時計回りが表）と同じく頂点順の外積の逆側を表とし、
            // 表の側へ放射する。
            vec3 sourceNormal = -crossEdges / doubleArea;
            if (bHasLocalNormal)
            {
                mat3 localToWorld = mat3(
                    vec3(emitter.transform0.x, emitter.transform1.x, emitter.transform2.x),
                    vec3(emitter.transform0.y, emitter.transform1.y, emitter.transform2.y),
                    vec3(emitter.transform0.z, emitter.transform1.z, emitter.transform2.z));
                float transformDeterminant = determinant(localToWorld);
                if (!isnan(transformDeterminant) && !isinf(transformDeterminant) &&
                    abs(transformDeterminant) > 1.0e-8)
                {
                    vec3 transformedNormal = transpose(inverse(localToWorld)) * localNormal;
                    float transformedLengthSquared = dot(transformedNormal, transformedNormal);
                    if (!any(isnan(transformedNormal)) && !any(isinf(transformedNormal)) &&
                        transformedLengthSquared > 1.0e-12)
                    {
                        sourceNormal = normalize(transformedNormal);
                    }
                }
            }

            float sampleArea = doubleArea * 0.5 / (subdivisionScale * subdivisionScale);
            for (uint row = 0u; row < subdivision; ++row)
            {
                for (uint column = 0u; row + column < subdivision; ++column)
                {
                    // 上向きの小三角形の重心と、その斜辺の向こうに接する下向きの小三角形の重心。
                    vec2 upward = (vec2(float(row), float(column)) + 1.0 / 3.0) / subdivisionScale;
                    irradiance += EvaluateEmitterSample(receiverPosition,
                                                        shadowOrigin,
                                                        normal,
                                                        vertex0 + edge1 * upward.x +
                                                            edge2 * upward.y,
                                                        sourceNormal,
                                                        sourceRadiance,
                                                        sampleArea);
                    if (row + column + 1u < subdivision)
                    {
                        vec2 downward = (vec2(float(row), float(column)) + 2.0 / 3.0) /
                                        subdivisionScale;
                        irradiance += EvaluateEmitterSample(receiverPosition,
                                                            shadowOrigin,
                                                            normal,
                                                            vertex0 + edge1 * downward.x +
                                                                edge2 * downward.y,
                                                            sourceNormal,
                                                            sourceRadiance,
                                                            sampleArea);
                    }
                }
            }
        }
    }
    return irradiance;
}
