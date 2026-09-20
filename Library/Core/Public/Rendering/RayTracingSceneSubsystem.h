#pragma once

#include "RHI/IAccelerationStructure.h"
#include "Rendering/FramePacket.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class MeshResources;

    /**
     * @brief FramePacketの描画スナップショットから加速構造を管理
     *
     * GameThreadではDrawCommandからgeometryとtransformをコピーし、
     * RenderThreadではFramePacketだけを使ってBLAS/TLAS buildを記録します。
     */
    class RayTracingSceneSubsystem
    {
    public:
        RayTracingSceneSubsystem() = default;
        ~RayTracingSceneSubsystem();

        RayTracingSceneSubsystem(const RayTracingSceneSubsystem&) = delete;
        RayTracingSceneSubsystem& operator=(const RayTracingSceneSubsystem&) = delete;

        /**
         * @brief Draw snapshotからFramePacketのレイトレーシングシーンを構築
         * @param meshResources DrawCommandのメッシュハンドルを解決するGPU mesh resource
         * @param packet 構築元および出力先のフレームスナップショット
         * @return 有効なsnapshotを構築できた場合true
         */
        bool BuildFrameSnapshot(const MeshResources* meshResources, FramePacket& packet);

        /**
         * @brief FramePacketのgeometryからBLAS/TLAS buildをRenderThreadへ記録
         * @param device 加速構造を作成するRHIデバイス
         * @param commandList buildを記録するコマンドリスト
         * @param frameSlot FramePacketManagerのslot index
         * @param packet GameThreadが確定したシーンスナップショット
         * @return buildを記録できた場合true
         */
        bool BuildAccelerationStructures(RHI::DevicePtr device,
                                         RHI::ICommandList& commandList,
                                         uint32_t frameSlot,
                                         FramePacket& packet);

        /** @brief サブシステムが保持するGPU資源を解放 */
        void Shutdown();

    private:
        struct BottomLevelCacheEntry
        {
            MeshDataHandle MeshHandle;
            RHI::BufferPtr SourceVertexBuffer;
            RHI::BufferPtr SourceIndexBuffer;
            uint32_t IndexOffset = 0;
            uint32_t IndexCount = 0;
            uint32_t VertexOffset = 0;
            uint32_t VertexCount = 0;
            uint32_t VertexStride = 0;
            bool bGeometryOpaque = true;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            RHI::AccelerationStructurePtr Structure;
        };

        struct TopLevelCacheEntry
        {
            RHI::AccelerationStructurePtr Structure;
            uint32_t InstanceCount = 0;
        };

        Container::VariableArray<BottomLevelCacheEntry> m_BottomLevelCache;
        Container::FixedArray<TopLevelCacheEntry, FRAME_PACKET_BUFFER_COUNT> m_TopLevelCache;
    };

} // namespace NorvesLib::Core::Rendering
