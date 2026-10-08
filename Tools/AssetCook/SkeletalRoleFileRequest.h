#pragma once
#include "SkeletalRoleProfileCook.h"

namespace NorvesLib::Tools::AssetCook
{
    // file要求はlocatorと値を所有する。借用bytesは保存しない。
    struct SkeletalRoleFileRequest
    {
        bool bEnabled = false;
        std::filesystem::path BvhPath, ProfilePath;
        Core::Container::String ClipName;
        SkeletalBvhClipOperation Operation = SkeletalBvhClipOperation::Unspecified;
        Core::Animation::SkeletalRoleProfileLimits ProfileLimits;
        Core::Animation::SkeletalBvhClipLimits ClipLimits;
        Core::Animation::SkeletalJointMappingLimits MappingLimits;
        Core::Bvh::BvhDecodeLimits DecodeLimits;
        uint64_t MaxNvskelBytes = uint64_t{128} << 20;
    };
    [[nodiscard]] inline bool HasSkeletalRoleFileRequest(const SkeletalRoleFileRequest& r) noexcept
    {
        return r.bEnabled || !r.BvhPath.empty() || !r.ProfilePath.empty() || !r.ClipName.empty() ||
               r.Operation != SkeletalBvhClipOperation::Unspecified;
    }
} // namespace NorvesLib::Tools::AssetCook
