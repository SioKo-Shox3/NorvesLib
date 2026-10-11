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
    class MaterialResources;
    class MegaGeometryResources;

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
         * @brief DrawスナップショットからFramePacketのレイトレーシングシーンを構築
         * @param meshResources DrawCommandのメッシュハンドルを解決するGPUメッシュリソース
         * @param packet 構築元および出力先のフレームスナップショット
         * @param materialResources DrawCommandのマテリアル値を解決するリソース
         * @param megaGeometryResources MegaGeometry（岩・小屋など）のLOD0を解決するリソース。nullなら含めない
         * @param megaGeometryProxies MegaGeometry の proxy の配列。nullなら packet.Scene.MegaGeometryProxies を読む
         *        （連番のパストレーシングでは差分の同期でも GameThread で作るので、GameThread の SceneView の配列を渡す）
         * @param meshProxies インスタンシング描画の物体IDの照合に使う MeshProxy の配列。nullなら packet.Scene.MeshProxies を読む
         *        （差分の同期ではパケットが全量を持たないので、GameThread の SceneView の配列を渡す）
         * @return 有効なスナップショットを構築できた場合true
         */
        bool BuildFrameSnapshot(const MeshResources* meshResources,
                                FramePacket& packet,
                                const MaterialResources* materialResources = nullptr,
                                const MegaGeometryResources* megaGeometryResources = nullptr,
                                const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies = nullptr,
                                const Container::VariableArray<MeshProxy>* meshProxies = nullptr);

        /**
         * @brief MegaGeometry（岩・小屋など）のインスタンスを、パケットのレイトレーシングシーンの末尾へ足す
         * @param megaGeometryResources LOD0 を解決するリソース。nullなら何も足さない
         * @param megaProxies 足す proxy の配列。差分の同期では RenderThread の写し（MegaGeometryMirror）を渡す
         * @param packet 足し先のフレームスナップショット
         *
         * BuildFrameSnapshot の MegaGeometry の段と同じ規則で、BuildAccelerationStructures の前に呼ぶ。
         */
        void AppendMegaGeometryInstances(const MegaGeometryResources* megaGeometryResources,
                                         const Container::VariableArray<MegaGeometryProxy>& megaProxies,
                                         FramePacket& packet);

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
            uint64_t VertexBufferOffsetBytes = 0;
            uint64_t IndexBufferOffsetBytes = 0;
            uint64_t MegaMeshId = 0;
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
