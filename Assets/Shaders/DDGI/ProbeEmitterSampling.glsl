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

// shadowOriginからreceiverPosition→samplePositionの向きへ、発光面の手前まで遮られていないか。
bool IsEmitterSampleVisible(vec3 receiverPosition, vec3 shadowOrigin, vec3 samplePosition)
{
    vec3 toSource = samplePosition - receiverPosition;
    float distanceToSource = length(toSource);
    if (isnan(distanceToSource) || isinf(distanceToSource) || distanceToSource <= 1.0e-6)
    {
        return false;
    }
    float shadowDistance = max(distanceToSource - EmitterShadowEndOffset, 0.0);
    if (shadowDistance <= parameters.rayLimits.x)
    {
        return true;
    }

    rayQueryEXT shadowQuery;
    rayQueryInitializeEXT(shadowQuery,
                          sceneTopLevel,
                          gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          RayTracingInstanceMaskShadowCaster,
                          shadowOrigin,
                          parameters.rayLimits.x,
                          toSource / distanceToSource,
                          shadowDistance);
    while (rayQueryProceedEXT(shadowQuery))
    {
    }
    return rayQueryGetIntersectionTypeEXT(shadowQuery, true) !=
           gl_RayQueryCommittedIntersectionTriangleEXT;
}

// 三角形を受け手の半球（dot(normal, x - receiverPosition) > 0）で切り取った多角形（最大4頂点）の頂点数。
uint ClipTriangleToHemisphere(vec3 receiverPosition,
                              vec3 normal,
                              vec3 vertex0,
                              vec3 vertex1,
                              vec3 vertex2,
                              out vec3 polygon[4])
{
    vec3 source[3] = vec3[3](vertex0, vertex1, vertex2);
    polygon = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    uint count = 0u;
    for (uint index = 0u; index < 3u; ++index)
    {
        vec3 current = source[index];
        vec3 next = source[(index + 1u) % 3u];
        float currentHeight = dot(normal, current - receiverPosition);
        float nextHeight = dot(normal, next - receiverPosition);
        if (currentHeight > 0.0 && count < 4u)
        {
            polygon[count] = current;
            ++count;
        }
        if ((currentHeight > 0.0) != (nextHeight > 0.0) && count < 4u)
        {
            polygon[count] = mix(current, next, currentHeight / (currentHeight - nextHeight));
            ++count;
        }
    }
    return count;
}

// 放射輝度1で一様に放射する多角形（受け手の半球の中）から、向きnormalの面が受ける照度（Lambertの式。
// 各辺が張る角と、受け手と辺を通る面の法線から求める厳密な値で、πを超えない）。
float ComputePolygonIrradianceFactor(vec3 receiverPosition,
                                     vec3 normal,
                                     vec3 polygon[4],
                                     uint count)
{
    if (count < 3u)
    {
        return 0.0;
    }
    vec3 vectorIrradiance = vec3(0.0);
    for (uint index = 0u; index < count; ++index)
    {
        vec3 edgeStart = normalize(polygon[index] - receiverPosition);
        vec3 edgeEnd = normalize(polygon[(index + 1u) % count] - receiverPosition);
        vec3 edgeCross = cross(edgeStart, edgeEnd);
        float crossLength = length(edgeCross);
        if (!(crossLength > 1.0e-8))
        {
            continue;
        }
        float edgeAngle = atan(crossLength, dot(edgeStart, edgeEnd));
        vectorIrradiance += edgeAngle * (edgeCross / crossLength);
    }
    float factor = 0.5 * abs(dot(normal, vectorIrradiance));
    return (isnan(factor) || isinf(factor)) ? 0.0 : min(factor, Pi);
}

// 発光するinstance（先頭instanceCount個、excludedCustomIndexは除く）の三角形から、receiverPositionで
// 向きnormalの面が受ける直接照度を求める。影のない照度は三角形を受け手の半球で切り取った多角形から
// Lambertの式で厳密に求め、影は三角形を辺ごとにsubdivision個へ分けた合同な小三角形（subdivisionの
// 2乗個）の重心へ向けた影のrayの、影のない寄与の重みでの可視率を掛ける（固定の配置なのでframe間で
// 揺らがず、影のない値を超えない）。subdivisionが1なら三角形の重心1点で可視を確かめる。
// 放射する面の向きは三角形の幾何の法線（頂点順の外積を法線の変換（逆転置）でworldへ移した向き）で、
// 表（放射する側）は頂点法線の平均の側、頂点法線がなければ外積の逆側にする（鏡映の変換でも閉じた物体の
// 外向きを保ち、頂点の順の巡回で変わらない）。
vec3 ComputeEmitterIrradiance(vec3 receiverPosition,
                              vec3 shadowOrigin,
                              vec3 normal,
                              uint excludedCustomIndex,
                              uint instanceCount,
                              uint subdivision)
{
    vec3 irradiance = vec3(0.0);
    uint sampleSubdivision = max(subdivision, 1u);
    float subdivisionScale = float(sampleSubdivision);
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
        mat3 localToWorld = mat3(
            vec3(emitter.transform0.x, emitter.transform1.x, emitter.transform2.x),
            vec3(emitter.transform0.y, emitter.transform1.y, emitter.transform2.y),
            vec3(emitter.transform0.z, emitter.transform1.z, emitter.transform2.z));
        float transformDeterminant = determinant(localToWorld);
        if (isnan(transformDeterminant) || isinf(transformDeterminant) ||
            abs(transformDeterminant) <= 1.0e-8)
        {
            continue;
        }
        mat3 normalTransform = transpose(inverse(localToWorld));

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

            vec3 sourceNormal = normalTransform *
                cross(localVertex1 - localVertex0, localVertex2 - localVertex0);
            float sourceNormalLengthSquared = dot(sourceNormal, sourceNormal);
            if (isnan(sourceNormalLengthSquared) || isinf(sourceNormalLengthSquared) ||
                sourceNormalLengthSquared <= 1.0e-12)
            {
                continue;
            }
            sourceNormal *= inversesqrt(sourceNormalLengthSquared);
            vec3 frontHint = bHasLocalNormal ? normalTransform * localNormal : -sourceNormal;
            if (dot(sourceNormal, frontHint) < 0.0)
            {
                sourceNormal = -sourceNormal;
            }

            vec3 vertex0 = TransformInstancePoint(emitter, localVertex0);
            vec3 vertex1 = TransformInstancePoint(emitter, localVertex1);
            vec3 vertex2 = TransformInstancePoint(emitter, localVertex2);
            // 受け手が表の側にない（面の裏か面の上）ときは届かない。
            if (!(dot(sourceNormal, receiverPosition - vertex0) > 1.0e-6))
            {
                continue;
            }

            vec3 polygon[4];
            uint polygonCount = ClipTriangleToHemisphere(
                receiverPosition, normal, vertex0, vertex1, vertex2, polygon);
            float factor = ComputePolygonIrradianceFactor(
                receiverPosition, normal, polygon, polygonCount);
            if (!(factor > 0.0))
            {
                continue;
            }

            vec3 edge1 = vertex1 - vertex0;
            vec3 edge2 = vertex2 - vertex0;
            float sampleArea = 0.5 * length(cross(edge1, edge2)) /
                               (subdivisionScale * subdivisionScale);
            float weightSum = 0.0;
            float visibleWeight = 0.0;
            for (uint row = 0u; row < sampleSubdivision; ++row)
            {
                for (uint column = 0u; row + column < sampleSubdivision; ++column)
                {
                    for (uint orientation = 0u; orientation < 2u; ++orientation)
                    {
                        // 上向きの小三角形の重心と、その斜辺の向こうに接する下向きの小三角形の重心。
                        if (orientation == 1u && row + column + 1u >= sampleSubdivision)
                        {
                            continue;
                        }
                        float offset = orientation == 0u ? 1.0 / 3.0 : 2.0 / 3.0;
                        vec2 barycentric =
                            (vec2(float(row), float(column)) + offset) / subdivisionScale;
                        vec3 samplePosition =
                            vertex0 + edge1 * barycentric.x + edge2 * barycentric.y;
                        vec3 toSource = samplePosition - receiverPosition;
                        float distanceSquared = dot(toSource, toSource);
                        if (!(distanceSquared > 1.0e-6))
                        {
                            continue;
                        }
                        vec3 lightDirection = toSource * inversesqrt(distanceSquared);
                        float weight = max(dot(normal, lightDirection), 0.0) *
                                       max(dot(sourceNormal, -lightDirection), 0.0) *
                                       min(sampleArea / distanceSquared, 2.0 * Pi);
                        if (!(weight > 0.0))
                        {
                            continue;
                        }
                        weightSum += weight;
                        if (IsEmitterSampleVisible(receiverPosition, shadowOrigin, samplePosition))
                        {
                            visibleWeight += weight;
                        }
                    }
                }
            }

            float visibility = 0.0;
            if (weightSum > 0.0)
            {
                visibility = visibleWeight / weightSum;
            }
            else
            {
                // 小片の重心がどれも受け手の半球の外なら、切り取った多角形の重心で確かめる。
                vec3 polygonCenter = vec3(0.0);
                for (uint index = 0u; index < polygonCount; ++index)
                {
                    polygonCenter += polygon[index];
                }
                polygonCenter /= float(polygonCount);
                visibility = IsEmitterSampleVisible(receiverPosition, shadowOrigin, polygonCenter)
                    ? 1.0
                    : 0.0;
            }
            irradiance += sourceRadiance * (factor * visibility);
        }
    }
    return irradiance;
}
