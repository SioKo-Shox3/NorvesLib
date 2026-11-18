// humanoid_v1 のロール候補をスキンのパレットの関節番号へ解決する。
#pragma once

#include "Rendering/GpuParticle/GpuSkinEmitter.h"
#include "Animation/RigV1Types.h"
#include "Animation/SkeletalRoleProfile.h"

namespace NorvesLib::Core::Rendering
{
    static_assert(kGpuSkinHumanoidRoleCount == static_cast<uint32_t>(Animation::SkeletalHumanoidRole::Count));

    /// joints は SkinMeshV1Data::Topology.Joints を渡す（正準順）。
    /// CookedSkinMeshV1 は JointIndices と逆バインド行列を同じ正準順へ写すため、添字がパレットと一致する。
    /// 候補名を順に探し、最初に一致した関節を採る。人型以外では out を変更しない。
    inline bool BuildGpuSkinHumanoidJointTable(const Animation::SkeletalRoleProfile& profile,
        Container::Span<const Skeletal::RigTopologyJoint> joints, GpuSkinHumanoidJointTable& out)
    {
        if (profile.Vocabulary != Animation::SkeletalRoleVocabulary::Humanoid)
        {
            return false;
        }
        GpuSkinHumanoidJointTable table;
        for (uint32_t role = 0; role < kGpuSkinHumanoidRoleCount; ++role)
        {
            for (const auto& candidate : profile.Target[role])
            {
                for (size_t joint = 0; joint < joints.size() && joint < kGpuSkinNoBone; ++joint)
                {
                    const auto& name = joints[joint].Name;
                    if (name.size() != candidate.Name.size())
                    {
                        continue;
                    }
                    bool matches = true;
                    for (size_t byte = 0; byte < candidate.Name.size(); ++byte)
                    {
                        if (static_cast<uint8_t>(name[byte]) != candidate.Name[byte])
                        {
                            matches = false;
                            break;
                        }
                    }
                    if (matches)
                    {
                        table.Joints[role] = static_cast<uint32_t>(joint);
                        break;
                    }
                }
                if (table.Joints[role] != kGpuSkinNoBone)
                {
                    break;
                }
            }
        }
        out = table;
        return true;
    }
} // 名前空間 NorvesLib::Core::Rendering
