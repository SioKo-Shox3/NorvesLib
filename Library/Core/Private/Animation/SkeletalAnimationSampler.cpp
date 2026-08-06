#include "Animation/SkeletalAnimationSampler.h"

#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Math/MatrixUtils.h"
#include "Math/Quaternion.h"
#include "Math/QuaternionUtils.h"
#include "Math/Vector4.h"
#include "Resource/SkinnedMeshResource.h"

#include <cmath>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        struct JointTransform
        {
            Math::Vector3 Translation = Math::Vector3::Zero;
            Math::Quaternion Rotation = Math::Quaternion::Identity;
            Math::Vector3 Scale = Math::Vector3::One;
        };

        Math::Matrix4x4 LoadMatrix(const Container::FixedArray<float, 16>& values)
        {
            return Math::Matrix4x4(
                values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11],
                values[12], values[13], values[14], values[15]);
        }

        Math::Quaternion NormalizeQuaternion(const Math::Quaternion& value)
        {
            const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
            if (lengthSquared <= Math::Constants::EPSILON)
            {
                return Math::Quaternion::Identity;
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            return Math::Quaternion(
                value.x * inverseLength,
                value.y * inverseLength,
                value.z * inverseLength,
                value.w * inverseLength);
        }

        Math::Quaternion Slerp(const Math::Quaternion& start, const Math::Quaternion& end, float alpha)
        {
            Math::Quaternion from = NormalizeQuaternion(start);
            Math::Quaternion to = NormalizeQuaternion(end);
            float dot = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
            if (dot < 0.0f)
            {
                to = Math::Quaternion(-to.x, -to.y, -to.z, -to.w);
                dot = -dot;
            }
            if (dot > 0.9995f)
            {
                return NormalizeQuaternion(Math::Quaternion(
                    from.x + (to.x - from.x) * alpha,
                    from.y + (to.y - from.y) * alpha,
                    from.z + (to.z - from.z) * alpha,
                    from.w + (to.w - from.w) * alpha));
            }

            dot = std::fmax(-1.0f, std::fmin(1.0f, dot));
            const float theta = std::acos(dot);
            const float sinTheta = std::sin(theta);
            const float fromWeight = std::sin((1.0f - alpha) * theta) / sinTheta;
            const float toWeight = std::sin(alpha * theta) / sinTheta;
            return NormalizeQuaternion(Math::Quaternion(
                from.x * fromWeight + to.x * toWeight,
                from.y * fromWeight + to.y * toWeight,
                from.z * fromWeight + to.z * toWeight,
                from.w * fromWeight + to.w * toWeight));
        }

        Skeletal::SkeletalValue SampleChannelValue(const Skeletal::SkeletalAnimationChannel& channel,
                                                   float timeSeconds)
        {
            const auto& samples = channel.Samples;
            if (samples.empty())
            {
                return {};
            }
            if (samples.size() == 1 || timeSeconds <= samples.front().TimeSeconds)
            {
                return samples.front().Value;
            }
            if (timeSeconds >= samples.back().TimeSeconds)
            {
                return samples.back().Value;
            }

            for (size_t sampleIndex = 1; sampleIndex < samples.size(); ++sampleIndex)
            {
                const auto& next = samples[sampleIndex];
                if (timeSeconds > next.TimeSeconds)
                {
                    continue;
                }

                const auto& previous = samples[sampleIndex - 1];
                if (channel.Interpolation == Skeletal::SkeletalAnimationInterpolation::Step)
                {
                    return timeSeconds == next.TimeSeconds ? next.Value : previous.Value;
                }
                const float duration = next.TimeSeconds - previous.TimeSeconds;
                const float alpha = duration > Math::Constants::EPSILON
                    ? (timeSeconds - previous.TimeSeconds) / duration
                    : 0.0f;
                if (channel.Path == Skeletal::SkeletalAnimationPath::Rotation)
                {
                    const Math::Quaternion rotation = Slerp(
                        Math::Quaternion(previous.Value.X, previous.Value.Y, previous.Value.Z, previous.Value.W),
                        Math::Quaternion(next.Value.X, next.Value.Y, next.Value.Z, next.Value.W),
                        alpha);
                    return {rotation.x, rotation.y, rotation.z, rotation.w};
                }
                return {
                    previous.Value.X + (next.Value.X - previous.Value.X) * alpha,
                    previous.Value.Y + (next.Value.Y - previous.Value.Y) * alpha,
                    previous.Value.Z + (next.Value.Z - previous.Value.Z) * alpha,
                    previous.Value.W + (next.Value.W - previous.Value.W) * alpha};
            }
            return samples.back().Value;
        }

        JointTransform DecomposeRowTransform(const Math::Matrix4x4& matrix)
        {
            JointTransform result;
            result.Translation = matrix.GetTranslationRow();
            result.Scale = Math::MatrixUtils::ExtractScale(matrix);
            const Math::Matrix4x4 rotation =
                Math::MatrixUtils::ExtractRotationRowVector(matrix, result.Scale);
            result.Rotation = NormalizeQuaternion(Math::QuaternionUtils::FromRotationMatrix(rotation));
            return result;
        }

        bool ValidateParentChain(size_t jointIndex,
                                 const Container::VariableArray<Skeletal::SkeletalJoint>& joints,
                                 Container::VariableArray<uint8_t>& visitState)
        {
            if (visitState[jointIndex] == 2)
            {
                return true;
            }
            if (visitState[jointIndex] == 1)
            {
                return false;
            }

            visitState[jointIndex] = 1;
            const int32_t parentIndex = joints[jointIndex].ParentIndex;
            if (parentIndex >= 0 &&
                !ValidateParentChain(static_cast<size_t>(parentIndex), joints, visitState))
            {
                return false;
            }
            visitState[jointIndex] = 2;
            return true;
        }

        bool ValidateParentHierarchy(const Container::VariableArray<Skeletal::SkeletalJoint>& joints)
        {
            const size_t jointCount = joints.size();
            for (const Skeletal::SkeletalJoint& joint : joints)
            {
                if (joint.ParentIndex < -1 ||
                    (joint.ParentIndex >= 0 && static_cast<size_t>(joint.ParentIndex) >= jointCount))
                {
                    return false;
                }
            }

            Container::VariableArray<uint8_t> visitState(jointCount, 0);
            for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                if (!ValidateParentChain(jointIndex, joints, visitState))
                {
                    return false;
                }
            }
            return true;
        }

        bool BuildJointGlobal(uint32_t jointIndex,
                              const Container::VariableArray<Skeletal::SkeletalJoint>& joints,
                              const Container::VariableArray<Math::Matrix4x4>& localMatrices,
                              Container::VariableArray<Math::Matrix4x4>& globalMatrices,
                              Container::VariableArray<uint8_t>& visitState)
        {
            if (visitState[jointIndex] == 2)
            {
                return true;
            }
            if (visitState[jointIndex] == 1)
            {
                return false;
            }
            visitState[jointIndex] = 1;

            const int32_t parentIndex = joints[jointIndex].ParentIndex;
            if (parentIndex >= 0)
            {
                const uint32_t parent = static_cast<uint32_t>(parentIndex);
                if (parent >= joints.size() ||
                    !BuildJointGlobal(parent, joints, localMatrices, globalMatrices, visitState))
                {
                    return false;
                }
                globalMatrices[jointIndex] = localMatrices[jointIndex] * globalMatrices[parent];
            }
            else
            {
                globalMatrices[jointIndex] = localMatrices[jointIndex];
            }
            visitState[jointIndex] = 2;
            return true;
        }
    } // namespace

    bool SkeletalAnimationSampler::Sample(const SkeletonResource& skeleton,
                                          const AnimationClipResource& clip,
                                          const SkinnedMeshResource& mesh,
                                          float timeSeconds,
                                          const Math::Matrix4x4& meshNodeGlobalRow,
                                          SkeletalPoseSnapshot& outPose)
    {
        outPose.Clear();
        const auto& joints = skeleton.GetJoints();
        if (joints.empty())
        {
            return false;
        }

        const size_t jointCount = joints.size();
        if (!ValidateParentHierarchy(joints))
        {
            return false;
        }
        Container::VariableArray<Math::Matrix4x4> inverseBindMatrices(jointCount);
        Container::VariableArray<Math::Matrix4x4> bindGlobals(jointCount);
        Container::VariableArray<Math::Matrix4x4> localMatrices(jointCount);
        Container::VariableArray<Math::Matrix4x4> jointGlobals(jointCount);
        Container::VariableArray<JointTransform> localTransforms(jointCount);
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            inverseBindMatrices[jointIndex] = LoadMatrix(joints[jointIndex].InverseBindMatrix);
            bindGlobals[jointIndex] = Math::MatrixUtils::Inverse(inverseBindMatrices[jointIndex]) * meshNodeGlobalRow;
        }
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            const int32_t parentIndex = joints[jointIndex].ParentIndex;
            const Math::Matrix4x4 bindLocal = parentIndex >= 0
                ? bindGlobals[jointIndex] * Math::MatrixUtils::Inverse(bindGlobals[static_cast<size_t>(parentIndex)])
                : bindGlobals[jointIndex];
            localTransforms[jointIndex] = DecomposeRowTransform(bindLocal);
        }

        const auto& clipData = clip.GetClip();
        const float sampleTime = std::fmax(0.0f, std::fmin(timeSeconds, clipData.DurationSeconds));
        for (const auto& channel : clipData.Channels)
        {
            if (channel.JointIndex >= jointCount || channel.Samples.empty())
            {
                continue;
            }
            const Skeletal::SkeletalValue value = SampleChannelValue(channel, sampleTime);
            JointTransform& transform = localTransforms[channel.JointIndex];
            switch (channel.Path)
            {
            case Skeletal::SkeletalAnimationPath::Translation:
                transform.Translation = Math::Vector3(value.X, value.Y, value.Z);
                break;
            case Skeletal::SkeletalAnimationPath::Rotation:
                transform.Rotation = NormalizeQuaternion(Math::Quaternion(value.X, value.Y, value.Z, value.W));
                break;
            case Skeletal::SkeletalAnimationPath::Scale:
                transform.Scale = Math::Vector3(value.X, value.Y, value.Z);
                break;
            }
        }
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            const JointTransform& transform = localTransforms[jointIndex];
            localMatrices[jointIndex] = Math::MatrixUtils::CreateWorldRowVector(
                transform.Translation, transform.Rotation, transform.Scale);
        }

        Container::VariableArray<uint8_t> visitState(jointCount, 0);
        for (uint32_t jointIndex = 0; jointIndex < static_cast<uint32_t>(jointCount); ++jointIndex)
        {
            if (!BuildJointGlobal(jointIndex, joints, localMatrices, jointGlobals, visitState))
            {
                outPose.Clear();
                return false;
            }
        }

        const Math::Matrix4x4 inverseMeshNodeGlobal = Math::MatrixUtils::Inverse(meshNodeGlobalRow);
        outPose.BonePalette.resize(jointCount);
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            outPose.BonePalette[jointIndex] =
                inverseBindMatrices[jointIndex] * jointGlobals[jointIndex] * inverseMeshNodeGlobal;
        }

        const auto& vertices = mesh.GetVertices();
        if (!vertices.empty())
        {
            outPose.AnimatedBounds = Math::AABB::CreateInvalid();
            for (const auto& vertex : vertices)
            {
                outPose.AnimatedBounds.Expand(SkinVertex(vertex, outPose.BonePalette).Position);
            }
            outPose.bHasAnimatedBounds = true;
        }
        return true;
    }

    SkinnedVertexSample SkeletalAnimationSampler::SkinVertex(
        const Skeletal::SkeletalVertex& vertex,
        const Container::VariableArray<Math::Matrix4x4>& bonePalette)
    {
        const Math::Vector3 sourcePosition(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
        const Math::Vector3 sourceNormal(vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z);
        Math::Vector3 position = Math::Vector3::Zero;
        Math::Vector3 normal = Math::Vector3::Zero;
        float totalWeight = 0.0f;
        for (uint32_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
        {
            const float weight = vertex.JointWeights[influenceIndex];
            const uint32_t jointIndex = vertex.JointIndices[influenceIndex];
            if (weight <= 0.0f || jointIndex >= bonePalette.size())
            {
                continue;
            }
            const Math::Matrix4x4& palette = bonePalette[jointIndex];
            position += Math::MatrixUtils::TransformPointRowVector(palette, sourcePosition) * weight;
            const Math::Matrix4x4 normalMatrix = Math::MatrixUtils::CreateNormalMatrix(palette);
            normal += Math::MatrixUtils::TransformVectorRowVector(normalMatrix, sourceNormal) * weight;
            totalWeight += weight;
        }
        if (totalWeight <= Math::Constants::EPSILON)
        {
            return {sourcePosition, sourceNormal};
        }
        position /= totalWeight;
        normal /= totalWeight;
        if (normal.LengthSquared() > Math::Constants::EPSILON)
        {
            normal.Normalize();
        }
        return {position, normal};
    }
} // namespace NorvesLib::Core::Animation
