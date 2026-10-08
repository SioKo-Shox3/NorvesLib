#include "Rendering/SkinnedDrawCommands.h"
#include "Resource/SkeletalLimits.h"

namespace NorvesLib::Core::Rendering
{
    CommandRange AppendSkinnedDrawCommands(FramePacket* packet, const Container::VariableArray<SkinnedMeshProxy>& proxies)
    {
        CommandRange range;
        if (!packet || proxies.empty() || packet->DrawCommands.size() > UINT32_MAX)
        {
            return range;
        }
        range.First = static_cast<uint32_t>(packet->DrawCommands.size());
        for (const auto& proxy : proxies)
        {
            if (!proxy.IsValid() || proxy.MaterialCount > MAX_MATERIAL_SLOTS)
            {
                continue;
            }
            Container::TSharedPtr<const SkinnedMeshAssetLease> asset = proxy.AssetLease.lock();
            if (!asset || asset->GetHandle() != proxy.MeshHandle || !asset->HasValidRenderData())
            {
                continue;
            }
            const auto& submeshes = asset->GetSubMeshes();
            const uint32_t count = submeshes.empty() ? 1u : static_cast<uint32_t>(submeshes.size());
            if (packet->SkinnedMeshFrameLeases.size() >= UINT32_MAX || packet->DrawCommands.size() > UINT32_MAX - count)
            {
                continue;
            }
            auto frame = Container::MakeShared<SkinnedMeshFrameLease>(asset,proxy.ComponentId);
            if (!frame || !frame->IsValid())
            {
                continue;
            }
            MaterialHandle fallback = proxy.Material;
            if (proxy.MaterialCount != 0 && proxy.Materials[0].IsValid())
            {
                fallback = proxy.Materials[0];
            }
            const uint32_t frameIndex = static_cast<uint32_t>(packet->SkinnedMeshFrameLeases.size());
            packet->SkinnedMeshFrameLeases.push_back(frame);
            for (uint32_t index = 0; index < count; ++index)
            {
                const Skeletal::SkeletalSubMesh implicit{0,static_cast<uint32_t>(asset->GetIndices().size()),0};
                const auto& submesh = submeshes.empty() ? implicit : submeshes[index];
                const MaterialHandle material = submesh.MaterialSlot < proxy.MaterialCount &&
                    proxy.Materials[submesh.MaterialSlot].IsValid() ? proxy.Materials[submesh.MaterialSlot] : fallback;
                DrawCommand command = DrawCommand::CreateDrawIndexed();
                command.Draw.PayloadKind = DrawPayloadKind::Skinned;
                command.Draw.SubMeshIndex = index;
                command.Draw.IndexOffset = submesh.IndexStart;
                command.Draw.IndexCount = submesh.IndexCount;
                command.Draw.VertexOffset = 0;
                command.Draw.MaterialIndex = submesh.MaterialSlot;
                command.Draw.MaterialHandle = material;
                command.Draw.MaterialBlendMode = BlendMode::Opaque;
                command.Draw.ObjectId = proxy.ObjectId;
                command.Draw.SourceMeshComponentId = proxy.ComponentId;
                command.Draw.WorldMatrix = proxy.WorldTransform;
                command.Draw.InstanceCount = 1;
                command.Draw.FirstInstance = 0;
                command.Draw.bInstanced = false;
                command.Draw.bCastShadow = proxy.bCastShadow && !submesh.bNoShadow;
                command.Skinned.FrameLeaseIndex = frameIndex;
                command.Skinned.BonePalette = proxy.BonePalette;
                packet->DrawCommands.push_back(std::move(command));
                ++range.Count;
            }
        }
        return range;
    }
} // namespace NorvesLib::Core::Rendering
