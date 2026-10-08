// 前姿勢をcomponent・資産世代・不変asset実体へ束縛する描画用履歴。
#pragma once
#include "Rendering/FramePacket.h"
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    class SkinnedPoseHistory final
    {
    public:
        void Reset() { m_States.clear(); }

        void Apply(FramePacket& packet, bool allowHistory) const
        {
            for (DrawCommand& command : packet.DrawCommands)
            {
                if (command.Draw.PayloadKind != DrawPayloadKind::Skinned)
                {
                    continue;
                }
                command.Skinned.PreviousWorldMatrix = command.Draw.WorldMatrix;
                command.Skinned.PreviousBonePalette.clear();
                command.Skinned.bHasPrevious = false;
                const auto asset = ResolveAsset(packet,command);
                if (!allowHistory || !asset)
                {
                    continue;
                }
                const auto found = m_States.find(command.Draw.SourceMeshComponentId);
                if (found == m_States.end())
                {
                    continue;
                }
                const State& state = found->second;
                if (!state.Conflicting && state.Handle == asset->GetHandle() && state.Asset.lock().get() == asset.get() &&
                    state.Bones.size() == command.Skinned.BonePalette.size())
                {
                    command.Skinned.PreviousWorldMatrix = state.World;
                    command.Skinned.PreviousBonePalette = state.Bones;
                    command.Skinned.bHasPrevious = true;
                }
            }
        }

        void Record(const FramePacket& packet)
        {
            m_States.clear();
            for (const DrawCommand& command : packet.DrawCommands)
            {
                const auto asset = ResolveAsset(packet,command);
                if (!asset)
                {
                    continue;
                }
                const uint64_t component = command.Draw.SourceMeshComponentId;
                const auto found = m_States.find(component);
                if (found != m_States.end())
                {
                    // 同componentのsubmesh/viewportは1回だけ保存。異なるposeの混在は次frameへ渡さない。
                    State& state = found->second;
                    state.Conflicting = state.Conflicting || state.Handle != asset->GetHandle() ||
                        state.Asset.lock().get() != asset.get() || !SameMatrix(state.World,command.Draw.WorldMatrix) ||
                        !SameBones(state.Bones,command.Skinned.BonePalette);
                    continue;
                }
                State state;
                state.Handle = asset->GetHandle();
                state.Asset = asset;
                state.World = command.Draw.WorldMatrix;
                state.Bones = command.Skinned.BonePalette;
                m_States.emplace(component,std::move(state));
            }
        }
    private:
        struct State
        {
            SkinnedMeshHandle Handle;
            Container::TWeakPtr<const SkinnedMeshAssetLease> Asset;
            Math::Matrix4x4 World;
            Container::VariableArray<Math::Matrix4x4> Bones;
            bool Conflicting = false;
        };
        static Container::TSharedPtr<const SkinnedMeshAssetLease> ResolveAsset(const FramePacket& packet, const DrawCommand& command)
        {
            if (command.Draw.PayloadKind != DrawPayloadKind::Skinned || command.Draw.SourceMeshComponentId == 0 ||
                command.Skinned.BonePalette.empty() || command.Skinned.FrameLeaseIndex >= packet.SkinnedMeshFrameLeases.size())
            {
                return {};
            }
            const auto& frame = packet.SkinnedMeshFrameLeases[command.Skinned.FrameLeaseIndex];
            if (!frame || !frame->IsValid() || frame->ComponentId != command.Draw.SourceMeshComponentId)
            {
                return {};
            }
            return frame->AssetLease;
        }
        static bool SameMatrix(const Math::Matrix4x4& lhs, const Math::Matrix4x4& rhs)
        {
            return std::memcmp(lhs.values,rhs.values,sizeof(lhs.values)) == 0;
        }
        static bool SameBones(const Container::VariableArray<Math::Matrix4x4>& lhs,
            const Container::VariableArray<Math::Matrix4x4>& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (size_t index=0;index<lhs.size();++index)
            {
                if (!SameMatrix(lhs[index],rhs[index]))
                {
                    return false;
                }
            }
            return true;
        }
        Container::UnorderedMap<uint64_t,State> m_States;
    };
}
