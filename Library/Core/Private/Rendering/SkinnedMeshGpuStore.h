#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/SkinnedMeshTypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class SkinnedMeshGpuStore final
    {
    public:
        explicit SkinnedMeshGpuStore(Container::TSharedPtr<RHI::IDevice> device);

        void BeginFrame(uint64_t completedSubmissionSerial);
        /**
         * previousBonePalette・previousWorldTransform を与えると、直前のフレームの変換と骨ごとの位置の行列
         * （velocity 用）を別のバッファ（PreviousPaletteBuffer）に載せる。骨の数は bonePalette と同じでなければ
         * ならない。
         */
        bool PrepareDraw(const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease,
                         const Container::VariableArray<Math::Matrix4x4>& bonePalette,
                         const Math::Matrix4x4& worldTransform,
                         SkinnedMeshPreparedDraw& outPrepared,
                         const Container::VariableArray<Math::Matrix4x4>* previousBonePalette = nullptr,
                         const Math::Matrix4x4* previousWorldTransform = nullptr);
        bool MarkLastUse(const SkinnedMeshPreparedDraw& prepared,
                         const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease);
        bool CommitSubmittedFrame(uint64_t submissionSerial);
        void AbortFrame();
        bool GetLifetimeSnapshot(SkinnedMeshHandle handle, SkinnedMeshGpuLifetimeSnapshot& outSnapshot) const;
        bool IsResident(SkinnedMeshHandle handle) const;
        void CollectReleasedResources();
        void ForceClearAfterWaitIdle();

    private:
        struct PaletteUse
        {
            RHI::BufferPtr Buffer;
            // 直前のフレームの変換とパレット（無ければ空）。Buffer と同じ寿命で保つ。
            RHI::BufferPtr PreviousBuffer;
            Container::TWeakPtr<const SkinnedMeshFrameLease> FrameLease;
            uint64_t LastSubmittedSerial = 0;
        };

        struct Entry
        {
            SkinnedMeshHandle Handle;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            uint32_t IndexCount = 0;
            Container::TWeakPtr<const SkinnedMeshAssetLease> AssetLease;
            Container::VariableArray<Container::TWeakPtr<const SkinnedMeshFrameLease>> FrameLeases;
            Container::VariableArray<PaletteUse> PaletteUses;
            uint64_t LastSubmittedSerial = 0;
        };

        struct PendingUse
        {
            SkinnedMeshHandle Handle;
            RHI::BufferPtr PaletteBuffer;
            Container::TWeakPtr<const SkinnedMeshFrameLease> FrameLease;
        };

        Entry* FindOrUpload(const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease);
        void TrackFrameLease(Entry& entry,
                             const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease);
        void CollectReleased();
        bool IsSubmissionComplete(uint64_t submissionSerial) const;

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::Map<SkinnedMeshHandle, Entry> m_Entries;
        Container::VariableArray<PendingUse> m_PendingUses;
        uint64_t m_CompletedSubmissionSerial = 0;
        bool m_bFrameOpen = false;
    };
} // namespace NorvesLib::Core::Rendering
