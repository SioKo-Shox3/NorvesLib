#pragma once

#include "RHI/RHITypes.h"
#include "RHI/DeviceCapabilities.h"
#include "DrawCommand.h"
#include "FrameCommand.h"
#include "ViewportSnapshot.h"
#include "SceneRenderer.h"
#include "SceneProxy.h"
#include "Container/Containers.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
    class TransientResourcePool;
    class IRenderPass;
    class IFramebuffer;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class SharedResourceRegistry;
    class RenderResourceManager;
    class ShaderManager;
    class MegaGeometryPass;

    /**
     * @brief View描画コンテキスト
     *
     * View::Render()に渡される描画実行に必要なリソースの参照をまとめた構造体。
     * 各Viewはこのコンテキストからコマンドリスト・一時リソースプール等にアクセスします。
     *
     * 責務:
     * - RHIコマンドリストへの参照提供
     * - TransientResourcePool（一時レンダーターゲット等）への参照提供
     * - View間共有リソースレジストリへの参照提供
     * - フレーム・タイミング情報の提供
     * - 現在のレンダーパス/フレームバッファ情報（SwapChain直接描画用）
     */
    struct ViewRenderContext
    {
        // ========================================
        // RHIリソース
        // ========================================

        /** @brief 描画コマンド記録先 */
        RHI::ICommandList *CommandList = nullptr;

        /** @brief RHIデバイス */
        RHI::IDevice *Device = nullptr;

        /** @brief フレーム内一時リソースプール（レンダーターゲット等） */
        RHI::TransientResourcePool *TransientPool = nullptr;

        // ========================================
        // 共有リソース
        // ========================================

        /** @brief View間でリソースを共有するためのレジストリ */
        SharedResourceRegistry *SharedResources = nullptr;

        /** @brief メッシュGPUデータ等を管理するリソースマネージャー */
        RenderResourceManager *ResourceManager = nullptr;

        /** @brief シェーダーアセットの読み込み・コンパイル・キャッシュ管理 */
        ShaderManager *ShaderMgr = nullptr;

        /** @brief デバイス能力情報（Neural Shaders / Mega Geometry等の判定用） */
        const RHI::DeviceCapabilities *Capabilities = nullptr;

        /** @brief メインカメラ情報（ビュー/プロジェクション行列計算用） */
        const CameraProxy *MainCamera = nullptr;

        /** @brief 現在描画中のViewportスナップショット（新描画フロー用） */
        const ViewportSnapshot *CurrentViewport = nullptr;

        /** @brief 現在描画中のViewportに対応するカメラ */
        const CameraProxy *CurrentCamera = nullptr;

        // ========================================
        // DrawCommand / Proxyスナップショット（FramePacketから指す、RenderThread読み取り専用）
        // ========================================

        /** @brief 全DrawCommandスナップショット（GameThreadが生成、パスが読み取る） */
        const Container::VariableArray<DrawCommand> *SnapshotDrawCommands = nullptr;

        /** @brief 不透明DrawCommandスナップショット */
        const Container::VariableArray<DrawCommand> *SnapshotOpaqueCommands = nullptr;

        /** @brief 半透明DrawCommandスナップショット */
        const Container::VariableArray<DrawCommand> *SnapshotTransparentCommands = nullptr;

        /** @brief 現在描画中のViewportに対応する全DrawCommand */
        const Container::VariableArray<DrawCommand> *CurrentDrawCommands = nullptr;

        /** @brief 現在描画中のViewportに対応する不透明DrawCommand */
        const Container::VariableArray<DrawCommand> *CurrentOpaqueCommands = nullptr;

        /** @brief 現在描画中のViewportに対応する半透明DrawCommand */
        const Container::VariableArray<DrawCommand> *CurrentTransparentCommands = nullptr;

        /** @brief LightProxyスナップショット（FramePacket::Scene.LightProxiesを指す） */
        const Container::VariableArray<LightProxy> *SnapshotLightProxies = nullptr;

        /** @brief MegaGeometryProxyスナップショット（FramePacket::Scene.MegaGeometryProxiesを指す） */
        const Container::VariableArray<MegaGeometryProxy> *SnapshotMegaGeometryProxies = nullptr;

        // ========================================
        // FrameCommand queue（mixed mode migration 用）
        // ========================================

        /** @brief FrameCommand を ICommandList に変換する唯一の実行レイヤ */
        SceneRenderer* Renderer = nullptr;

        /** @brief pass が enqueue した FrameCommand の一時キュー */
        Container::VariableArray<FrameCommand>* PendingFrameCommands = nullptr;

        const CameraProxy *GetActiveCamera() const
        {
            return CurrentCamera ? CurrentCamera : MainCamera;
        }

        const Container::VariableArray<DrawCommand> *GetActiveDrawCommands() const
        {
            return CurrentDrawCommands ? CurrentDrawCommands : SnapshotDrawCommands;
        }

        const Container::VariableArray<DrawCommand> *GetActiveOpaqueCommands() const
        {
            return CurrentOpaqueCommands ? CurrentOpaqueCommands : SnapshotOpaqueCommands;
        }

        const Container::VariableArray<DrawCommand> *GetActiveTransparentCommands() const
        {
            return CurrentTransparentCommands ? CurrentTransparentCommands : SnapshotTransparentCommands;
        }

        void EnqueueFrameCommand(const FrameCommand& command)
        {
            if (PendingFrameCommands)
            {
                PendingFrameCommands->push_back(command);
                return;
            }

            if (Renderer && CommandList)
            {
                Container::VariableArray<FrameCommand> immediateCommands;
                immediateCommands.push_back(command);
                Renderer->ExecuteFrameCommands(immediateCommands, CommandList);
            }
        }

        void EnqueueFullscreenPass(RHI::RenderPassPtr renderPass,
                                   RHI::FramebufferPtr framebuffer,
                                   const RHI::Viewport& viewport,
                                   const RHI::ScissorRect& scissor,
                                   RHI::PipelinePtr pipeline,
                                   RHI::DescriptorSetPtr descriptorSet,
                                   uint32_t descriptorSetSlot = 0,
                                   uint32_t vertexCount = 3)
        {
            EnqueueFrameCommand(FrameCommand::CreateFullscreenPass(renderPass,
                                                                   framebuffer,
                                                                   viewport,
                                                                   scissor,
                                                                   pipeline,
                                                                   descriptorSet,
                                                                   descriptorSetSlot,
                                                                   vertexCount));
        }

        void EnqueueTextureBarrier(RHI::TexturePtr texture,
                                   RHI::ResourceState beforeState,
                                   RHI::ResourceState afterState,
                                   uint32_t mipLevel = 0,
                                   uint32_t arrayIndex = 0,
                                   uint32_t mipCount = 0,
                                   uint32_t arrayCount = 0)
        {
            EnqueueFrameCommand(FrameCommand::CreateTextureBarrier(texture,
                                                                  beforeState,
                                                                  afterState,
                                                                  mipLevel,
                                                                  arrayIndex,
                                                                  mipCount,
                                                                  arrayCount));
        }

        void EnqueueMegaGeometryPass(MegaGeometryPass* pass)
        {
            const CameraProxy *activeCamera = GetActiveCamera();
            EnqueueFrameCommand(FrameCommand::CreateMegaGeometryPass(pass,
                                                                   ResourceManager,
                                                                   activeCamera ? *activeCamera : CameraProxy{},
                                                                   activeCamera != nullptr));
        }

        // ========================================
        // 現在のレンダーパスコンテキスト
        // ========================================

        /**
         * @brief 現在アクティブなレンダーパス
         *
         * RenderingCoordinatorが既にBeginRenderPassしている場合、
         * パスはこのレンダーパス内で描画を行います。
         * nullptrの場合、パスが自分でBeginRenderPass/EndRenderPassを呼びます。
         */
        RHI::IRenderPass *CurrentRenderPass = nullptr;

        /**
         * @brief 現在アクティブなフレームバッファ
         *
         * SwapChainフレームバッファへの直接描画時に使用されます。
         */
        RHI::IFramebuffer *CurrentFramebuffer = nullptr;

        /**
         * @brief レンダーパスが外部で管理されているか
         *
         * trueの場合、パスはBeginRenderPass/EndRenderPassを呼びません。
         * RenderingCoordinatorが既にレンダーパスを開始している場合にtrueになります。
         */
        bool bRenderPassActive = false;

        // ========================================
        // フレーム情報
        // ========================================

        /** @brief 現在のフレームインデックス（ダブル/トリプルバッファリング用） */
        uint32_t FrameIndex = 0;

        /** @brief スクリーン幅 */
        uint32_t ScreenWidth = 0;

        /** @brief スクリーン高さ */
        uint32_t ScreenHeight = 0;

        /** @brief 内部描画幅 */
        uint32_t RenderWidth = 0;

        /** @brief 内部描画高さ */
        uint32_t RenderHeight = 0;

        /** @brief 前フレームからの経過時間（秒） */
        float DeltaTime = 0.0f;

        /** @brief アプリケーション開始からの経過時間（秒） */
        double TotalTime = 0.0;
    };

} // namespace NorvesLib::Core::Rendering
