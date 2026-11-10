#include "Rendering/SkinnedLodSelection.h"

#include "Rendering/DrawCommand.h"
#include "Math/GeometryIntersection.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool IsFiniteVector(const Math::Vector3& vector)
        {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        }

        bool IsFiniteVector(const Math::Vector4& vector)
        {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z) &&
                std::isfinite(vector.w);
        }

        bool IsFiniteMatrix(const Math::Matrix4x4& matrix)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!IsFiniteVector(matrix.GetRow(row)))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsValidBounds(const Math::AABB& bounds)
        {
            return IsFiniteVector(bounds.Min) && IsFiniteVector(bounds.Max) &&
                bounds.Min.x <= bounds.Max.x && bounds.Min.y <= bounds.Max.y && bounds.Min.z <= bounds.Max.z;
        }
    } // namespace

    SkinnedViewFrustum MakeSkinnedViewFrustum(const Math::Matrix4x4& viewProjection)
    {
        SkinnedViewFrustum result;
        if (!IsFiniteMatrix(viewProjection))
        {
            return result;
        }
        result.Planes = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            viewProjection, Math::ClipSpaceDepthRange::ZeroToOne);
        for (const auto& plane : result.Planes.Planes)
        {
            if (!IsFiniteVector(plane))
            {
                return result;
            }
        }
        result.bValid = true;
        return result;
    }

    bool ComputeSkinnedWorldBounds(const Math::AABB& localBounds, const Math::Matrix4x4& worldMatrix,
        Math::AABB& outWorldBounds)
    {
        if (!IsValidBounds(localBounds) || !IsFiniteMatrix(worldMatrix))
        {
            return false;
        }
        const Math::Vector3 center = Math::MatrixUtils::TransformPointRowVector(worldMatrix, localBounds.Center());
        const Math::Vector3 half = Math::MatrixUtils::AbsUpper3x3TransformExtentsRowVector(
            worldMatrix, localBounds.HalfExtents());
        if (!IsFiniteVector(center) || !IsFiniteVector(half))
        {
            return false;
        }
        const Math::AABB bounds = Math::AABB::FromCenterExtents(center, half);
        if (!IsValidBounds(bounds))
        {
            return false;
        }
        outWorldBounds = bounds;
        return true;
    }

    bool IsSkinnedBoundsInView(const Math::AABB& localBounds, const Math::Matrix4x4& worldMatrix,
        const SkinnedViewFrustum& frustum)
    {
        Math::AABB worldBounds;
        if (!frustum.bValid || !ComputeSkinnedWorldBounds(localBounds, worldMatrix, worldBounds))
        {
            return true;
        }
        Math::Frustum geometryFrustum;
        for (int index = 0; index < 6; ++index)
        {
            const Math::Vector4& plane = frustum.Planes.Planes[index];
            if (!IsFiniteVector(plane))
            {
                return true;
            }
            // クリップ空間の ax + by + cz + d に対し、Plane は dot(Normal, p) - Distance。
            geometryFrustum.Planes[index].Normal = Math::Vector3(plane.x, plane.y, plane.z);
            geometryFrustum.Planes[index].Distance = -plane.w;
        }
        return Math::FrustumIntersectsAABB(geometryFrustum, worldBounds);
    }

    bool IsSkinnedDrawInView(const DrawCommand& command, const SkinnedViewFrustum& frustum)
    {
        if (command.Draw.PayloadKind != DrawPayloadKind::Skinned || !command.Skinned.bHasLocalBounds)
        {
            return true;
        }
        return IsSkinnedBoundsInView(command.Skinned.LocalBounds, command.Draw.WorldMatrix, frustum);
    }
} // namespace NorvesLib::Core::Rendering
