#pragma once

#include "RHI/RHITypes.h"
#include "RHI/DeviceCapabilities.h"
#include "DrawCommand.h"
#include "Rendering/DebugDrawQueue.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderGraph/RenderGraphDump.h"
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
    struct ShadowMapPassSettings;

    struct RenderGraphDebugCapture
    {
        uint32_t TargetSceneViewId = UINT32_MAX;
        RGDumpOptions Options;
        Container::String Text;
        bool bCaptured = false;
        bool bAttempted = false;
    };

    struct DirectionalShadowShaderValues
    {
        float View[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f,
                          0.0f, 0.0f, 0.0f, 1.0f};
        float Projection[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
        uint64_t LightId = 0;
        bool bEnabled = false;
    };

    inline constexpr uint32_t PhysicalLightingShadowCascadeCount = 4u;
    inline constexpr uint32_t PhysicalLightingShadowSplitCount =
        PhysicalLightingShadowCascadeCount + 1u;

    struct CascadedDirectionalShadowShaderValues
    {
        float View[PhysicalLightingShadowCascadeCount][16] = {};
        float Projection[PhysicalLightingShadowCascadeCount][16] = {};
        float SplitDistances[PhysicalLightingShadowSplitCount] = {};
        uint32_t CascadeCount = 0;
        uint64_t LightId = 0;
        bool bEnabled = false;
    };

    struct PhysicalLightingResources
    {
        uint64_t FrameNumber = 0;
        uint32_t ViewId = UINT32_MAX;
        uint32_t ViewportId = UINT32_MAX;
        bool bActive = false;
        bool bShadowPublished = false;
        bool bLightingPublished = false;

        RHI::TexturePtr ShadowMapTexture;
        RHI::SamplerPtr ShadowSampler;
        DirectionalShadowShaderValues DirectionalShadow;
        CascadedDirectionalShadowShaderValues CascadedShadow;

        RHI::BufferPtr LightBuffer;
        uint32_t LogicalLightCount = 0;
        uint32_t LightBufferSizeBytes = 0;

        RHI::TexturePtr EnvironmentRadianceTexture;
        RHI::SamplerPtr EnvironmentRadianceSampler;
        RHI::TexturePtr DiffuseIrradianceTexture;
        RHI::SamplerPtr DiffuseIrradianceSampler;
        RHI::TexturePtr PrefilteredSpecularTexture;
        RHI::SamplerPtr PrefilteredSpecularSampler;
        RHI::TexturePtr DfgLutTexture;
        RHI::SamplerPtr DfgLutSampler;
        uint32_t PrefilteredSpecularMipLevels = 0;
        float IBLIntensity = 0.0f;
        bool bIBLEnabled = false;

        void Begin(uint64_t frameNumber, uint32_t viewId, uint32_t viewportId)
        {
            FrameNumber = 0;
            ViewId = UINT32_MAX;
            ViewportId = UINT32_MAX;
            bActive = false;
            bShadowPublished = false;
            bLightingPublished = false;
            ShadowMapTexture.reset();
            ShadowSampler.reset();
            LightBuffer.reset();
            LogicalLightCount = 0;
            LightBufferSizeBytes = 0;
            EnvironmentRadianceTexture.reset();
            EnvironmentRadianceSampler.reset();
            DiffuseIrradianceTexture.reset();
            DiffuseIrradianceSampler.reset();
            PrefilteredSpecularTexture.reset();
            PrefilteredSpecularSampler.reset();
            DfgLutTexture.reset();
            DfgLutSampler.reset();
            PrefilteredSpecularMipLevels = 0;
            IBLIntensity = 0.0f;
            bIBLEnabled = false;
            CascadedShadow = CascadedDirectionalShadowShaderValues{};
            for (uint32_t index = 0; index < 16; ++index)
            {
                DirectionalShadow.View[index] = 0.0f;
                DirectionalShadow.Projection[index] = 0.0f;
            }
            DirectionalShadow.View[0] = 1.0f;
            DirectionalShadow.View[5] = 1.0f;
            DirectionalShadow.View[10] = 1.0f;
            DirectionalShadow.View[15] = 1.0f;
            DirectionalShadow.Projection[0] = 1.0f;
            DirectionalShadow.Projection[5] = 1.0f;
            DirectionalShadow.Projection[10] = 1.0f;
            DirectionalShadow.Projection[15] = 1.0f;
            DirectionalShadow.LightId = 0;
            DirectionalShadow.bEnabled = false;
            FrameNumber = frameNumber;
            ViewId = viewId;
            ViewportId = viewportId;
            bActive = true;
        }

        void PublishDirectionalShadow(const float* view,
                                      const float* projection,
                                      uint64_t lightId,
                                      bool bEnabledValue,
                                      const RHI::TexturePtr& shadowMap,
                                      const RHI::SamplerPtr& shadowSampler)
        {
            if (view != nullptr && projection != nullptr)
            {
                for (uint32_t index = 0; index < 16; ++index)
                {
                    DirectionalShadow.View[index] = view[index];
                    DirectionalShadow.Projection[index] = projection[index];
                }
            }
            DirectionalShadow.LightId = lightId;
            DirectionalShadow.bEnabled = bEnabledValue;
            ShadowMapTexture = shadowMap;
            ShadowSampler = shadowSampler;
            bShadowPublished = true;
        }

        void PublishCascadedShadow(const float* views,
                                   const float* projections,
                                   const float* splitDistances,
                                   uint32_t cascadeCount,
                                   uint64_t lightId,
                                   bool bEnabledValue)
        {
            CascadedShadow = CascadedDirectionalShadowShaderValues{};
            if (views == nullptr || projections == nullptr || splitDistances == nullptr ||
                cascadeCount > PhysicalLightingShadowCascadeCount)
            {
                return;
            }

            for (uint32_t cascadeIndex = 0;
                 cascadeIndex < PhysicalLightingShadowCascadeCount;
                 ++cascadeIndex)
            {
                for (uint32_t matrixIndex = 0; matrixIndex < 16; ++matrixIndex)
                {
                    CascadedShadow.View[cascadeIndex][matrixIndex] =
                        views[cascadeIndex * 16u + matrixIndex];
                    CascadedShadow.Projection[cascadeIndex][matrixIndex] =
                        projections[cascadeIndex * 16u + matrixIndex];
                }
            }
            for (uint32_t splitIndex = 0;
                 splitIndex < PhysicalLightingShadowSplitCount;
                 ++splitIndex)
            {
                CascadedShadow.SplitDistances[splitIndex] = splitDistances[splitIndex];
            }
            CascadedShadow.CascadeCount = cascadeCount;
            CascadedShadow.LightId = lightId;
            CascadedShadow.bEnabled = bEnabledValue &&
                cascadeCount == PhysicalLightingShadowCascadeCount;
        }

        void PublishLighting(const RHI::BufferPtr& lightBuffer,
                             uint32_t logicalLightCount,
                             uint32_t lightBufferSizeBytes,
                             const RHI::TexturePtr& environmentRadiance,
                             const RHI::SamplerPtr& environmentRadianceSampler,
                             const RHI::TexturePtr& diffuseIrradiance,
                             const RHI::SamplerPtr& diffuseIrradianceSampler,
                             const RHI::TexturePtr& prefilteredSpecular,
                             const RHI::SamplerPtr& prefilteredSpecularSampler,
                             const RHI::TexturePtr& dfgLut,
                             const RHI::SamplerPtr& dfgLutSampler,
                             uint32_t prefilteredSpecularMipLevels,
                             float iblIntensity,
                             bool bIBLEnabledValue)
        {
            LightBuffer = lightBuffer;
            LogicalLightCount = logicalLightCount;
            LightBufferSizeBytes = lightBufferSizeBytes;
            EnvironmentRadianceTexture = environmentRadiance;
            EnvironmentRadianceSampler = environmentRadianceSampler;
            DiffuseIrradianceTexture = diffuseIrradiance;
            DiffuseIrradianceSampler = diffuseIrradianceSampler;
            PrefilteredSpecularTexture = prefilteredSpecular;
            PrefilteredSpecularSampler = prefilteredSpecularSampler;
            DfgLutTexture = dfgLut;
            DfgLutSampler = dfgLutSampler;
            PrefilteredSpecularMipLevels = prefilteredSpecularMipLevels;
            IBLIntensity = iblIntensity;
            bIBLEnabled = bIBLEnabledValue;
            bLightingPublished = true;
        }

        bool Matches(uint64_t frameNumber, uint32_t viewId, uint32_t viewportId) const
        {
            return bActive && FrameNumber == frameNumber && ViewId == viewId &&
                   ViewportId == viewportId;
        }

        void Invalidate()
        {
            *this = PhysicalLightingResources{};
        }
    };

    struct SkyAtmosphereRenderResources
    {
        SkyAtmosphereParameters Parameters;
        RHI::TexturePtr TransmittanceTexture;
        RHI::TexturePtr RadianceTexture;
        RHI::TexturePtr SunDiskTexture;
        RHI::SamplerPtr Sampler;
        float PreExposure = 1.0f;
        float SunDiskPreExposedLuminance = 0.0f;
        bool bSnapshotEnabled = false;
        bool bValid = false;
        bool bSunDiskSaturated = false;

        void Reset()
        {
            Parameters = SkyAtmosphereParameters{};
            TransmittanceTexture.reset();
            RadianceTexture.reset();
            SunDiskTexture.reset();
            Sampler.reset();
            PreExposure = 1.0f;
            SunDiskPreExposedLuminance = 0.0f;
            bSnapshotEnabled = false;
            bValid = false;
            bSunDiskSaturated = false;
        }

        void Publish(const SkyAtmosphereParameters& parameters,
                     const RHI::TexturePtr& transmittance,
                     const RHI::TexturePtr& radiance,
                     const RHI::TexturePtr& sunDisk,
                     const RHI::SamplerPtr& sampler,
                     float preExposureValue,
                     float sunDiskValue,
                     bool bSunDiskSaturatedValue,
                     bool bValidValue)
        {
            Parameters = parameters;
            TransmittanceTexture = transmittance;
            RadianceTexture = radiance;
            SunDiskTexture = sunDisk;
            Sampler = sampler;
            PreExposure = preExposureValue;
            SunDiskPreExposedLuminance = sunDiskValue;
            bSnapshotEnabled = parameters.bEnabled;
            bValid = bValidValue;
            bSunDiskSaturated = bSunDiskSaturatedValue;
        }
    };

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
        PhysicalLightingResources PhysicalLighting;
        SkyAtmosphereRenderResources SkyAtmosphere;

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
        /** @brief RenderThread-owned GPU skinned mesh store */
        SkinnedMeshResources* SkinnedMeshes = nullptr;

        RenderResourceFrameContext Resources;

        /** @brief シェーダーアセットの読み込み・コンパイル・キャッシュ管理 */
        ShaderManager *ShaderMgr = nullptr;

        /** @brief デバイス能力情報（Neural Shaders / Mega Geometry等の判定用） */
        const RHI::DeviceCapabilities *Capabilities = nullptr;

        /** @brief メインカメラ情報（ビュー/プロジェクション行列計算用） */
        const CameraProxy *MainCamera = nullptr;

        /** @brief FramePacketが所有する空パラメータのスナップショット（未接続時は無効） */
        const SceneProxy *SnapshotScene = nullptr;

        /** @brief SnapshotScene未接続時に使用する空パラメータ値 */
        SkyAtmosphereParameters SkyAtmosphereSnapshot;

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

        /** @brief デバッグライン頂点スナップショット（FramePacket::DebugLineVerticesを指す） */
        const Container::VariableArray<DebugLineVertex> *SnapshotDebugLineVertices = nullptr;

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

        /** @brief MeshProxyスナップショット（FramePacket::Scene.MeshProxiesを指す） */
        /** @brief FramePacket所有のskinned mesh frame lease */
        const Container::VariableArray<Container::TSharedPtr<const SkinnedMeshFrameLease>>*
            SnapshotSkinnedMeshFrameLeases = nullptr;

        const Container::VariableArray<MeshProxy>* SnapshotMeshProxies = nullptr;

        /** @brief FramePacket::Scene.SkinnedMeshProxies のanimated boundsスナップショット */
        const Container::VariableArray<SkinnedMeshProxy>* SnapshotSkinnedMeshProxies = nullptr;

        /** @brief LightProxyスナップショット（FramePacket::Scene.LightProxiesを指す） */
        const Container::VariableArray<LightProxy> *SnapshotLightProxies = nullptr;

        /** @brief MegaGeometryProxyスナップショット（FramePacket::Scene.MegaGeometryProxiesを指す） */
        const Container::VariableArray<MegaGeometryProxy> *SnapshotMegaGeometryProxies = nullptr;

        /** @brief 現在のViewportで実行済みのShadowMapPass設定（フレーム内借用） */
        const ShadowMapPassSettings* ActiveShadowMapSettings = nullptr;

        // ========================================
        // FrameCommand queue（mixed mode migration 用）
        // ========================================

        /** @brief FrameCommand を ICommandList に変換する唯一の実行レイヤ */
        SceneRenderer* Renderer = nullptr;

        /** @brief pass が enqueue した FrameCommand の一時キュー */
        Container::VariableArray<FrameCommand>* PendingFrameCommands = nullptr;

        /** @brief 有効な場合、ViewはRenderGraph経由でパスチェーンを実行する */
        RenderGraph* Graph = nullptr;

        /** @brief RenderFrame stack-owned, requested text-only graph diagnostics. */
        RenderGraphDebugCapture* DebugDumpCapture = nullptr;

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
        // overlay seam 専用 presentation load 経路
        // ========================================

        /**
         * @brief overlay seam 用の最終 blit 後 back buffer へ load-blend する load render pass
         *
         * 録画窓内の汎用 CurrentRenderPass/CurrentFramebuffer は overlay seam では
         * clear 系/未 Begin のため流用不可。RenderingCoordinator が seam で経路
         * (bOverlayComposite)に応じ legacy/graph の presentation load 経路を選んで設定する。
         * overlay 0 件のときは未設定(nullptr)のままで完全 no-op。
         */
        RHI::IRenderPass *OverlayLoadRenderPass = nullptr;

        /**
         * @brief OverlayLoadRenderPass に対応する back buffer framebuffer[imageIndex]
         */
        RHI::IFramebuffer *OverlayLoadFramebuffer = nullptr;

        /**
         * @brief overlay が composite 経路で描かれたか(=ShouldCompose 結果)
         *
         * true なら graph(composite) presentation load 経路、false なら legacy 経路で
         * back buffer が残されている。OverlayLoadRenderPass/Framebuffer の選択根拠。
         */
        bool bOverlayComposite = false;

        /**
         * @brief overlay seam で処理中の FramePacket が占有するスロット index
         *
         * RenderThread が RenderFrame で処理中のパケットのスロット index
         * (FramePacketManager::GetSlotIndex)を seam が設定する。overlay パスが
         * FramePacket スロット寿命に束ねた per-slot リソース(オーバーレイのドローデータ
         * スナップショット等)を読む際の添字。GameThread の書込みスロットと RenderThread の
         * 読取スロットはプール排他で同一スロットに対して時間的に重ならないため、本 index で
         * 添字すれば use-after-free なく安全に読める。既定 FRAME_PACKET_BUFFER_COUNT は無効値。
         */
        uint32_t OverlayPacketSlotIndex = 0;

        // ========================================
        // フレーム情報
        // ========================================

        /** @brief 現在のフレームインデックス（ダブル/トリプルバッファリング用） */
        uint32_t FrameIndex = 0;

        /** @brief FramePacketの単調なフレーム番号 */
        uint64_t FrameNumber = 0;

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
