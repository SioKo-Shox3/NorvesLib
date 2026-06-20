#pragma once

#include "RHI/RHITypes.h"
#include "RHI/DeviceCapabilities.h"
#include "DrawCommand.h"
#include "Rendering/RenderResourceContexts.h"
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
    class ShaderManager;
    class MegaGeometryPass;
    class PresentationPass;
    class RenderGraph;
    struct RenderGraphExecutionResult;

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

        /** @brief FramePacket::InstanceDataをアップロードしたSSBO（P6以降で消費） */
        RHI::BufferPtr InstanceDataBuffer;

        // ========================================
        // 共有リソース
        // ========================================

        /** @brief View間でリソースを共有するためのレジストリ */
        SharedResourceRegistry *SharedResources = nullptr;

        /** @brief フレーム実行中だけ有効なレンダリングリソースドメイン */
        RenderResourceFrameContext Resources;

        /** @brief シェーダーアセットの読み込み・コンパイル・キャッシュ管理 */
        ShaderManager *ShaderMgr = nullptr;

        /** @brief デバイス能力情報（Neural Shaders / Mega Geometry等の判定用） */
        const RHI::DeviceCapabilities *Capabilities = nullptr;

        /** @brief メインカメラ情報（ビュー/プロジェクション行列計算用） */
        const CameraProxy *MainCamera = nullptr;

        /** @brief 現在描画中のViewportスナップショット（新描画フロー用） */
        const ViewportRenderPlan *CurrentViewport = nullptr;

        /** @brief 現在描画中のViewportに対応するカメラ */
        const CameraProxy *CurrentCamera = nullptr;

        // ========================================
        // DrawCommand / Proxyスナップショット（FramePacketから指す、RenderThread読み取り専用）
        // ========================================

        /** @brief FramePacket::DrawCommands実体配列（範囲ビューの基準） */
        const Container::VariableArray<DrawCommand> *SnapshotDrawCommandSource = nullptr;

        /** @brief 全DrawCommandスナップショット（GameThreadが生成、パスが読み取る） */
        DrawCommandView SnapshotDrawCommands;

        /** @brief 不透明DrawCommandスナップショット */
        DrawCommandView SnapshotOpaqueCommands;

        /** @brief 半透明DrawCommandスナップショット */
        DrawCommandView SnapshotTransparentCommands;

        /** @brief 現在描画中のViewportに対応する全DrawCommand */
        DrawCommandView CurrentDrawCommands;

        /** @brief 現在描画中のViewportに対応する不透明DrawCommand */
        DrawCommandView CurrentOpaqueCommands;

        /** @brief 現在描画中のViewportに対応する半透明DrawCommand */
        DrawCommandView CurrentTransparentCommands;

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

        /** @brief 有効な場合、ViewはRenderGraph経由でパスチェーンを実行する */
        RenderGraph* Graph = nullptr;

        /** @brief フレーム中だけ借用するGraph最終合成パス */
        PresentationPass* PresentationGraphPass = nullptr;

        /** @brief 現在のView/Viewportで成功したRenderGraph実行結果 */
        const RenderGraphExecutionResult* CurrentGraphExecutionResult = nullptr;

        /** @brief 現在のViewportでPresentationGraphPassがswapchain合成を担当したか */
        bool bPresentationGraphPassHandled = false;

        const CameraProxy *GetActiveCamera() const
        {
            return CurrentCamera ? CurrentCamera : MainCamera;
        }

        DrawCommandView GetActiveDrawCommands() const
        {
            return CurrentViewport ? CurrentDrawCommands : SnapshotDrawCommands;
        }

        DrawCommandView GetActiveOpaqueCommands() const
        {
            return CurrentViewport ? CurrentOpaqueCommands : SnapshotOpaqueCommands;
        }

        DrawCommandView GetActiveTransparentCommands() const
        {
            return CurrentViewport ? CurrentTransparentCommands : SnapshotTransparentCommands;
        }

        DebugViewMode GetActiveDebugMode() const
        {
            return CurrentViewport ? CurrentViewport->DebugMode : DebugViewMode::Normal;
        }

        uint32_t GetActiveRenderWidth() const
        {
            if (CurrentViewport && CurrentViewport->HasDrawableExtent())
            {
                return static_cast<uint32_t>(CurrentViewport->PixelRect.Width);
            }
            return RenderWidth;
        }

        uint32_t GetActiveRenderHeight() const
        {
            if (CurrentViewport && CurrentViewport->HasDrawableExtent())
            {
                return static_cast<uint32_t>(CurrentViewport->PixelRect.Height);
            }
            return RenderHeight;
        }

        float GetActiveAspectRatio() const
        {
            const uint32_t height = GetActiveRenderHeight();
            return height > 0
                       ? static_cast<float>(GetActiveRenderWidth()) / static_cast<float>(height)
                       : 1.0f;
        }

        RHI::Viewport GetActiveLocalViewport() const
        {
            const bool bHasActiveViewport = CurrentViewport && CurrentViewport->HasDrawableExtent();
            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(GetActiveRenderWidth());
            viewport.height = static_cast<float>(GetActiveRenderHeight());
            viewport.minDepth = bHasActiveViewport ? CurrentViewport->PixelRect.MinDepth : 0.0f;
            viewport.maxDepth = bHasActiveViewport ? CurrentViewport->PixelRect.MaxDepth : 1.0f;
            return viewport;
        }

        RHI::ScissorRect GetActiveLocalScissor() const
        {
            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32_t>(GetActiveRenderWidth());
            scissor.bottom = static_cast<int32_t>(GetActiveRenderHeight());
            return scissor;
        }

        RHI::Viewport GetActiveOutputViewport() const
        {
            if (!CurrentViewport || !CurrentViewport->HasDrawableExtent())
            {
                return GetActiveLocalViewport();
            }

            RHI::Viewport viewport;
            viewport.x = CurrentViewport->PixelRect.X;
            viewport.y = CurrentViewport->PixelRect.Y;
            viewport.width = CurrentViewport->PixelRect.Width;
            viewport.height = CurrentViewport->PixelRect.Height;
            viewport.minDepth = CurrentViewport->PixelRect.MinDepth;
            viewport.maxDepth = CurrentViewport->PixelRect.MaxDepth;
            return viewport;
        }

        RHI::ScissorRect GetActiveOutputScissor() const
        {
            if (!CurrentViewport || !CurrentViewport->HasDrawableExtent())
            {
                return GetActiveLocalScissor();
            }

            RHI::ScissorRect scissor;
            scissor.left = CurrentViewport->Scissor.Left;
            scissor.top = CurrentViewport->Scissor.Top;
            scissor.right = CurrentViewport->Scissor.Right;
            scissor.bottom = CurrentViewport->Scissor.Bottom;
            return scissor;
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
                                                                   Resources.MegaGeometry,
                                                                   activeCamera ? *activeCamera : CameraProxy{},
                                                                   activeCamera != nullptr,
                                                                   GetActiveLocalViewport(),
                                                                   GetActiveLocalScissor(),
                                                                   GetActiveDebugMode()));
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
