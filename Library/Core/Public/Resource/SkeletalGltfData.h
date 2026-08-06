#pragma once

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    struct SkeletalPosition
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };

    struct SkeletalTexCoord
    {
        float U = 0.0f;
        float V = 0.0f;
    };

    struct SkeletalValue
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 0.0f;
    };

    struct SkeletalVertex
    {
        SkeletalPosition Position;
        SkeletalPosition Normal;
        SkeletalTexCoord TexCoord;
        Container::FixedArray<uint32_t, 4> JointIndices{};
        Container::FixedArray<float, 4> JointWeights{};
    };

    struct SkeletalJoint
    {
        Container::String Name;
        int32_t ParentIndex = -1;
        Container::FixedArray<float, 16> InverseBindMatrix{};
    };

    enum class SkeletalAnimationPath : uint32_t
    {
        Translation = 0,
        Rotation = 1,
        Scale = 2
    };

    enum class SkeletalAnimationInterpolation : uint32_t
    {
        Linear = 0,
        Step = 1
    };

    struct SkeletalAnimationSample
    {
        float TimeSeconds = 0.0f;
        SkeletalValue Value;
    };

    struct SkeletalAnimationChannel
    {
        uint32_t JointIndex = 0;
        SkeletalAnimationPath Path = SkeletalAnimationPath::Translation;
        SkeletalAnimationInterpolation Interpolation = SkeletalAnimationInterpolation::Linear;
        Container::VariableArray<SkeletalAnimationSample> Samples;
    };

    struct SkeletalAnimationClip
    {
        Container::String Name;
        float DurationSeconds = 0.0f;
        Container::VariableArray<SkeletalAnimationChannel> Channels;
    };

    struct SkeletalGltfData
    {
        Container::FixedArray<float, 16> MeshNodeGlobalTransform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
        Container::VariableArray<SkeletalVertex> Vertices;
        Container::VariableArray<uint32_t> Indices;
        Container::VariableArray<SkeletalJoint> Joints;
        Container::VariableArray<SkeletalAnimationClip> Clips;
    };

    enum class SkeletalGltfDecodeStatus : uint8_t
    {
        Success,
        FileReadFailed,
        InvalidJson,
        InvalidDocument,
        InvalidAccessor,
        UnsupportedMeshCount,
        UnsupportedPrimitiveCount,
        UnsupportedSkinCount,
        UnsupportedClipCount,
        UnsupportedInterpolation,
        UnsupportedMorphTargets,
        JointLimitExceeded,
        InvalidSkeleton,
        InvalidAnimation,
        UnsupportedSparseAccessor
    };

    struct SkeletalGltfDecodeResult
    {
        SkeletalGltfDecodeStatus Status = SkeletalGltfDecodeStatus::InvalidDocument;
        SkeletalGltfData Data;

        [[nodiscard]] bool Succeeded() const
        {
            return Status == SkeletalGltfDecodeStatus::Success;
        }
    };
} // namespace NorvesLib::Core::Skeletal
