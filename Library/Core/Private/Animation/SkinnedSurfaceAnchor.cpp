// スキンメッシュの表面フレームと関節別の候補から突き立て位置を求める。
#include "Animation/SkinnedSurfaceAnchor.h"

#include "Animation/SkeletalPoseBounds.h"
#include "Math/GeometryIntersection.h"
#include "Math/MatrixUtils.h"
#include "Resource/SkinnedMeshResource.h"
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        constexpr double RelativeAreaSquaredTolerance = 1.0e-12;
        constexpr double AbsoluteAreaSquaredTolerance = 1.0e-24;
        constexpr double AxisLengthSquaredTolerance = 1.0e-24;
        constexpr float BarycentricTolerance = 1.0e-5f;
        constexpr double DirectionLengthSquaredTolerance = 1.0e-4;

        struct DoubleVector3
        {
            double X;
            double Y;
            double Z;
        };

        DoubleVector3 ToDouble(const Math::Vector3& value) noexcept
        {
            return {value.x, value.y, value.z};
        }

        DoubleVector3 Difference(const Math::Vector3& a, const Math::Vector3& b) noexcept
        {
            return {double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z};
        }

        double Dot(const DoubleVector3& a, const DoubleVector3& b) noexcept
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        DoubleVector3 Cross(const DoubleVector3& a, const DoubleVector3& b) noexcept
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }

        bool IsFinite(const Math::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Math::Matrix4x4& value) noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                const auto components = value.GetRow(row);
                if (!std::isfinite(components.x) || !std::isfinite(components.y) ||
                    !std::isfinite(components.z) || !std::isfinite(components.w))
                {
                    return false;
                }
            }
            return true;
        }

        bool ToFloat(const DoubleVector3& value, Math::Vector3& out) noexcept
        {
            constexpr double maxFloat = double(std::numeric_limits<float>::max());
            if (!std::isfinite(value.X) || !std::isfinite(value.Y) || !std::isfinite(value.Z) ||
                std::fabs(value.X) > maxFloat || std::fabs(value.Y) > maxFloat || std::fabs(value.Z) > maxFloat)
            {
                return false;
            }
            out = Math::Vector3(float(value.X), float(value.Y), float(value.Z));
            return IsFinite(out);
        }

        bool Normalize(const DoubleVector3& value, DoubleVector3& out) noexcept
        {
            const double lengthSquared = Dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= AxisLengthSquaredTolerance)
            {
                return false;
            }
            const double inverse = 1.0 / std::sqrt(lengthSquared);
            out = {value.X * inverse, value.Y * inverse, value.Z * inverse};
            return true;
        }

        bool MakeAxes(const DoubleVector3& normalAxis, const DoubleVector3& tangentAxis,
                      Math::Vector3& outNormal, Math::Vector3& outTangent) noexcept
        {
            DoubleVector3 normal;
            if (!Normalize(normalAxis, normal))
            {
                return false;
            }
            const double projection = Dot(tangentAxis, normal);
            const DoubleVector3 orthogonal = {tangentAxis.X - projection * normal.X,
                                              tangentAxis.Y - projection * normal.Y,
                                              tangentAxis.Z - projection * normal.Z};
            if (Dot(orthogonal, orthogonal) <= RelativeAreaSquaredTolerance * Dot(tangentAxis, tangentAxis))
            {
                return false;
            }
            DoubleVector3 tangent;
            return Normalize(orthogonal, tangent) && ToFloat(normal, outNormal) && ToFloat(tangent, outTangent);
        }

        bool MakeSurfaceAxes(const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector3& p2,
                             Math::Vector3& outNormal, Math::Vector3& outTangent) noexcept
        {
            const DoubleVector3 edge1 = Difference(p1, p0);
            const DoubleVector3 edge2 = Difference(p2, p0);
            const DoubleVector3 normal = Cross(edge1, edge2);
            const double areaSquared = Dot(normal, normal);
            if (areaSquared <= AbsoluteAreaSquaredTolerance ||
                areaSquared <= RelativeAreaSquaredTolerance * Dot(edge1, edge1) * Dot(edge2, edge2))
            {
                return false;
            }
            return MakeAxes(normal, edge1, outNormal, outTangent);
        }

        bool Interpolate(const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector3& p2,
                         float u, float v, Math::Vector3& out) noexcept
        {
            const DoubleVector3 edge1 = Difference(p1, p0);
            const DoubleVector3 edge2 = Difference(p2, p0);
            return ToFloat({double(p0.x) + u * edge1.X + v * edge2.X,
                            double(p0.y) + u * edge1.Y + v * edge2.Y,
                            double(p0.z) + u * edge1.Z + v * edge2.Z}, out);
        }

        bool GetVertices(const SkinnedMeshResource& mesh, uint32_t triangleIndex,
                         const Skeletal::SkeletalVertex*& out0, const Skeletal::SkeletalVertex*& out1,
                         const Skeletal::SkeletalVertex*& out2) noexcept
        {
            const auto& indices = mesh.GetIndices();
            const auto& vertices = mesh.GetVertices();
            if (size_t(triangleIndex) >= indices.size() / 3)
            {
                return false;
            }
            const size_t first = size_t(triangleIndex) * 3;
            const uint32_t i0 = indices[first];
            const uint32_t i1 = indices[first + 1];
            const uint32_t i2 = indices[first + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                return false;
            }
            out0 = &vertices[i0];
            out1 = &vertices[i1];
            out2 = &vertices[i2];
            return true;
        }

        bool SkinWorldPosition(const Skeletal::SkeletalVertex& vertex, const SurfaceAnchorPoseView& pose,
                               Math::Vector3& out) noexcept
        {
            for (size_t influence = 0; influence < 4; ++influence)
            {
                if (vertex.JointWeights[influence] > 0.0f)
                {
                    const uint32_t joint = vertex.JointIndices[influence];
                    if (joint >= pose.Palette.size() || !IsFinite(pose.Palette[joint]))
                    {
                        return false;
                    }
                }
            }
            const Math::Vector3 local = SkinPosition(vertex, pose.Palette);
            if (!IsFinite(local))
            {
                return false;
            }
            out = Math::MatrixUtils::TransformPointRowVector(pose.OwnerWorld, local);
            return IsFinite(out);
        }

        bool SkinTriangle(const SkinnedMeshResource& mesh, uint32_t triangleIndex, const SurfaceAnchorPoseView& pose,
                          Math::Vector3& out0, Math::Vector3& out1, Math::Vector3& out2) noexcept
        {
            const Skeletal::SkeletalVertex* v0;
            const Skeletal::SkeletalVertex* v1;
            const Skeletal::SkeletalVertex* v2;
            return GetVertices(mesh, triangleIndex, v0, v1, v2) && SkinWorldPosition(*v0, pose, out0) &&
                   SkinWorldPosition(*v1, pose, out1) && SkinWorldPosition(*v2, pose, out2);
        }

        bool GetDominantJoint(const SkinnedMeshResource& mesh, uint32_t triangleIndex, uint32_t jointCount,
                              uint32_t& outJoint) noexcept
        {
            const Skeletal::SkeletalVertex* vertices[3];
            if (!GetVertices(mesh, triangleIndex, vertices[0], vertices[1], vertices[2]))
            {
                return false;
            }
            float largestWeight = 0.0f;
            uint32_t chosen = UINT32_MAX;
            for (const auto* vertex : vertices)
            {
                for (size_t influence = 0; influence < 4; ++influence)
                {
                    const float weight = vertex->JointWeights[influence];
                    const uint32_t joint = vertex->JointIndices[influence];
                    if (std::isfinite(weight) && weight > 0.0f && joint < jointCount &&
                        (weight > largestWeight || (weight == largestWeight && joint < chosen)))
                    {
                        largestWeight = weight;
                        chosen = joint;
                    }
                }
            }
            if (chosen == UINT32_MAX)
            {
                return false;
            }
            outJoint = chosen;
            return true;
        }

        bool GetBarycentric(const Math::Vector3& point, const Math::Vector3& p0, const Math::Vector3& p1,
                            const Math::Vector3& p2, float& outU, float& outV) noexcept
        {
            const DoubleVector3 edge1 = Difference(p1, p0);
            const DoubleVector3 edge2 = Difference(p2, p0);
            const DoubleVector3 offset = Difference(point, p0);
            const double d00 = Dot(edge1, edge1);
            const double d01 = Dot(edge1, edge2);
            const double d11 = Dot(edge2, edge2);
            const double d20 = Dot(offset, edge1);
            const double d21 = Dot(offset, edge2);
            const DoubleVector3 cross = Cross(edge1, edge2);
            const double denominator = Dot(cross, cross);
            if (denominator <= AbsoluteAreaSquaredTolerance ||
                denominator <= RelativeAreaSquaredTolerance * d00 * d11)
            {
                return false;
            }
            double u = (d11 * d20 - d01 * d21) / denominator;
            double v = (d00 * d21 - d01 * d20) / denominator;
            if (!std::isfinite(u) || !std::isfinite(v))
            {
                return false;
            }
            // 最近点を float で受け取る際の丸めを三角形内に戻す。
            u = std::fmax(0.0, std::fmin(1.0, u));
            v = std::fmax(0.0, std::fmin(1.0, v));
            if (u + v > 1.0)
            {
                const double sum = u + v;
                u /= sum;
                v /= sum;
            }
            outU = float(u);
            outV = float(v);
            return true;
        }

        struct PointCandidate
        {
            uint32_t TriangleIndex = 0;
            float U = 0.0f;
            float V = 0.0f;
            double Distance = 0.0;
            Math::Vector3 Position;
        };
    } // namespace

    SurfaceAnchorFrame EvaluateSurfaceAnchor(const SurfaceAnchorDesc& desc, const SkinnedMeshResource& mesh,
                                            const SurfaceAnchorPoseView& pose) noexcept
    {
        SurfaceAnchorFrame frame;
        if (desc.MeshRevision != mesh.GetPoseRevision() || !IsFinite(pose.OwnerWorld) ||
            !std::isfinite(desc.U) || !std::isfinite(desc.V) || desc.U < 0.0f || desc.V < 0.0f ||
            double(desc.U) + desc.V > 1.0 + BarycentricTolerance)
        {
            return frame;
        }
        Math::Vector3 p0, p1, p2;
        if (!SkinTriangle(mesh, desc.TriangleIndex, pose, p0, p1, p2))
        {
            return frame;
        }
        if (MakeSurfaceAxes(p0, p1, p2, frame.Normal, frame.Tangent))
        {
            frame.bValid = Interpolate(p0, p1, p2, desc.U, desc.V, frame.Position);
            return frame;
        }
        if (desc.FallbackJoint >= pose.JointModelMatrices.size() || !IsFinite(desc.FallbackLocalOffset) ||
            !IsFinite(pose.JointModelMatrices[desc.FallbackJoint]))
        {
            return {};
        }
        const Math::Matrix4x4 jointWorld = pose.JointModelMatrices[desc.FallbackJoint] * pose.OwnerWorld;
        if (!IsFinite(jointWorld))
        {
            return {};
        }
        const auto x = jointWorld.GetRow(0);
        const auto y = jointWorld.GetRow(1);
        if (!MakeAxes({y.x, y.y, y.z}, {x.x, x.y, x.z}, frame.Normal, frame.Tangent))
        {
            return {};
        }
        frame.Position = Math::MatrixUtils::TransformPointRowVector(jointWorld, desc.FallbackLocalOffset);
        if (!IsFinite(frame.Position))
        {
            return {};
        }
        frame.bValid = true;
        frame.bFromFallbackJoint = true;
        return frame;
    }

    bool JointTriangleBuckets::Build(const SkinnedMeshResource& mesh, uint32_t jointCount)
    {
        m_bBuilt = false;
        m_MeshRevision = 0;
        m_JointCount = 0;
        m_UnassignedTriangleCount = 0;
        m_Offsets.clear();
        m_Triangles.clear();
        const size_t triangleCount = mesh.GetIndices().size() / 3;
        const size_t offsetCount = size_t(jointCount) + 1;
        if (triangleCount > UINT32_MAX || offsetCount > m_Offsets.max_size() ||
            triangleCount > m_Triangles.max_size())
        {
            return false;
        }
        try
        {
            m_Offsets.assign(offsetCount, 0);
            for (size_t triangle = 0; triangle < triangleCount; ++triangle)
            {
                uint32_t joint;
                if (GetDominantJoint(mesh, uint32_t(triangle), jointCount, joint))
                {
                    ++m_Offsets[size_t(joint) + 1];
                }
                else
                {
                    ++m_UnassignedTriangleCount;
                }
            }
            for (size_t joint = 0; joint < jointCount; ++joint)
            {
                m_Offsets[joint + 1] += m_Offsets[joint];
            }
            m_Triangles.resize(m_Offsets[jointCount]);
            for (size_t triangle = 0; triangle < triangleCount; ++triangle)
            {
                uint32_t joint;
                if (GetDominantJoint(mesh, uint32_t(triangle), jointCount, joint))
                {
                    m_Triangles[m_Offsets[joint]++] = uint32_t(triangle);
                }
            }
            // 書き込み位置として進めたオフセットを、各区間の先頭に戻す。
            for (size_t joint = jointCount; joint > 0; --joint)
            {
                m_Offsets[joint] = m_Offsets[joint - 1];
            }
            m_Offsets[0] = 0;
        }
        catch (const std::bad_alloc&)
        {
            m_Offsets.clear();
            m_Triangles.clear();
            m_UnassignedTriangleCount = 0;
            return false;
        }
        catch (const std::length_error&)
        {
            m_Offsets.clear();
            m_Triangles.clear();
            m_UnassignedTriangleCount = 0;
            return false;
        }
        m_MeshRevision = mesh.GetPoseRevision();
        m_JointCount = jointCount;
        m_bBuilt = true;
        return true;
    }

    Container::Span<const uint32_t> JointTriangleBuckets::GetTriangles(uint32_t joint) const noexcept
    {
        if (!m_bBuilt || joint >= m_JointCount || m_Offsets[joint] == m_Offsets[size_t(joint) + 1])
        {
            return {};
        }
        const uint32_t first = m_Offsets[joint];
        return {m_Triangles.data() + first, size_t(m_Offsets[size_t(joint) + 1] - first)};
    }

    uint64_t JointTriangleBuckets::GetMeshRevision() const noexcept
    {
        return m_MeshRevision;
    }

    uint32_t JointTriangleBuckets::GetJointCount() const noexcept
    {
        return m_JointCount;
    }

    uint32_t JointTriangleBuckets::GetUnassignedTriangleCount() const noexcept
    {
        return m_UnassignedTriangleCount;
    }

    bool JointTriangleBuckets::IsBuiltFor(const SkinnedMeshResource& mesh) const noexcept
    {
        return m_bBuilt && m_MeshRevision == mesh.GetPoseRevision();
    }

    StabResult FindSurfacePoint(const StabQuery& query, const JointTriangleBuckets& buckets,
                                const SkinnedMeshResource& mesh, const SurfaceAnchorPoseView& pose) noexcept
    {
        StabResult result;
        result.MeshRevision = mesh.GetPoseRevision();
        const double directionLengthSquared = Dot(ToDouble(query.Ray.Direction), ToDouble(query.Ray.Direction));
        if (!buckets.IsBuiltFor(mesh) || !IsFinite(pose.OwnerWorld) || !IsFinite(query.Ray.Origin) ||
            !IsFinite(query.Ray.Direction) || !std::isfinite(query.MaxDistance) || query.MaxDistance < 0.0f ||
            !std::isfinite(query.MaxSnapDistance) || query.MaxSnapDistance < 0.0f ||
            std::fabs(directionLengthSquared - 1.0) > DirectionLengthSquaredTolerance)
        {
            result.Reason = StabMissReason::InvalidInput;
            return result;
        }
        const Math::Vector3 segmentEnd = query.Ray.Origin + query.Ray.Direction * query.MaxDistance;
        if (!IsFinite(segmentEnd))
        {
            result.Reason = StabMissReason::InvalidInput;
            return result;
        }

        bool bHasHit = false;
        bool bHasSnap = false;
        PointCandidate bestHit;
        PointCandidate bestSnap;
        for (size_t candidate = 0; candidate < query.CandidateJoints.size(); ++candidate)
        {
            const uint32_t joint = query.CandidateJoints[candidate];
            if (joint >= buckets.GetJointCount())
            {
                continue;
            }
            bool bDuplicate = false;
            for (size_t previous = 0; previous < candidate; ++previous)
            {
                if (query.CandidateJoints[previous] == joint)
                {
                    bDuplicate = true;
                    break;
                }
            }
            if (bDuplicate)
            {
                continue;
            }
            const auto triangles = buckets.GetTriangles(joint);
            for (size_t triangle = 0; triangle < triangles.size(); ++triangle)
            {
                const uint32_t triangleIndex = triangles[triangle];
                Math::Vector3 p0, p1, p2;
                if (!SkinTriangle(mesh, triangleIndex, pose, p0, p1, p2))
                {
                    continue;
                }
                ++result.TrianglesTested;
                float t = 0.0f, u = 0.0f, v = 0.0f;
                const bool bRayHit = Math::RayIntersectsTriangleBarycentric(query.Ray, p0, p1, p2, t, u, v);
                Math::Vector3 normal, tangent;
                if (!MakeSurfaceAxes(p0, p1, p2, normal, tangent))
                {
                    continue;
                }
                if (bRayHit && std::isfinite(t) && std::isfinite(u) && std::isfinite(v) &&
                    t >= 0.0f && t <= query.MaxDistance && u >= 0.0f && v >= 0.0f &&
                    double(u) + v <= 1.0 + BarycentricTolerance)
                {
                    Math::Vector3 position;
                    if (Interpolate(p0, p1, p2, u, v, position) &&
                        (!bHasHit || t < bestHit.Distance || (t == bestHit.Distance && triangleIndex < bestHit.TriangleIndex)))
                    {
                        bestHit = {triangleIndex, u, v, t, position};
                        bHasHit = true;
                    }
                }
                if (bHasHit)
                {
                    continue;
                }
                Math::Vector3 onSegment, onTriangle;
                Math::ClosestPointsSegmentTriangle(query.Ray.Origin, segmentEnd, Math::Triangle(p0, p1, p2),
                                                   onSegment, onTriangle);
                if (!IsFinite(onSegment) || !IsFinite(onTriangle) ||
                    !GetBarycentric(onTriangle, p0, p1, p2, u, v))
                {
                    continue;
                }
                Math::Vector3 position;
                if (!Interpolate(p0, p1, p2, u, v, position))
                {
                    continue;
                }
                const DoubleVector3 difference = Difference(onSegment, onTriangle);
                const double distance = std::sqrt(Dot(difference, difference));
                if (std::isfinite(distance) &&
                    (!bHasSnap || distance < bestSnap.Distance ||
                     (distance == bestSnap.Distance && triangleIndex < bestSnap.TriangleIndex)))
                {
                    bestSnap = {triangleIndex, u, v, distance, position};
                    bHasSnap = true;
                }
            }
        }

        if (bHasHit || (bHasSnap && bestSnap.Distance <= query.MaxSnapDistance))
        {
            const PointCandidate& best = bHasHit ? bestHit : bestSnap;
            result.Kind = bHasHit ? StabResultKind::Hit : StabResultKind::Snapped;
            result.TriangleIndex = best.TriangleIndex;
            result.U = best.U;
            result.V = best.V;
            result.Distance = float(best.Distance);
            result.WorldPosition = best.Position;
        }
        else
        {
            result.Reason = result.TrianglesTested == 0 ? StabMissReason::NoCandidates
                                                        : StabMissReason::BeyondSnapDistance;
        }
        return result;
    }
} // namespace NorvesLib::Core::Animation
