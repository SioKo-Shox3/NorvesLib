#include "CookRigPayload.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    bool InspectCookedRigPayload(Core::Container::AnsiStringView format, Core::Container::Span<const uint8_t> bytes,
                                 uint32_t profileValue, Core::Asset::AssetRigSplitMetadata& out,
                                 Core::Container::AnsiString& error)
    {
        namespace S = Core::Skeletal;
        using View = Core::Container::AnsiStringView;
        const auto profile = S::RigImportProfile(profileValue);
        S::RigV1Limits limits;
        limits.MaxJoints = profileValue == 3 ? 256 : 128;
        S::RigV1Report report;
        Core::Asset::AssetRigSplitMetadata m;
        m.Profile = profileValue;
        if (format == View("nvskel.v1.skeleton"))
        {
            S::SkeletonV1 value;
            if (!S::ParseSkeletonV1(bytes, value, report, limits, profile))
            {
                error = "rig_skeleton_payload";
                return false;
            }
            const auto& data = *value.GetData();
            m.Role = 1;
            m.JointCount = uint32_t(data.Topology.Joints.size());
            m.SkeletonId = data.Topology.SkeletonId;
        }
        else if (format == View("nvskel.v1.skinmesh.pnujiw.u32"))
        {
            S::SkinMeshV1 value;
            if (!S::ParseSkinMeshV1(bytes, value, report, limits, profile))
            {
                error = "rig_skinmesh_payload";
                return false;
            }
            const auto& data = *value.GetData();
            m.Role = 2;
            m.JointCount = uint32_t(data.Topology.Joints.size());
            m.SkeletonId = data.Topology.SkeletonId;
            m.VertexCount = uint32_t(data.Vertices.size());
            m.IndexCount = uint32_t(data.Indices.size());
            m.SubmeshCount = uint32_t(data.SubMeshes.size());
            m.MaterialSlotCount = uint32_t(data.Slots.size());
            m.MaterialCount = uint32_t(data.Materials.size());
        }
        else if (format == View("nvskel.v1.clips"))
        {
            S::ClipBankV1 value;
            if (!S::ParseClipBankV1(bytes, value, report, limits, profile))
            {
                error = "rig_clipbank_payload";
                return false;
            }
            const auto& data = *value.GetData();
            m.Role = 3;
            m.JointCount = uint32_t(data.Topology.Joints.size());
            m.SkeletonId = data.Topology.SkeletonId;
            m.ClipCount = uint32_t(data.Clips.size());
            m.SnapshotCount = uint32_t(data.Snapshots.size());
            for (const auto& clip : data.Clips)
            {
                m.ChannelCount += uint32_t(clip.Channels.size());
                m.SampleCount += uint32_t(clip.RootMotion.size());
                for (const auto& channel : clip.Channels)
                {
                    m.SampleCount += uint32_t(channel.Samples.size());
                }
            }
        }
        else
        {
            error = "rig_format_unsupported";
            return false;
        }
        out = m;
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
