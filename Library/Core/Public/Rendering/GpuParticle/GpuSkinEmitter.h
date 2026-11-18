// スキンの表面と骨の放出設定を、GPU の定数へ解決する。
#pragma once

#include "Rendering/GpuParticle/GpuParticleTypes.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    /// スキンの表面の標本を受け入れる方式。
    enum class GpuSkinAcceptMode : uint32_t
    {
        Uniform = 0,
        DissolveEdge = 1,
        BoneMask = 2
    };

    static_assert(static_cast<uint32_t>(GpuSkinAcceptMode::Uniform) == kGpuSkinAcceptUniform);
    static_assert(static_cast<uint32_t>(GpuSkinAcceptMode::DissolveEdge) == kGpuSkinAcceptDissolveEdge);
    static_assert(static_cast<uint32_t>(GpuSkinAcceptMode::BoneMask) == kGpuSkinAcceptBoneMask);

    /// 骨を選ぶデータの種類。
    enum class GpuSkinBoneSelectKind : uint8_t
    {
        None,
        HumanoidRole,
        BoneGroup
    };

    /// humanoid_v1 のロールの数。
    inline constexpr uint32_t kGpuSkinHumanoidRoleCount = 22u;
    /// 骨の組で表現できる骨の数。
    inline constexpr uint32_t kGpuSkinMaxBones = 128u;

    /// ロールの値は SkeletalHumanoidRole の順。骨の組は頂点の JointIndices と同じ添字。
    struct GpuSkinBoneSelector
    {
        GpuSkinBoneSelectKind Kind = GpuSkinBoneSelectKind::None;
        uint32_t HumanoidRole = 0;
        uint32_t BoneGroup[4]{};
    };

    /// ロールをパレットの関節番号へ対応付ける表。対応なしは kGpuSkinNoBone。
    struct GpuSkinHumanoidJointTable
    {
        uint32_t Joints[kGpuSkinHumanoidRoleCount];

        constexpr GpuSkinHumanoidJointTable()
        {
            for (uint32_t& joint : Joints)
            {
                joint = kGpuSkinNoBone;
            }
        }
    };

    /// スキンの表面からの放出設定。
    struct GpuSkinSurfaceEmitterDesc
    {
        float NormalSpeed = 0.0f;
        float VelocityInheritance = 0.0f;
        GpuSkinAcceptMode Accept = GpuSkinAcceptMode::Uniform;
        GpuSkinBoneSelector Mask;
    };

    /// 骨のバインド位置からの放出設定。
    struct GpuBoneEmitterDesc
    {
        GpuSkinBoneSelector Bone;
        float BindPosition[3]{};
        float VelocityInheritance = 0.0f;
    };

    /// 関節数と 128 bit の範囲内の骨だけを残す。空の結果は false とし、出力をゼロにする。
    inline bool ResolveGpuSkinBoneMask(const GpuSkinBoneSelector& selector,
        const GpuSkinHumanoidJointTable& table, uint32_t jointCount, uint32_t outMask[4])
    {
        uint32_t mask[4]{};
        const uint32_t count = jointCount < kGpuSkinMaxBones ? jointCount : kGpuSkinMaxBones;
        if (selector.Kind == GpuSkinBoneSelectKind::HumanoidRole &&
            selector.HumanoidRole < kGpuSkinHumanoidRoleCount)
        {
            const uint32_t joint = table.Joints[selector.HumanoidRole];
            if (joint < count)
            {
                mask[joint / 32u] = 1u << (joint % 32u);
            }
        }
        else if (selector.Kind == GpuSkinBoneSelectKind::BoneGroup)
        {
            for (uint32_t joint = 0; joint < count; ++joint)
            {
                mask[joint / 32u] |= selector.BoneGroup[joint / 32u] & (1u << (joint % 32u));
            }
        }
        uint32_t combined = 0;
        for (uint32_t word = 0; word < 4u; ++word)
        {
            outMask[word] = mask[word];
            combined |= mask[word];
        }
        return combined != 0u;
    }

    /// 表面からの放出設定を適用する。不正な入力では inOut を変更しない。
    inline bool ApplyGpuSkinSurfaceEmitter(const GpuSkinSurfaceEmitterDesc& desc,
        const GpuSkinHumanoidJointTable& table, uint32_t jointCount, GpuEmitterParams& inOut)
    {
        if (!std::isfinite(desc.NormalSpeed) || !std::isfinite(desc.VelocityInheritance) ||
            static_cast<uint32_t>(desc.Accept) > kGpuSkinAcceptBoneMask)
        {
            return false;
        }
        uint32_t mask[4]{};
        const bool hasMask = ResolveGpuSkinBoneMask(desc.Mask, table, jointCount, mask);
        if (desc.Accept == GpuSkinAcceptMode::BoneMask && !hasMask)
        {
            return false;
        }

        inOut.Flags = (inOut.Flags & ~(kGpuEmitterShapeMask << kGpuEmitterShapeShift)) |
            kGpuEmitterFlagShape | (kGpuEmitterShapeSkinSurface << kGpuEmitterShapeShift);
        for (float& position : inOut.SkinBindPosition)
        {
            position = 0.0f;
        }
        inOut.SkinBoneIndex = kGpuSkinNoBone;
        inOut.SkinNormalSpeed = desc.NormalSpeed;
        inOut.SkinVelocityInheritance = desc.VelocityInheritance;
        inOut.SkinAcceptMode = static_cast<uint32_t>(desc.Accept);
        inOut.SkinSourceIndex = kGpuSkinSourceNone;
        for (uint32_t word = 0; word < 4u; ++word)
        {
            inOut.SkinBoneMask[word] = mask[word];
        }
        return true;
    }

    /// 骨からの放出設定を適用する。不正な入力や空の選択では inOut を変更しない。
    inline bool ApplyGpuBoneEmitter(const GpuBoneEmitterDesc& desc,
        const GpuSkinHumanoidJointTable& table, uint32_t jointCount, GpuEmitterParams& inOut)
    {
        if (!std::isfinite(desc.VelocityInheritance))
        {
            return false;
        }
        for (float position : desc.BindPosition)
        {
            if (!std::isfinite(position))
            {
                return false;
            }
        }
        uint32_t mask[4]{};
        uint32_t boneIndex = kGpuSkinNoBone;
        if (desc.Bone.Kind == GpuSkinBoneSelectKind::HumanoidRole)
        {
            if (desc.Bone.HumanoidRole >= kGpuSkinHumanoidRoleCount)
            {
                return false;
            }
            boneIndex = table.Joints[desc.Bone.HumanoidRole];
            if (boneIndex == kGpuSkinNoBone || boneIndex >= jointCount)
            {
                return false;
            }
        }
        else if (desc.Bone.Kind != GpuSkinBoneSelectKind::BoneGroup ||
            !ResolveGpuSkinBoneMask(desc.Bone, table, jointCount, mask))
        {
            return false;
        }

        inOut.Flags = (inOut.Flags & ~(kGpuEmitterShapeMask << kGpuEmitterShapeShift)) |
            kGpuEmitterFlagShape | (kGpuEmitterShapeBone << kGpuEmitterShapeShift);
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            inOut.SkinBindPosition[axis] = desc.BindPosition[axis];
        }
        inOut.SkinBoneIndex = boneIndex;
        inOut.SkinNormalSpeed = 0.0f;
        inOut.SkinVelocityInheritance = desc.VelocityInheritance;
        inOut.SkinAcceptMode = kGpuSkinAcceptUniform;
        inOut.SkinSourceIndex = kGpuSkinSourceNone;
        for (uint32_t word = 0; word < 4u; ++word)
        {
            inOut.SkinBoneMask[word] = mask[word];
        }
        return true;
    }
} // 名前空間 NorvesLib::Core::Rendering
