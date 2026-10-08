#include "Animation/SkeletalPoseBounds.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Container/UnorderedMap.h"
#include "Resource/SkeletalGltfData.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        bool Finite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace
    bool IsValidPoseBoundsSettings(const PoseBoundsSettings& settings) noexcept
    {
        return std::isfinite(settings.WeightThreshold) && settings.WeightThreshold >= 0 &&
               settings.WeightThreshold <= 1 && std::isfinite(settings.RelativePadding) &&
               settings.RelativePadding >= 0;
    }
    void BuildMeshPoseBounds(Container::Span<const Skeletal::SkeletalVertex> vertices, float threshold,
                             MeshPoseBounds& out)
    {
        out = {};
        out.bHasVertices = !vertices.empty();
        Container::UnorderedMap<uint32_t, Math::AABB> jointBounds;
        Container::UnorderedMap<uint64_t, Math::AABB> fallbackBounds;
        for (const auto& vertex : vertices)
        {
            const Math::Vector3 position(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
            out.bNeedsExactFallback = out.bNeedsExactFallback || !Finite(position);
            float totalPositive = 0;
            for (float weight : vertex.JointWeights)
            {
                if (std::isfinite(weight) && weight > 0)
                {
                    totalPositive += weight;
                }
            }
            out.bNeedsExactFallback = out.bNeedsExactFallback || !std::isfinite(totalPositive);
            out.MaximumWeightSum = std::fmax(out.MaximumWeightSum, totalPositive);
            uint64_t counts[4] = {};
            size_t used = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                const float weight = vertex.JointWeights[i];
                if (std::isfinite(weight) && weight > threshold)
                {
                    const uint32_t index = vertex.JointIndices[i];
                    auto entry = jointBounds.emplace(index, Math::AABB::CreateInvalid());
                    entry.first->second.Expand(position);
                    counts[used++] = uint64_t(index) + 1;
                }
            }
            std::sort(counts, counts + used);
            uint64_t required = UINT64_MAX;
            for (size_t c = 0; c < used; ++c)
            {
                float total = 0;
                // 元の4影響の順で足し、EPSILON付近の丸めを旧SkinVertexと合わせる。
                for (size_t i = 0; i < 4; ++i)
                {
                    const float weight = vertex.JointWeights[i];
                    if (std::isfinite(weight) && weight > threshold && vertex.JointIndices[i] < counts[c])
                    {
                        total += weight;
                    }
                }
                out.bNeedsExactFallback = out.bNeedsExactFallback || !std::isfinite(total);
                if (total > Math::Constants::EPSILON)
                {
                    required = counts[c];
                    break;
                }
            }
            auto fallback = fallbackBounds.emplace(required, Math::AABB::CreateInvalid());
            fallback.first->second.Expand(position);
        }
        out.Joints.reserve(jointBounds.size());
        for (const auto& entry : jointBounds)
        {
            out.Joints.push_back({entry.first, entry.second});
        }
        out.Fallbacks.reserve(fallbackBounds.size());
        for (const auto& entry : fallbackBounds)
        {
            out.Fallbacks.push_back({entry.first, entry.second});
        }
        std::sort(out.Joints.begin(), out.Joints.end(),
                  [](const auto& a, const auto& b) { return a.JointIndex < b.JointIndex; });
        std::sort(out.Fallbacks.begin(), out.Fallbacks.end(),
                  [](const auto& a, const auto& b) { return a.RequiredJointCount < b.RequiredJointCount; });
    }

    Math::Vector3 SkinPosition(const Skeletal::SkeletalVertex& vertex, Container::Span<const Math::Matrix4x4> palette)
    {
        const Math::Vector3 source(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
        Math::Vector3 position = Math::Vector3::Zero;
        float totalWeight = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            const float weight = vertex.JointWeights[i];
            const uint32_t joint = vertex.JointIndices[i];
            if (!std::isfinite(weight) || weight <= 0 || joint >= palette.size() ||
                !Detail::IsFiniteMatrix(palette[joint]))
            {
                continue;
            }
            position += Math::MatrixUtils::TransformPointRowVector(palette[joint], source) * weight;
            totalWeight += weight;
        }
        return totalWeight <= Math::Constants::EPSILON ? source : position / totalWeight;
    }
    bool ComputeExactBounds(Container::Span<const Skeletal::SkeletalVertex> vertices,
                            Container::Span<const Math::Matrix4x4> palette, bool requireFinite, Math::AABB& out)
    {
        auto result = Math::AABB::CreateInvalid();
        for (const auto& vertex : vertices)
        {
            const auto position = SkinPosition(vertex, palette);
            if (requireFinite && !Finite(position))
            {
                return false;
            }
            result.Expand(position);
        }
        out = result;
        return true;
    }
    bool TransformJointBounds(Container::Span<const JointBindBounds> joints, const Math::AABB& fallback,
                              Container::Span<const Math::Matrix4x4> palette, float padding, Math::AABB& out,
                              float maximumWeightSum)
    {
        auto result = fallback;
        double roundoff[3] = {};
        for (const auto& joint : joints)
        {
            if (joint.JointIndex >= palette.size())
            {
                continue;
            }
            const auto& matrix = palette[joint.JointIndex];
            if (!Detail::IsFiniteMatrix(matrix) || matrix.m03 != 0 || matrix.m13 != 0 || matrix.m23 != 0)
            {
                return false;
            }
            const double extent[] = {
                std::fmax(std::fabs(double(joint.Bounds.Min.x)), std::fabs(double(joint.Bounds.Max.x))),
                std::fmax(std::fabs(double(joint.Bounds.Min.y)), std::fabs(double(joint.Bounds.Max.y))),
                std::fmax(std::fabs(double(joint.Bounds.Min.z)), std::fabs(double(joint.Bounds.Max.z)))};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double sum = extent[0] * std::fabs(double(matrix.m[0][axis])) +
                                   extent[1] * std::fabs(double(matrix.m[1][axis])) +
                                   extent[2] * std::fabs(double(matrix.m[2][axis])) +
                                   std::fabs(double(matrix.m[3][axis]));
                // 3積和・最大4影響の加算・重み合計と除算のfloat丸めを外向きに覆う。
                const double underflow = 16.0 * std::numeric_limits<float>::denorm_min() / Math::Constants::EPSILON;
                roundoff[axis] =
                    std::fmax(roundoff[axis], sum * (32.0 * std::numeric_limits<float>::epsilon()) + underflow);
            }
            for (unsigned corner = 0; corner < 8; ++corner)
            {
                const Math::Vector3 point((corner & 1) ? joint.Bounds.Max.x : joint.Bounds.Min.x,
                                          (corner & 2) ? joint.Bounds.Max.y : joint.Bounds.Min.y,
                                          (corner & 4) ? joint.Bounds.Max.z : joint.Bounds.Min.z);
                const auto transformed = Math::MatrixUtils::TransformPointRowVector(matrix, point);
                const float safeMagnitude = (std::numeric_limits<float>::max() / 8.0f) / maximumWeightSum;
                if (!Finite(transformed) || std::fabs(transformed.x) > safeMagnitude ||
                    std::fabs(transformed.y) > safeMagnitude || std::fabs(transformed.z) > safeMagnitude)
                {
                    return false;
                }
                result.Expand(transformed);
            }
        }
        float* lower[] = {&result.Min.x, &result.Min.y, &result.Min.z};
        float* upper[] = {&result.Max.x, &result.Max.y, &result.Max.z};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (roundoff[axis] > 0)
            {
                *lower[axis] = std::nextafter(static_cast<float>(double(*lower[axis]) - roundoff[axis]),
                                              -std::numeric_limits<float>::infinity());
                *upper[axis] = std::nextafter(static_cast<float>(double(*upper[axis]) + roundoff[axis]),
                                              std::numeric_limits<float>::infinity());
                if (!std::isfinite(*lower[axis]) || !std::isfinite(*upper[axis]))
                {
                    return false;
                }
            }
        }
        if (!ApplyPoseBoundsPadding(padding, result))
        {
            return false;
        }
        out = result;
        return true;
    }
    bool ApplyPoseBoundsPadding(float padding, Math::AABB& bounds)
    {
        if (!std::isfinite(padding) || padding < 0)
        {
            return false;
        }
        if (padding == 0 || bounds.Min.x > bounds.Max.x || bounds.Min.y > bounds.Max.y || bounds.Min.z > bounds.Max.z)
        {
            return true;
        }
        const float distance = (bounds.Max - bounds.Min).Length() * padding;
        if (!std::isfinite(distance))
        {
            return false;
        }
        const Math::Vector3 extra(distance, distance, distance);
        const auto minimum = bounds.Min - extra;
        const auto maximum = bounds.Max + extra;
        if (!Finite(minimum) || !Finite(maximum))
        {
            return false;
        }
        bounds.Min = minimum;
        bounds.Max = maximum;
        return true;
    }
} // namespace NorvesLib::Core::Animation
