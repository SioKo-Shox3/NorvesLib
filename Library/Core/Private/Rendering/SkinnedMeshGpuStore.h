#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/MeshIndexChunks.h"
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
        // 登録時に分けた128三角形以下の塊。全三角形をちょうど1回ずつ覆う。
        // 未登録のメッシュと、塊に分けられなかったメッシュ（GBuffer・影では描ける）は false で、出力は空。
        bool TryGetChunks(SkinnedMeshHandle handle, Container::VariableArray<MeshIndexChunk>& out) const;
        // 塊の作り方を差し替える（テスト用。nullptr で既定へ戻す）。登録済みのメッシュには効かない。
        void SetChunkBuilderForTesting(MeshIndexChunkBuilder builder);
        void CollectReleasedResources();
        void ForceClearAfterWaitIdle();

    private:
        struct PaletteUse
        {
            RHI::BufferPtr Buffer;
            // 直前のフレームの変換とパレット（無ければ空）。Buffer と同じ寿命で保つ。
            RHI::BufferPtr PreviousBuffer;
            Container::VariableArray<Container::TWeakPtr<const SkinnedMeshFrameLease>> FrameLeases;
            uint64_t ComponentId = 0;
            uint64_t Epoch = 0;
            SkinnedMeshHandle Handle;
            Math::Matrix4x4 World;
            Container::VariableArray<Math::Matrix4x4> Bones;
            Math::Matrix4x4 PreviousWorld;
            Container::VariableArray<Math::Matrix4x4> PreviousBones;
            bool bPreviousAttempted = false;
            uint64_t LastSubmittedSerial = 0;
        };

        struct Entry
        {
            SkinnedMeshHandle Handle;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            uint32_t IndexCount = 0;
            Container::VariableArray<MeshIndexChunk> Chunks;
            // 塊に分けられたか。false でもメッシュは登録され、GBuffer・影の経路は頂点とインデックスだけで描く
            bool bChunksValid = false;
            Container::TWeakPtr<const SkinnedMeshAssetLease> AssetLease;
            Container::VariableArray<Container::TWeakPtr<const SkinnedMeshFrameLease>> FrameLeases;
            Container::VariableArray<Container::TSharedPtr<PaletteUse>> PaletteUses;
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
        // この表の所有は準備用。GPU/packet寿命はEntry::PaletteUsesで別に保持する。
        Container::Map<uint64_t, Container::TSharedPtr<PaletteUse>> m_ActivePalettes;
        uint64_t m_PreparationEpoch = 0;
        uint64_t m_CompletedSubmissionSerial = 0;
        bool m_bFrameOpen = false;
        MeshIndexChunkBuilder m_ChunkBuilder = nullptr;
        bool m_bLoggedChunkFailure = false;
    };
} // namespace NorvesLib::Core::Rendering
