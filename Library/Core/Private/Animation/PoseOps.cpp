#include "Animation/PoseOps.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        Math::Quaternion Normalize(const Math::Quaternion& q)
        {
            const double length =
                std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
            if (!(length > 0) || !std::isfinite(length))
            {
                return Math::Quaternion::Identity;
            }
            return {float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)};
        }
        bool ValidWeight(float weight)
        {
            return std::isfinite(weight) && weight >= 0 && weight <= 1;
        }
        bool Validate(const LocalPose& a, const LocalPose& b, float weight, Container::Span<const float> mask)
        {
            if (a.size() != b.size() || !ValidWeight(weight) || (!mask.empty() && mask.size() != a.size()))
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (!PoseOps::IsFinite(a[i]) || !PoseOps::IsFinite(b[i]) || (!mask.empty() && !ValidWeight(mask[i])))
                {
                    return false;
                }
            }
            return true;
        }
        JointTransform Blend(const JointTransform& a, const JointTransform& b, float weight)
        {
            if (weight == 0)
            {
                return a;
            }
            if (weight == 1)
            {
                return b;
            }
            JointTransform result;
            // 差を先に取らず、有限の両端からの補間で不要なoverflowを避ける。
            result.Translation = a.Translation * (1 - weight) + b.Translation * weight;
            result.Scale = a.Scale * (1 - weight) + b.Scale * weight;
            result.Rotation = PoseOps::Nlerp(a.Rotation, b.Rotation, weight);
            return result;
        }
        JointTransform Add(const JointTransform& a, const JointTransform& b, const JointTransform& reference,
                           float weight)
        {
            if (weight == 0)
            {
                return a;
            }
            JointTransform result = a;
            result.Translation += (b.Translation - reference.Translation) * weight;
            result.Scale += (b.Scale - reference.Scale) * weight;
            const auto q = Normalize(reference.Rotation);
            const auto target = Normalize(b.Rotation);
            const auto delta = target * Math::Quaternion(-q.x, -q.y, -q.z, q.w);
            result.Rotation = Normalize(PoseOps::Nlerp(Math::Quaternion::Identity, delta, weight) * a.Rotation);
            return result;
        }
    } // namespace
    bool PoseOps::IsFinite(const JointTransform& value) noexcept
    {
        const auto& t = value.Translation;
        const auto& s = value.Scale;
        const auto& q = value.Rotation;
        return std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z) && std::isfinite(s.x) &&
               std::isfinite(s.y) && std::isfinite(s.z) && std::isfinite(q.x) && std::isfinite(q.y) &&
               std::isfinite(q.z) && std::isfinite(q.w) && (q.x != 0 || q.y != 0 || q.z != 0 || q.w != 0);
    }
    Math::Quaternion PoseOps::Nlerp(const Math::Quaternion& a, const Math::Quaternion& b, float weight)
    {
        const auto x = Normalize(a), y = Normalize(b);
        const float sign = x.x * y.x + x.y * y.y + x.z * y.z + x.w * y.w < 0 ? -1.f : 1.f;
        const float u = 1 - weight, v = weight * sign;
        return Normalize({u * x.x + v * y.x, u * x.y + v * y.y, u * x.z + v * y.z, u * x.w + v * y.w});
    }
    bool PoseOps::BlendInto(LocalPose& dst, const LocalPose& source, float weight, Container::Span<const float> mask)
    {
        if (!Validate(dst, source, weight, mask))
        {
            return false;
        }
        for (size_t i = 0; i < dst.size(); ++i)
        {
            if (!IsFinite(Blend(dst[i], source[i], weight * (mask.empty() ? 1 : mask[i]))))
            {
                return false;
            }
        }
        for (size_t i = 0; i < dst.size(); ++i)
        {
            dst[i] = Blend(dst[i], source[i], weight * (mask.empty() ? 1 : mask[i]));
        }
        return true;
    }
    bool PoseOps::AddInto(LocalPose& dst, const LocalPose& target, const LocalPose& reference, float weight,
                          Container::Span<const float> mask)
    {
        if (!Validate(dst, target, weight, mask) || !Validate(dst, reference, weight, mask))
        {
            return false;
        }
        for (size_t i = 0; i < dst.size(); ++i)
        {
            if (!IsFinite(Add(dst[i], target[i], reference[i], weight * (mask.empty() ? 1 : mask[i]))))
            {
                return false;
            }
        }
        for (size_t i = 0; i < dst.size(); ++i)
        {
            dst[i] = Add(dst[i], target[i], reference[i], weight * (mask.empty() ? 1 : mask[i]));
        }
        return true;
    }
    bool BoneMask::Build(Container::Span<const int32_t> parents, uint32_t root, uint32_t fadeDepth)
    {
        if (root >= parents.size())
        {
            return false;
        }
        Container::VariableArray<float> candidate(parents.size(), 0);
        for (size_t i = 0; i < parents.size(); ++i)
        {
            int64_t joint = int64_t(i);
            size_t depth = 0;
            while (joint >= 0)
            {
                if (size_t(joint) >= parents.size() || depth > parents.size())
                {
                    return false;
                }
                if (uint64_t(joint) == root)
                {
                    candidate[i] = fadeDepth == 0 ? 1.f : std::min(1.f, float(depth + 1) / float(fadeDepth));
                    break;
                }
                if (parents[size_t(joint)] < -1)
                {
                    return false;
                }
                joint = parents[size_t(joint)];
                ++depth;
            }
        }
        Weights = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Animation
