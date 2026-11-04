#include "Animation/Procedural/AngleDistribution.h"
#include "Math/QuaternionUtils.h"
#include <cmath>

namespace NorvesLib::Core::Animation::Procedural
{
    namespace
    {
        bool IsValidRotation(const Math::Quaternion& rotation)
        {
            if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
                !std::isfinite(rotation.w))
            {
                return false;
            }
            const double lengthSquared = double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                         double(rotation.z) * rotation.z + double(rotation.w) * rotation.w;
            const double epsilon = Math::Constants::EPSILON;
            return lengthSquared > epsilon * epsilon;
        }

        // 有効性を確認済みの回転だけを渡す。倍精度で長さを求めて有限の float の二乗溢れを避ける。
        Math::Quaternion NormalizeRotation(const Math::Quaternion& rotation)
        {
            const double length = std::sqrt(double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                            double(rotation.z) * rotation.z + double(rotation.w) * rotation.w);
            return {float(rotation.x / length), float(rotation.y / length), float(rotation.z / length),
                    float(rotation.w / length)};
        }

        bool TryGetRootRotation(const SkeletonPoseRuntime& skeleton, Math::Quaternion& outRotation)
        {
            if (!skeleton.bSplit)
            {
                outRotation = Math::Quaternion::Identity;
                return true;
            }
            const auto& frame = skeleton.RootFrame;
            for (float value : frame.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }

            // 行ベクトルの RootFrame から各行のスケールを除き、列規約の四元数に戻す。
            Math::Matrix4x4 rotation;
            for (size_t row = 0; row < 3; ++row)
            {
                const double x = frame.m[row][0];
                const double y = frame.m[row][1];
                const double z = frame.m[row][2];
                const double length = std::sqrt(x * x + y * y + z * z);
                if (length == 0.0)
                {
                    return false;
                }
                rotation.m[row][0] = float(x / length);
                rotation.m[row][1] = float(y / length);
                rotation.m[row][2] = float(z / length);
            }
            const auto candidate = Math::QuaternionUtils::Conjugate(Math::QuaternionUtils::FromRotationMatrix(rotation));
            if (!IsValidRotation(candidate))
            {
                return false;
            }
            outRotation = NormalizeRotation(candidate);
            return true;
        }

        bool IsValidPath(const LocalPose& pose, const SkeletonPoseRuntime& skeleton, int32_t joint)
        {
            const size_t count = pose.size();
            if (count != skeleton.Parents.size() || joint < 0 || static_cast<size_t>(joint) >= count)
            {
                return false;
            }
            size_t visited = 0;
            while (joint != -1)
            {
                if (joint < 0 || static_cast<size_t>(joint) >= count || visited >= count ||
                    !IsValidRotation(pose[static_cast<size_t>(joint)].Rotation))
                {
                    return false;
                }
                ++visited;
                joint = skeleton.Parents[static_cast<size_t>(joint)];
            }
            return true;
        }

        // 経路の添字と回転、根の回転が検証済みであることを前提にする。
        Math::Quaternion ComputeGlobalRotation(const LocalPose& pose, const SkeletonPoseRuntime& skeleton,
                                               int32_t joint, const Math::Quaternion& rootRotation)
        {
            Math::Quaternion result = Math::Quaternion::Identity;
            while (joint != -1)
            {
                const size_t index = static_cast<size_t>(joint);
                result = NormalizeRotation(NormalizeRotation(pose[index].Rotation) * result);
                joint = skeleton.Parents[index];
            }
            return NormalizeRotation(rootRotation * result);
        }
    } // namespace

    bool TryComputeJointGlobalRotation(const LocalPose& pose, const SkeletonPoseRuntime& skeleton, int32_t joint,
                                       Math::Quaternion& outGlobal)
    {
        Math::Quaternion rootRotation;
        if (!IsValidPath(pose, skeleton, joint) || !TryGetRootRotation(skeleton, rootRotation))
        {
            return false;
        }
        outGlobal = ComputeGlobalRotation(pose, skeleton, joint, rootRotation);
        return true;
    }

    bool TrySetJointGlobalRotation(LocalPose& pose, const SkeletonPoseRuntime& skeleton, int32_t joint,
                                   const Math::Quaternion& newGlobal)
    {
        Math::Quaternion rootRotation;
        if (!IsValidRotation(newGlobal) || !IsValidPath(pose, skeleton, joint) ||
            !TryGetRootRotation(skeleton, rootRotation))
        {
            return false;
        }
        const int32_t parent = skeleton.Parents[static_cast<size_t>(joint)];
        const auto parentGlobal = ComputeGlobalRotation(pose, skeleton, parent, rootRotation);
        const auto local = NormalizeRotation(Math::QuaternionUtils::Conjugate(parentGlobal) *
                                             NormalizeRotation(newGlobal));
        pose[static_cast<size_t>(joint)].Rotation = local;
        return true;
    }

    bool TryDistributeModelRotation(LocalPose& pose, const SkeletonPoseRuntime& skeleton,
                                    Container::Span<const JointRotationShare> chain,
                                    const Math::Quaternion& modelRotation)
    {
        const size_t count = pose.size();
        if (count != skeleton.Parents.size() || chain.empty() || chain.data() == nullptr || chain.size() > count ||
            !IsValidRotation(modelRotation))
        {
            return false;
        }
        double totalWeight = 0.0;
        for (const auto& share : chain)
        {
            if (share.Joint < 0 || static_cast<size_t>(share.Joint) >= count || !std::isfinite(share.Weight) ||
                share.Weight < 0.0f)
            {
                return false;
            }
            totalWeight += double(share.Weight);
        }
        Math::Quaternion rootRotation;
        if (std::abs(totalWeight - 1.0) > 1e-4 || !IsValidPath(pose, skeleton, chain.back().Joint) ||
            !TryGetRootRotation(skeleton, rootRotation))
        {
            return false;
        }

        // 先端から検証すると、次に辿る経路も検証済みの先祖経路に含まれる。
        for (size_t i = chain.size() - 1; i > 0; --i)
        {
            int32_t ancestor = skeleton.Parents[static_cast<size_t>(chain[i].Joint)];
            while (ancestor != -1 && ancestor != chain[i - 1].Joint)
            {
                ancestor = skeleton.Parents[static_cast<size_t>(ancestor)];
            }
            if (ancestor == -1)
            {
                return false;
            }
        }

        auto rotation = NormalizeRotation(modelRotation);
        if (rotation.w < 0.0f)
        {
            rotation = {-rotation.x, -rotation.y, -rotation.z, -rotation.w};
        }
        const double axisLength = std::sqrt(double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                            double(rotation.z) * rotation.z);
        if (axisLength == 0.0)
        {
            return true;
        }
        const double halfAngle = std::atan2(axisLength, double(rotation.w));
        const double axisX = rotation.x / axisLength;
        const double axisY = rotation.y / axisLength;
        const double axisZ = rotation.z / axisLength;

        // 入力と全経路の検証後は、単位四元数同士の有限な演算だけを行う。
        for (const auto& share : chain)
        {
            if (share.Weight == 0.0f)
            {
                continue;
            }
            const double angle = halfAngle * double(share.Weight);
            const double sine = std::sin(angle);
            const Math::Quaternion part(float(axisX * sine), float(axisY * sine), float(axisZ * sine),
                                        float(std::cos(angle)));
            const size_t index = static_cast<size_t>(share.Joint);
            const int32_t parent = skeleton.Parents[index];
            const auto parentGlobal = ComputeGlobalRotation(pose, skeleton, parent, rootRotation);
            const auto global = NormalizeRotation(parentGlobal * NormalizeRotation(pose[index].Rotation));
            const auto newGlobal = NormalizeRotation(part * global);
            pose[index].Rotation = NormalizeRotation(Math::QuaternionUtils::Conjugate(parentGlobal) * newGlobal);
        }
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
