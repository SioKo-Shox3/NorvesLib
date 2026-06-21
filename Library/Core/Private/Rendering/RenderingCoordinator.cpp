#include "Rendering/RenderingCoordinator.h"
#include "Rendering/Screen.h"
#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/Viewport.h"
#include "Rendering/InstanceBufferRing.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FramePacket.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/PresentationComposer.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/RenderFrameExecutor.h"
#include "RHI/ISampler.h"
#include "RHI/IDevice.h"
#include "RHI/ISwapChain.h"
#include "RHI/ICommandList.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <cassert>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        ViewportRenderPlan BuildViewportRenderPlan(const Viewport &viewport,
                                                   uint32_t viewId,
                                                   uint32_t viewportId,
                                                   uint32_t renderWidth,
                                                   uint32_t renderHeight,
                                                   const CameraProxy *fallbackCamera)
        {
            ViewportRenderPlan plan;
            plan.ViewId = viewId;
            plan.ViewportId = viewportId;
            plan.bEnabled = viewport.IsEnabled();
            plan.RenderWidth = renderWidth;
            plan.RenderHeight = renderHeight;
            plan.DebugMode = viewport.GetDebugViewMode();

            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            viewport.GetRect(x, y, width, height);

            float minDepth = 0.0f;
            float maxDepth = 1.0f;
            viewport.GetDepthRange(minDepth, maxDepth);

            plan.NormalizedRect.X = x;
            plan.NormalizedRect.Y = y;
            plan.NormalizedRect.Width = width;
            plan.NormalizedRect.Height = height;
            plan.NormalizedRect.MinDepth = minDepth;
            plan.NormalizedRect.MaxDepth = maxDepth;

            uint32_t pixelX = 0;
            uint32_t pixelY = 0;
            uint32_t pixelWidth = 0;
            uint32_t pixelHeight = 0;
            viewport.GetPixelRect(renderWidth, renderHeight, pixelX, pixelY, pixelWidth, pixelHeight);

            plan.PixelRect.X = static_cast<float>(pixelX);
            plan.PixelRect.Y = static_cast<float>(pixelY);
            plan.PixelRect.Width = static_cast<float>(pixelWidth);
            plan.PixelRect.Height = static_cast<float>(pixelHeight);
            plan.PixelRect.MinDepth = minDepth;
            plan.PixelRect.MaxDepth = maxDepth;

            plan.Scissor.Left = static_cast<int32_t>(pixelX);
            plan.Scissor.Top = static_cast<int32_t>(pixelY);
            plan.Scissor.Right = static_cast<int32_t>(pixelX + pixelWidth);
            plan.Scissor.Bottom = static_cast<int32_t>(pixelY + pixelHeight);

            CameraProxy camera = viewport.GetCamera();
            if (!camera.IsValid() && fallbackCamera)
            {
                camera = *fallbackCamera;
            }

            if (!camera.IsValid() && pixelWidth > 0 && pixelHeight > 0)
            {
                camera.Viewport.X = static_cast<float>(pixelX);
                camera.Viewport.Y = static_cast<float>(pixelY);
                camera.Viewport.Width = static_cast<float>(pixelWidth);
                camera.Viewport.Height = static_cast<float>(pixelHeight);
                camera.Viewport.MinDepth = minDepth;
                camera.Viewport.MaxDepth = maxDepth;
            }

            if (pixelHeight > 0)
            {
                camera.AspectRatio = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
            }

            plan.Camera = camera;
            plan.bHasCamera = camera.IsValid();

            return plan;
        }

        uint32_t AppendInstanceDataToPacket(FramePacket *packet,
                                            const Container::VariableArray<GPUSceneInstanceData> &instanceData)
        {
            if (!packet)
            {
                return 0;
            }

            const uint32_t baseInstance = static_cast<uint32_t>(packet->InstanceData.size());
            if (!instanceData.empty())
            {
                packet->InstanceData.insert(packet->InstanceData.end(),
                                            instanceData.begin(),
                                            instanceData.end());
            }
            return baseInstance;
        }

        CommandRange AppendRebasedDrawCommands(const Container::VariableArray<DrawCommand> &source,
                                               uint32_t baseInstance,
                                               Container::VariableArray<DrawCommand> &destination)
        {
            CommandRange range;
            if (source.empty())
            {
                return range;
            }

            range.First = static_cast<uint32_t>(destination.size());
            range.Count = static_cast<uint32_t>(source.size());
            destination.insert(destination.end(), source.begin(), source.end());

            if (baseInstance == 0)
            {
                return range;
            }

            const uint32_t rangeEnd = range.End();
            for (uint32_t index = range.First; index < rangeEnd; ++index)
            {
                DrawCommand &command = destination[index];
                if (!command.IsGraphicsCommand())
                {
                    continue;
                }

                command.Draw.FirstInstance += baseInstance;
                command.Draw.InstanceDataOffset += baseInstance;
            }

            return range;
        }

        CommandRange CombineCommandRanges(const CommandRange &opaqueRange,
                                          const CommandRange &transparentRange)
        {
            if (opaqueRange.IsEmpty())
            {
                return transparentRange;
            }

            if (transparentRange.IsEmpty())
            {
                return opaqueRange;
            }

            return {opaqueRange.First, opaqueRange.Count + transparentRange.Count};
        }

        uint32_t ResolveFrameIndex(const RHI::ISwapChain &swapChain)
        {
            const uint32_t frameIndex = swapChain.GetCurrentFrameIndex();
            [[maybe_unused]] const uint32_t maxFramesInFlight = swapChain.GetMaxFramesInFlight();
            assert(maxFramesInFlight > 0);
            assert(frameIndex < maxFramesInFlight);
            return frameIndex;
        }

    } // namespace

    // ========================================
    // RenderingCoordinator
    // ========================================

    bool RenderingCoordinator::Initialize(const RenderingCoordinatorSettings &settings)
    {
        if (m_bInitialized)
        {
            return true;
        }

        LOG_INFO("RenderingCoordinator::Initialize() - Starting initialization");

        m_Width = settings.Width;
        m_Height = settings.Height;
        m_RenderScale = std::clamp(settings.RenderScale, 0.5f, 1.0f);
        UpdateRenderResolution(m_Width, m_Height);
        m_bVSyncEnabled = settings.bVSync;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        m_MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;
        m_RenderGraph.SetDebugDumpOptions(settings.RenderGraphDumpOptions);

        // ========================================
        // 1. RHIデバイス（RenderWorldから渡される）
        // ========================================
        m_Device = settings.Device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "RHI Device is null");
            return false;
        }

        // ========================================
        // 2. Screenの初期化（SwapChain作成を含む）
        // ========================================
        ScreenSettings screenSettings;
        screenSettings.Width = settings.Width;
        screenSettings.Height = settings.Height;
        screenSettings.WindowHandle = settings.WindowHandle;
        screenSettings.bVSync = settings.bVSync;
        screenSettings.BackBufferCount = settings.BackBufferCount;

        if (!m_Screen.Initialize(m_Device, screenSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize Screen");
            return false;
        }

        // ========================================
        // 3. CommandList作成
        // ========================================
        m_CommandList = m_Device->CreateCommandList();
        if (!m_CommandList)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create command list");
            m_Screen.Shutdown();
            return false;
        }

        // ========================================
        // 4. RenderPass作成（Screen SwapChain用）
        // ========================================
        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Screen SwapChain is null");
            return false;
        }

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = swapChain->GetFormat();
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = true;
        // Cornflower blue clear color
        colorAttachment.clearColor[0] = 0.392f;
        colorAttachment.clearColor[1] = 0.584f;
        colorAttachment.clearColor[2] = 0.929f;
        colorAttachment.clearColor[3] = 1.0f;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::Present;

        RHI::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);

        // デプスステンシルアタッチメント
        RHI::AttachmentDesc depthAttachment;
        depthAttachment.format = RHI::Format::D32_FLOAT;
        depthAttachment.isDepthStencil = true;
        depthAttachment.clear = true;
        depthAttachment.clearDepth = 1.0f;
        depthAttachment.clearStencil = 0;
        depthAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        depthAttachment.storeOp = RHI::AttachmentStoreOp::DontCare;
        depthAttachment.initialState = RHI::ResourceState::Undefined;
        depthAttachment.finalState = RHI::ResourceState::DepthWrite;

        renderPassDesc.depthStencilAttachment = depthAttachment;
        renderPassDesc.hasDepthStencil = true;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create render pass");
            return false;
        }

        RHI::AttachmentDesc loadColorAttachment = colorAttachment;
        loadColorAttachment.clear = false;
        loadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadColorAttachment.initialState = RHI::ResourceState::Present;

        RHI::AttachmentDesc loadDepthAttachment = depthAttachment;
        loadDepthAttachment.clear = false;
        loadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc loadRenderPassDesc;
        loadRenderPassDesc.colorAttachments.push_back(loadColorAttachment);
        loadRenderPassDesc.depthStencilAttachment = loadDepthAttachment;
        loadRenderPassDesc.hasDepthStencil = true;

        m_PresentationLoadRenderPass = m_Device->CreateRenderPass(loadRenderPassDesc);
        if (!m_PresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create presentation load render pass");
            return false;
        }

        RHI::AttachmentDesc graphClearColorAttachment = colorAttachment;
        graphClearColorAttachment.initialState = RHI::ResourceState::RenderTarget;

        RHI::AttachmentDesc graphLoadColorAttachment = graphClearColorAttachment;
        graphLoadColorAttachment.clear = false;
        graphLoadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;

        RHI::AttachmentDesc graphClearDepthAttachment = depthAttachment;
        graphClearDepthAttachment.initialState = RHI::ResourceState::Undefined;

        RHI::AttachmentDesc graphLoadDepthAttachment = depthAttachment;
        graphLoadDepthAttachment.clear = false;
        graphLoadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        graphLoadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc graphClearRenderPassDesc;
        graphClearRenderPassDesc.colorAttachments.push_back(graphClearColorAttachment);
        graphClearRenderPassDesc.depthStencilAttachment = graphClearDepthAttachment;
        graphClearRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationClearRenderPass = m_Device->CreateRenderPass(graphClearRenderPassDesc);
        if (!m_GraphPresentationClearRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation clear render pass");
            return false;
        }

        RHI::RenderPassDesc graphLoadRenderPassDesc;
        graphLoadRenderPassDesc.colorAttachments.push_back(graphLoadColorAttachment);
        graphLoadRenderPassDesc.depthStencilAttachment = graphLoadDepthAttachment;
        graphLoadRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationLoadRenderPass = m_Device->CreateRenderPass(graphLoadRenderPassDesc);
        if (!m_GraphPresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation load render pass");
            return false;
        }
        m_SwapChainFormat = swapChain->GetFormat();

        // ========================================
        // 5. Framebuffers（スワップチェーンイメージごと）
        // ========================================
        if (!CreateSwapChainFramebuffers())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffers");
            return false;
        }

        // ========================================
        // 6. ShaderManagerの初期化
        // ========================================
        if (!m_ShaderManager.Initialize(m_Device.get(), NORVES_SHADER_DIR))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize ShaderManager");
            return false;
        }

        // Slangコンパイラの設定（Neural Shaders対応GPUの場合）
        if (m_Device->GetCapabilities().NeuralShaders.bSupported)
        {
            auto slangCompiler = m_Device->CreateSlangShaderCompiler();
            if (slangCompiler)
            {
                m_ShaderManager.SetSlangCompiler(slangCompiler);
            }
            else
            {
                NORVES_LOG_WARNING("RenderingCoordinator",
                                   "Device reports Neural Shaders support but no Slang compiler is available");
            }
        }

        // ========================================
        // 7. Blitシェーダーの作成（ToneMappedColor → SwapChain合成用）
        // ========================================
        {
            // Blit用頂点シェーダー（フルスクリーン三角形）
            m_BlitVertexShader = m_ShaderManager.LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
            if (!m_BlitVertexShader)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit vertex shader");
                return false;
            }

            // Blit用フラグメントシェーダー
            m_BlitFragmentShader = m_ShaderManager.LoadShader("blit.frag", RHI::ShaderStage::Pixel);
            if (!m_BlitFragmentShader)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit fragment shader");
                return false;
            }

            // Blitサンプラー
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_BlitSampler = m_Device->CreateSampler(samplerDesc);
            if (!m_BlitSampler)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit sampler");
                return false;
            }

            // Blitディスクリプタセット（binding 0: CombinedImageSampler）
            RHI::DescriptorSetDesc blitDsDesc;
            RHI::DescriptorBinding texBinding;
            texBinding.binding = 0;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(texBinding);

            m_BlitDescriptorSet = m_Device->CreateDescriptorSet(blitDsDesc);
            if (!m_BlitDescriptorSet)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit descriptor set");
                return false;
            }

            // サンプラーを事前バインド（CombinedImageSamplerに必要）
            m_BlitDescriptorSet->BindSampler(0, m_BlitSampler);

            // Blitパイプライン
            RHI::GraphicsPipelineDesc blitPipelineDesc;
            blitPipelineDesc.vertexShader = m_BlitVertexShader;
            blitPipelineDesc.pixelShader = m_BlitFragmentShader;
            blitPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            blitPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            blitPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            blitPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            blitPipelineDesc.rasterState.lineWidth = 1.0f;
            blitPipelineDesc.depthStencilState.depthTestEnable = false;
            blitPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blitBlend;
            blitBlend.blendEnable = false;
            blitBlend.colorWriteMask = RHI::ColorWriteMask::All;
            blitPipelineDesc.blendState.attachments.push_back(blitBlend);
            blitPipelineDesc.renderPass = m_RenderPass;
            blitPipelineDesc.descriptorSetLayouts.push_back(blitDsDesc);

            m_BlitPipeline = m_Device->CreateGraphicsPipeline(blitPipelineDesc);
            if (!m_BlitPipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit pipeline");
                return false;
            }

            NORVES_LOG_INFO("RenderingCoordinator", "Blit compositing resources created");
        }

        // 旧三角形シェーダー・パイプラインも残す（フォールバック用）
        {
            m_TriangleVertexShader = m_ShaderManager.LoadShader("triangle.vert", RHI::ShaderStage::Vertex);
            m_TriangleFragmentShader = m_ShaderManager.LoadShader("triangle.frag", RHI::ShaderStage::Pixel);

            RHI::GraphicsPipelineDesc triPipelineDesc;
            triPipelineDesc.vertexShader = m_TriangleVertexShader;
            triPipelineDesc.pixelShader = m_TriangleFragmentShader;
            triPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            triPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            triPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            triPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            triPipelineDesc.rasterState.lineWidth = 1.0f;
            triPipelineDesc.depthStencilState.depthTestEnable = false;
            triPipelineDesc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc triBlend;
            triBlend.blendEnable = false;
            triBlend.colorWriteMask = RHI::ColorWriteMask::All;
            triPipelineDesc.blendState.attachments.push_back(triBlend);
            triPipelineDesc.renderPass = m_RenderPass;
            m_TrianglePipeline = m_Device->CreateGraphicsPipeline(triPipelineDesc);
        }

        // ========================================
        // 11. メインSceneViewの作成
        // ========================================
        SceneViewSettings sceneViewSettings;
        sceneViewSettings.Width = settings.Width;
        sceneViewSettings.Height = settings.Height;

        m_MainSceneView = Container::MakeShared<SceneView>();
        if (!m_MainSceneView->Initialize(sceneViewSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize main SceneView");
            m_Screen.Shutdown();
            return false;
        }

        auto mainViewport = Container::MakeShared<Viewport>();
        ViewportSettings viewportSettings;
        viewportSettings.X = 0.0f;
        viewportSettings.Y = 0.0f;
        viewportSettings.Width = 1.0f;
        viewportSettings.Height = 1.0f;
        if (!mainViewport->Initialize(viewportSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize main Viewport");
            m_MainSceneView->Shutdown();
            m_MainSceneView.reset();
            m_Screen.Shutdown();
            return false;
        }
        m_MainSceneView->AddViewport(mainViewport);

        m_Screen.AddView(m_MainSceneView, 0);
        m_Views.push_back(m_MainSceneView);

        // ========================================
        // 12. SceneRendererの初期化
        // ========================================
        if (!m_TransientPool.Initialize(m_Device->GetResourceAllocator(), swapChain->GetMaxFramesInFlight()))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize TransientResourcePool");
            ReleaseInitializedResources();
            return false;
        }

        if (!m_RenderGraph.Initialize(&m_TransientPool))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize RenderGraph");
            ReleaseInitializedResources();
            return false;
        }

        if (!m_InstanceBufferRing.Initialize(m_Device.get(), swapChain->GetMaxFramesInFlight(), 1024))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize InstanceBufferRing");
            ReleaseInitializedResources();
            return false;
        }

        if (!m_SceneRenderer.Initialize(m_Device.get(), nullptr, &m_TransientPool))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize SceneRenderer");
            ReleaseInitializedResources();
            return false;
        }

        // デフォルトパイプラインは設定しない（球体描画で代替）
        // m_SceneRenderer.SetDefaultPipeline(m_TrianglePipeline);

        // ========================================
        // 12.5. ディファードパイプラインの構築
        // ========================================
        // SceneViewにDeferred描画パス（GBuffer→Lighting→ToneMapping）を登録
        m_MainSceneView->SetupDeferredPipeline(&m_SceneRenderer);
        NORVES_LOG_INFO("RenderingCoordinator", "Deferred pipeline configured on MainSceneView");

        // ========================================
        // 13. MeshProxyはWorldから自動登録される
        // ========================================

        // ========================================
        // 14. FramePacketManagerの初期化
        // ========================================
        m_PacketManager.Initialize();

        m_bInitialized = true;
        LOG_INFO("RenderingCoordinator::Initialize() - Initialization completed successfully");
        return true;
    }

    void RenderingCoordinator::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderingCoordinator::Shutdown() - Starting shutdown");

        ReleaseInitializedResources();

        m_bInitialized = false;
        LOG_INFO("RenderingCoordinator::Shutdown() - Shutdown completed");
    }

    void RenderingCoordinator::ReleaseInitializedResources()
    {
        // GPU処理の完了を待機
        if (m_Device)
        {
            m_Device->WaitIdle();
        }

        // Writing中のパケットを安全にキャンセル（シャットダウン前のGT書き込み中断）
        m_PacketManager.CancelInflightWrites();

        // Readyな未消費パケットも全て解放（シャットダウン後は消費されないため）
        m_PacketManager.DrainUnconsumedPackets();

        // PresentationPass は直近の backbuffer/renderpass/pipeline 参照を保持するため、
        // RHI shutdown 前に request/result を明示クリアする。
        m_PresentationPass.SetRequest(PresentationPassRequest{});

        // Viewの破棄
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Shutdown();
            }
        }
        m_Views.clear();
        m_MainSceneView.reset();

        // SceneRendererの終了
        m_SceneRenderer.Shutdown();

        // インスタンスデータSSBOリングの終了
        m_InstanceBufferRing.Shutdown();

        // RenderGraphの終了（一時リソースプールより先に破棄）
        m_RenderGraph.Shutdown();

        // 一時リソースプールの終了
        m_TransientPool.Shutdown();

        // シェーダーマネージャーの終了
        m_ShaderManager.Shutdown();

        // テスト三角形リソースの解放
        m_TrianglePipeline.reset();
        m_TriangleFragmentShader.reset();
        m_TriangleVertexShader.reset();

        // 3Dメッシュリソースの解放 → Blitリソースの解放
        m_BlitPipeline.reset();
        m_BlitDescriptorSet.reset();
        m_BlitSampler.reset();
        m_BlitFragmentShader.reset();
        m_BlitVertexShader.reset();
        m_DepthTexture.reset();

        // フレームバッファ・レンダーパスの解放
        m_SwapChainFramebuffers.clear();
        m_PresentationLoadFramebuffers.clear();
        m_GraphPresentationClearFramebuffers.clear();
        m_GraphPresentationLoadFramebuffers.clear();
        m_bSwapChainFramebuffersReady = false;
        m_RenderPass.reset();
        m_PresentationLoadRenderPass.reset();
        m_GraphPresentationClearRenderPass.reset();
        m_GraphPresentationLoadRenderPass.reset();

        // コマンドリストの解放
        m_CommandList.reset();

        // 共有リソースレジストリを明示的にクリア（テクスチャ参照をデバイス破棄前に解放）
        m_Screen.GetSharedResourceRegistry().Clear();

        // Screenの破棄（SwapChain解放を含む）
        m_Screen.Shutdown();

        // FramePacketManagerの終了
        m_PacketManager.Shutdown();

        // デバイス参照の解放（Engine層がRHIの終了を管理）
        m_Device.reset();
    }

    void RenderingCoordinator::BeginFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 現在時刻を取得
        auto now = std::chrono::high_resolution_clock::now();
        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

        // デルタタイム計算
        float deltaTime = static_cast<float>(currentTime - m_LastFrameTime);
        m_LastFrameTime = currentTime;
        m_TotalTime += deltaTime;

        // ST経路ではReadyパケットがRTに消費されないため、フレーム開始時に再利用する。
        // MT経路ではReady→QueuedのハンドオフをRT側が行うため、ここでは触らない。
        if (!m_bMultiThreadedRendering)
        {
            m_PacketManager.DrainUnconsumedPackets();
        }

        // 書き込み用パケットを取得
        m_CurrentPacket = m_PacketManager.AcquireForWrite();
        if (!m_CurrentPacket)
        {
            NORVES_LOG_WARNING("RenderingCoordinator",
                               "BeginFrame: all FramePacket slots occupied, skipping packet acquisition this frame");
        }
        if (m_CurrentPacket)
        {
            m_CurrentPacket->FrameNumber = m_Stats.FrameNumber;
            m_CurrentPacket->DeltaTime = deltaTime;
            m_CurrentPacket->TotalTime = m_TotalTime;
        }

        m_Stats.DeltaTime = deltaTime;
        if (deltaTime > 0.0f)
        {
            m_Stats.FPS = 1.0f / deltaTime;
        }

        // Screen.BeginFrame（swapchain acquire）はRenderFrame内に移動。
        // GT側ではパケット取得のみ行い、swapchainへの触れは行わない。
    }

    void RenderingCoordinator::CollectScene()
    {
        if (!m_bInitialized || !m_CurrentPacket)
        {
            return;
        }

        NORVES_STAT_TIME_START(collection);

        // 各SceneViewでシーン収集
        // 注: ProxyはWorldからSceneViewに直接渡されているため、
        //     ここでは統計情報の更新のみ行う
        for (auto &view : m_Views)
        {
            if (!view)
            {
                continue;
            }

            // SceneViewのProxy情報は既にWorldから設定済み
        }

        NORVES_STAT_TIME_END(collection, m_Stats.CollectionTimeMs);
    }

    void RenderingCoordinator::GenerateDrawCommands()
    {
        if (!m_bInitialized)
        {
            return;
        }

        NORVES_STAT_TIME_START(cmdGen);

        if (m_CurrentPacket)
        {
            m_CurrentPacket->bHasMainCamera = false;
            if (m_bCameraSet)
            {
                auto mainCamera = m_MainCamera;
                if (!mainCamera.IsValid())
                {
                    mainCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
                    mainCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
                }

                m_CurrentPacket->Scene.MainCamera = mainCamera;
                m_CurrentPacket->bHasMainCamera = true;
            }

            if (m_MainSceneView)
            {
                m_CurrentPacket->Scene.MeshProxies = m_MainSceneView->GetMeshProxies();
                m_CurrentPacket->Scene.LightProxies = m_MainSceneView->GetLightProxies();
                m_CurrentPacket->Scene.MegaGeometryProxies = m_MainSceneView->GetMegaGeometryProxies();
            }

            m_CurrentPacket->DrawCommands.clear();
            m_CurrentPacket->DrawCommands.reserve(m_MaxDrawCallsPerFrame);
            m_CurrentPacket->DrawCommandRange = CommandRange{};
            m_CurrentPacket->OpaqueCommandRange = CommandRange{};
            m_CurrentPacket->TransparentCommandRange = CommandRange{};
            m_CurrentPacket->InstanceData.clear();
            m_CurrentPacket->Views.clear();
        }

        bool bLegacyCommandsSet = false;
        const auto &screenViews = m_Screen.GetViews();
        for (uint32_t viewIndex = 0; viewIndex < screenViews.size(); ++viewIndex)
        {
            const auto &view = screenViews[viewIndex];
            if (!view)
            {
                continue;
            }

            ViewRenderPlan viewPlan;
            viewPlan.ViewId = viewIndex;
            viewPlan.ViewType = static_cast<uint8_t>(view->GetViewType());
            viewPlan.Priority = static_cast<int32_t>(viewIndex);
            viewPlan.bEnabled = view->IsEnabled();

            auto sceneView = Container::DynamicPointerCast<SceneView>(view);
            const bool bIsMainSceneView = sceneView && sceneView == m_MainSceneView;
            const uint32_t viewportCount = view->GetViewportCount();

            if (sceneView && viewportCount == 0 && view->IsEnabled())
            {
                // Viewport未作成時の互換フォールバック。
                sceneView->PrepareDrawCommands();

                const uint32_t instanceBase =
                    AppendInstanceDataToPacket(m_CurrentPacket, sceneView->GetInstanceData());

                CommandRange opaqueCommandRange;
                CommandRange transparentCommandRange;
                CommandRange drawCommandRange;
                if (m_CurrentPacket)
                {
                    opaqueCommandRange =
                        AppendRebasedDrawCommands(sceneView->GetOpaqueCommands(),
                                                  instanceBase,
                                                  m_CurrentPacket->DrawCommands);
                    transparentCommandRange =
                        AppendRebasedDrawCommands(sceneView->GetTransparentCommands(),
                                                  instanceBase,
                                                  m_CurrentPacket->DrawCommands);
                    drawCommandRange = CombineCommandRanges(opaqueCommandRange, transparentCommandRange);
                }

                if (m_CurrentPacket && bIsMainSceneView && !bLegacyCommandsSet)
                {
                    m_CurrentPacket->DrawCommandRange = drawCommandRange;
                    m_CurrentPacket->OpaqueCommandRange = opaqueCommandRange;
                    m_CurrentPacket->TransparentCommandRange = transparentCommandRange;
                    bLegacyCommandsSet = true;
                }
            }

            for (uint32_t viewportIndex = 0; viewportIndex < viewportCount; ++viewportIndex)
            {
                auto viewport = view->GetViewport(viewportIndex);
                if (!viewport)
                {
                    continue;
                }

                const CameraProxy *fallbackCamera =
                    (bIsMainSceneView && m_bCameraSet) ? &m_MainCamera : nullptr;
                ViewportRenderPlan viewportPlan = BuildViewportRenderPlan(*viewport,
                                                                          viewIndex,
                                                                          viewportIndex,
                                                                          m_RenderWidth,
                                                                          m_RenderHeight,
                                                                          fallbackCamera);

                if (sceneView && view->IsEnabled() && viewportPlan.HasDrawableExtent())
                {
                    sceneView->PrepareDrawCommandsForViewport(viewportPlan);

                    const uint32_t instanceBase =
                        AppendInstanceDataToPacket(m_CurrentPacket, sceneView->GetInstanceData());
                    if (m_CurrentPacket)
                    {
                        viewportPlan.OpaqueCommandRange =
                            AppendRebasedDrawCommands(sceneView->GetOpaqueCommands(),
                                                      instanceBase,
                                                      m_CurrentPacket->DrawCommands);
                        viewportPlan.TransparentCommandRange =
                            AppendRebasedDrawCommands(sceneView->GetTransparentCommands(),
                                                      instanceBase,
                                                      m_CurrentPacket->DrawCommands);
                        viewportPlan.DrawCommandRange =
                            CombineCommandRanges(viewportPlan.OpaqueCommandRange,
                                                 viewportPlan.TransparentCommandRange);
                    }

                    if (m_CurrentPacket && bIsMainSceneView && !bLegacyCommandsSet)
                    {
                        m_CurrentPacket->DrawCommandRange = viewportPlan.DrawCommandRange;
                        m_CurrentPacket->OpaqueCommandRange = viewportPlan.OpaqueCommandRange;
                        m_CurrentPacket->TransparentCommandRange = viewportPlan.TransparentCommandRange;
                        bLegacyCommandsSet = true;
                    }
                }

                viewPlan.Viewports.push_back(viewportPlan);
            }

            if (m_CurrentPacket)
            {
                m_CurrentPacket->Views.push_back(viewPlan);
            }
        }

        NORVES_STAT_TIME_END(cmdGen, m_Stats.CommandGenerationTimeMs);

        NORVES_STAT_ADD(m_Stats.DrawCalls,
                        m_CurrentPacket ? static_cast<uint32_t>(m_CurrentPacket->DrawCommands.size()) : 0u);
    }

    FramePacket* RenderingCoordinator::EndFrame()
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        // 書き込み完了をマーク（Writing→Ready）
        // Screen.EndFrame（submit/present）はRenderFrame内で実行するため、ここでは行わない。
        FramePacket* finishedPacket = m_CurrentPacket;
        if (m_CurrentPacket)
        {
            m_PacketManager.FinishWrite(m_CurrentPacket);
            m_CurrentPacket = nullptr;
        }

        m_Stats.FrameNumber++;
        return finishedPacket;
    }

    void RenderingCoordinator::RenderFrame(FramePacket *packet)
    {
        if (!m_bInitialized || !packet)
        {
            return;
        }

#if NORVES_ENABLE_STATS
        auto &statsManager = NorvesLib::Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point renderFrameStartTime;
        if (bTraceActive)
        {
            renderFrameStartTime = std::chrono::high_resolution_clock::now();
        }
#endif

        bool bCanReadPacket = packet->GetState() == FramePacketState::Reading;
        if (!bCanReadPacket)
        {
            bCanReadPacket = packet->CompareExchangeState(FramePacketState::Queued, FramePacketState::Reading) ||
                             packet->CompareExchangeState(FramePacketState::Ready, FramePacketState::Reading);
        }
        if (!bCanReadPacket)
        {
            return;
        }

        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain)
        {
            return;
        }

        // swapchain acquire（旧BeginFrame経路から移動）
        // RenderThreadまたはSTインライン経路でここを呼ぶ。
        if (!m_Screen.BeginFrame())
        {
            const uint32_t swapChainWidth = swapChain->GetWidth();
            const uint32_t swapChainHeight = swapChain->GetHeight();

            m_Width = swapChainWidth;
            m_Height = swapChainHeight;
            UpdateRenderResolution(swapChainWidth, swapChainHeight);

            if (!RecreateSwapChainPresentationResources())
            {
                NORVES_LOG_ERROR("RenderingCoordinator",
                                 "Failed to recreate swapchain presentation resources after acquire failure");
            }

            for (auto &view : m_Views)
            {
                if (view)
                {
                    view->Resize(swapChainWidth, swapChainHeight);
                }
            }

            return;
        }

        const uint32_t frameIndex = ResolveFrameIndex(*swapChain);
        m_TransientPool.BeginFrame(frameIndex);
        m_RenderGraph.BeginFrame(frameIndex);
        RHI::BufferPtr instanceDataBuffer = m_InstanceBufferRing.Upload(frameIndex, packet->InstanceData);

        uint32_t imageIndex = swapChain->GetCurrentBackBufferIndex();

        if (!m_bSwapChainFramebuffersReady && !RecreateSwapChainPresentationResources())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Swapchain presentation resources are not ready");
            m_TransientPool.EndFrame();
            return;
        }

        if (imageIndex >= m_SwapChainFramebuffers.size() ||
            imageIndex >= m_PresentationLoadFramebuffers.size() ||
            imageIndex >= m_GraphPresentationClearFramebuffers.size() ||
            imageIndex >= m_GraphPresentationLoadFramebuffers.size())
        {
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "Swapchain framebuffer index out of range: image=%u framebufferCount=%zu loadFramebufferCount=%zu graphClearFramebufferCount=%zu graphLoadFramebufferCount=%zu",
                             imageIndex,
                             m_SwapChainFramebuffers.size(),
                             m_PresentationLoadFramebuffers.size(),
                             m_GraphPresentationClearFramebuffers.size(),
                             m_GraphPresentationLoadFramebuffers.size());
            m_TransientPool.EndFrame();
            return;
        }

        // フレーム別コマンドバッファを選択（ダブルバッファリングでの同期問題を回避）
        m_CommandList->SetFrameIndex(frameIndex);

        // SceneRendererフレーム開始
        m_SceneRenderer.BeginFrame();

        // コマンド録画開始
        m_CommandList->BeginRecording();

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            const float latestGPUTimeMs = m_CommandList->GetLastGPUTimestampDurationMs();
            if (latestGPUTimeMs > 0.0f)
            {
                m_Stats.GPUTimeMs = latestGPUTimeMs;
                statsManager.SetGPUFrameTimeMs(latestGPUTimeMs);
            }

            if (m_CommandList->SupportsGPUTimestamps())
            {
                m_CommandList->BeginGPUTimestamp("FrameGPU");
            }
        }
#endif

        // ========================================
        // Deferredパスチェーン描画（スワップチェーンレンダーパスの外で実行）
        // 各パスが独自のレンダーパスを開閉する
        // ========================================

        // ViewRenderContextを構築（パスチェーン対応の新フロー）
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        ViewRenderContext viewContext;
        viewContext.CommandList = m_CommandList.get();
        viewContext.InstanceDataBuffer = instanceDataBuffer;
        viewContext.Device = m_Device.get();
        viewContext.TransientPool = &m_TransientPool;
        viewContext.SharedResources = &m_Screen.GetSharedResourceRegistry();
        viewContext.CurrentRenderPass = m_RenderPass.get();
        viewContext.CurrentFramebuffer = m_SwapChainFramebuffers[imageIndex].get();
        viewContext.bRenderPassActive = false; // Deferredパスは独自のレンダーパスを使用
        viewContext.FrameIndex = frameIndex;
        viewContext.ScreenWidth = swapChain->GetWidth();
        viewContext.ScreenHeight = swapChain->GetHeight();
        viewContext.RenderWidth = m_RenderWidth;
        viewContext.RenderHeight = m_RenderHeight;
        viewContext.DeltaTime = static_cast<float>(m_Stats.TotalFrameTimeMs * 0.001);
        viewContext.TotalTime = m_TotalTime;
        if (m_RenderResources)
        {
            viewContext.Resources.Gpu = &m_RenderResources->Gpu();
            viewContext.Resources.Textures = &m_RenderResources->Textures();
            viewContext.Resources.Materials = &m_RenderResources->Materials();
            viewContext.Resources.Meshes = &m_RenderResources->Meshes();
            viewContext.Resources.MegaGeometry = &m_RenderResources->MegaGeometry();
        }
        viewContext.ShaderMgr = &m_ShaderManager;
        viewContext.Capabilities = &m_Device->GetCapabilities();
        viewContext.Renderer = &m_SceneRenderer;
        viewContext.PendingFrameCommands = &pendingFrameCommands;
        viewContext.Graph = &m_RenderGraph;

        // フレームパケットからスナップショットを設定（RenderThread読み取り専用）
        viewContext.MainCamera = packet->bHasMainCamera ? &packet->Scene.MainCamera : nullptr;
        viewContext.SnapshotDrawCommandSource = &packet->DrawCommands;
        viewContext.SnapshotDrawCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                      packet->DrawCommandRange);
        viewContext.SnapshotOpaqueCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                        packet->OpaqueCommandRange);
        viewContext.SnapshotTransparentCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                             packet->TransparentCommandRange);
        viewContext.SnapshotLightProxies = &packet->Scene.LightProxies;
        viewContext.SnapshotMegaGeometryProxies = &packet->Scene.MegaGeometryProxies;

        PresentationComposer presentationComposer;
        const auto &screenViews = m_Screen.GetViews();
        PresentationComposeRequest presentationRequest;
        presentationRequest.Context = &viewContext;
        presentationRequest.Renderer = &m_SceneRenderer;
        presentationRequest.CommandList = m_CommandList.get();
        presentationRequest.ClearRenderPass = m_RenderPass;
        presentationRequest.LoadRenderPass = m_PresentationLoadRenderPass;
        presentationRequest.ClearFramebuffer = m_SwapChainFramebuffers[imageIndex];
        presentationRequest.LoadFramebuffer = m_PresentationLoadFramebuffers[imageIndex];
        presentationRequest.BlitPipeline = m_BlitPipeline;
        presentationRequest.BlitDescriptorSet = m_BlitDescriptorSet;
        presentationRequest.BlitSampler = m_BlitSampler;

        PresentationPassRequest graphPresentationRequest;
        graphPresentationRequest.BackBufferTexture = swapChain->GetBackBuffer(imageIndex);
        graphPresentationRequest.ClearRenderPass = m_GraphPresentationClearRenderPass;
        graphPresentationRequest.LoadRenderPass = m_GraphPresentationLoadRenderPass;
        graphPresentationRequest.ClearFramebuffer = m_GraphPresentationClearFramebuffers[imageIndex];
        graphPresentationRequest.LoadFramebuffer = m_GraphPresentationLoadFramebuffers[imageIndex];
        graphPresentationRequest.BlitPipeline = m_BlitPipeline;
        graphPresentationRequest.BlitDescriptorSet = m_BlitDescriptorSet;
        graphPresentationRequest.BlitSampler = m_BlitSampler;

        RenderFrameExecutionRequest executionRequest;
        executionRequest.Packet = packet;
        executionRequest.Views = &screenViews;
        executionRequest.FallbackView = m_MainSceneView.get();
        executionRequest.Context = &viewContext;
        executionRequest.Renderer = &m_SceneRenderer;
        executionRequest.CommandList = m_CommandList.get();
        executionRequest.PendingFrameCommands = &pendingFrameCommands;
        executionRequest.Presentation = &presentationComposer;
        executionRequest.PresentationRequest = presentationRequest;
        executionRequest.PresentationGraphPass = &m_PresentationPass;
        executionRequest.GraphPresentationRequest = graphPresentationRequest;

        RenderFrameExecutor frameExecutor;
        frameExecutor.Execute(executionRequest);
        m_Stats.RenderGraphBarrierCount = m_RenderGraph.GetLastCompiledBarrierCount();
        m_Stats.RenderGraphTransientAcquireCount = m_RenderGraph.GetLastTransientAcquireCount();

        // コマンド録画終了
#if NORVES_ENABLE_STATS
        if (bTraceActive && m_CommandList->SupportsGPUTimestamps())
        {
            m_CommandList->EndGPUTimestamp();
        }
#endif
        m_CommandList->End();

        // SceneRendererフレーム終了
        m_SceneRenderer.EndFrame();
        m_TransientPool.EndFrame();

        // 統計更新
        const auto &rendererStats = m_SceneRenderer.GetStats();
        m_Stats.DrawCalls = rendererStats.DrawCallCount;
        m_Stats.TrianglesRendered = rendererStats.TriangleCount;

        // コマンドリストをサブミット＆Present（旧EndFrame経路から移動）
        m_Screen.EndFrame(m_CommandList);

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            auto renderFrameEndTime = std::chrono::high_resolution_clock::now();
            m_Stats.RenderFrameTimeMs =
                std::chrono::duration<float, std::milli>(renderFrameEndTime - renderFrameStartTime).count();
            m_Stats.TotalFrameTimeMs = std::max(std::max(m_Stats.GameThreadTimeMs, m_Stats.RenderThreadTimeMs),
                                                std::max(m_Stats.RenderFrameTimeMs, m_Stats.GPUTimeMs));
            statsManager.SetRenderFrameTimeMs(m_Stats.RenderFrameTimeMs);
            statsManager.UpdateRenderingStats(m_Stats);
        }
#endif

        const bool bPresentationDirty = swapChain->ConsumePresentationDirty();
        const uint32_t presentWidth = swapChain->GetWidth();
        const uint32_t presentHeight = swapChain->GetHeight();
        const uint32_t presentBackBufferCount = swapChain->GetBufferCount();
        const RHI::Format presentFormat = swapChain->GetFormat();
        if (bPresentationDirty ||
            presentWidth != m_Width ||
            presentHeight != m_Height ||
            presentFormat != m_SwapChainFormat ||
            presentBackBufferCount != m_SwapChainFramebuffers.size())
        {
            m_Width = presentWidth;
            m_Height = presentHeight;
            m_SwapChainFormat = presentFormat;
            m_bSwapChainFramebuffersReady = false;
            m_SwapChainFramebuffers.clear();
            m_PresentationLoadFramebuffers.clear();
            m_GraphPresentationClearFramebuffers.clear();
            m_GraphPresentationLoadFramebuffers.clear();
            m_DepthTexture.reset();

            UpdateRenderResolution(presentWidth, presentHeight);
            for (auto &view : m_Views)
            {
                if (view)
                {
                    view->Resize(presentWidth, presentHeight);
                }
            }
        }
    }

    void RenderingCoordinator::ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands)
    {
        if (!m_bInitialized || commands.empty())
        {
            return;
        }

        // SceneRenderer経由でRHI描画コールを発行
        m_SceneRenderer.ExecuteDrawCommands(commands, m_CommandList.get());
    }

    void RenderingCoordinator::SubmitToGPU()
    {
        // Screen::EndFrame()内でSwapChain::EndFrame(commandList)がsubmit+presentを行うため、
        // この関数は将来的なマルチスレッドレンダリング用に予約
    }

    void RenderingCoordinator::Present()
    {
        // Screen::EndFrame()内でSwapChain::EndFrame(commandList)がsubmit+presentを行うため、
        // この関数は将来的なマルチスレッドレンダリング用に予約
    }

    void RenderingCoordinator::ReleasePacket(FramePacket *packet)
    {
        if (packet)
        {
            m_PacketManager.FinishRead(packet);
        }
    }

    Container::TSharedPtr<SceneView> RenderingCoordinator::CreateSceneView(const SceneViewSettings &settings)
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        auto sceneView = Container::MakeShared<SceneView>();
        if (!sceneView->Initialize(settings))
        {
            return nullptr;
        }

        m_Screen.AddView(sceneView, 0);
        m_Views.push_back(sceneView);
        return sceneView;
    }

    void RenderingCoordinator::DestroyView(Container::TSharedPtr<View> view)
    {
        if (!m_bInitialized || !view)
        {
            return;
        }

        // Screenから削除
        m_Screen.RemoveView(view);

        // リストから削除
        auto it = std::find(m_Views.begin(), m_Views.end(), view);
        if (it != m_Views.end())
        {
            (*it)->Shutdown();
            m_Views.erase(it);
        }
    }

    void RenderingCoordinator::SetMainCamera(const CameraProxy &camera)
    {
        m_MainCamera = camera;
        if (!m_MainCamera.IsValid())
        {
            m_MainCamera.Viewport.X = 0.0f;
            m_MainCamera.Viewport.Y = 0.0f;
            m_MainCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
            m_MainCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
            m_MainCamera.Viewport.MinDepth = 0.0f;
            m_MainCamera.Viewport.MaxDepth = 1.0f;
        }

        if (m_MainSceneView)
        {
            auto mainViewport = m_MainSceneView->GetMainViewport();
            if (mainViewport)
            {
                mainViewport->SetCamera(m_MainCamera);
            }
        }

        m_bCameraSet = true;
    }

    void RenderingCoordinator::Resize(uint32_t width, uint32_t height)
    {
        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderingCoordinator::Resize(%u, %u)", width, height);

        // GPU処理の完了を待機
        if (m_Device)
        {
            m_Device->WaitIdle();
        }

        // リサイズ前にWriting中のパケットをキャンセル
        // （新しいフレームバッファが確保されるまでGTの書き込みを止める）
        m_PacketManager.CancelInflightWrites();

        // Readyな未消費パケットも解放（リサイズ後はフレームデータが無効になるため）
        m_PacketManager.DrainUnconsumedPackets();

        m_Width = width;
        m_Height = height;
        UpdateRenderResolution(width, height);

        // Screenのリサイズ（SwapChainリサイズを含む）
        m_Screen.Resize(width, height);

        // フレームバッファの再作成（デプスバッファも含む）
        if (!RecreateSwapChainPresentationResources())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate swapchain presentation resources during resize");
        }

        // 各Viewのリサイズ
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Resize(width, height);
            }
        }
    }

    void RenderingCoordinator::SetRenderScale(float renderScale)
    {
        float clampedRenderScale = std::clamp(renderScale, 0.5f, 1.0f);
        if (std::abs(m_RenderScale - clampedRenderScale) < 0.0001f)
        {
            return;
        }

        m_RenderScale = clampedRenderScale;
        UpdateRenderResolution(m_Width, m_Height);

        if (m_bInitialized)
        {
            Resize(m_Width, m_Height);
        }
    }

    void RenderingCoordinator::UpdateRenderResolution(uint32_t screenWidth, uint32_t screenHeight)
    {
        m_RenderWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(screenWidth) * static_cast<double>(m_RenderScale))));
        m_RenderHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(screenHeight) * static_cast<double>(m_RenderScale))));
    }

    bool RenderingCoordinator::CreateSwapChainFramebuffers()
    {
        m_bSwapChainFramebuffersReady = false;
        m_SwapChainFramebuffers.clear();
        m_PresentationLoadFramebuffers.clear();
        m_GraphPresentationClearFramebuffers.clear();
        m_GraphPresentationLoadFramebuffers.clear();
        m_DepthTexture.reset();

        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain ||
            !m_RenderPass ||
            !m_PresentationLoadRenderPass ||
            !m_GraphPresentationClearRenderPass ||
            !m_GraphPresentationLoadRenderPass)
        {
            return false;
        }

        // デプステクスチャの作成
        RHI::TextureDesc depthDesc = RHI::TextureDesc::DepthStencil(
            swapChain->GetWidth(), swapChain->GetHeight(),
            RHI::Format::D32_FLOAT, "DepthBuffer");
        m_DepthTexture = m_Device->CreateTexture(depthDesc);
        if (!m_DepthTexture)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create depth texture");
            return false;
        }

        uint32_t imageCount = swapChain->GetBufferCount();
        m_SwapChainFramebuffers.reserve(imageCount);
        m_PresentationLoadFramebuffers.reserve(imageCount);
        m_GraphPresentationClearFramebuffers.reserve(imageCount);
        m_GraphPresentationLoadFramebuffers.reserve(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            RHI::FramebufferDesc fbDesc;
            fbDesc.colorTargets.push_back(swapChain->GetBackBuffer(i));
            fbDesc.depthStencilTarget = m_DepthTexture;
            fbDesc.renderPass = m_RenderPass;
            fbDesc.width = swapChain->GetWidth();
            fbDesc.height = swapChain->GetHeight();

            auto framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!framebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffer for swapchain image %u", i);
                return false;
            }

            m_SwapChainFramebuffers.push_back(framebuffer);

            fbDesc.renderPass = m_PresentationLoadRenderPass;
            auto loadFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!loadFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create presentation load framebuffer for swapchain image %u", i);
                return false;
            }

            m_PresentationLoadFramebuffers.push_back(loadFramebuffer);

            fbDesc.renderPass = m_GraphPresentationClearRenderPass;
            auto graphClearFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!graphClearFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation clear framebuffer for swapchain image %u", i);
                return false;
            }

            m_GraphPresentationClearFramebuffers.push_back(graphClearFramebuffer);

            fbDesc.renderPass = m_GraphPresentationLoadRenderPass;
            auto graphLoadFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!graphLoadFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation load framebuffer for swapchain image %u", i);
                return false;
            }

            m_GraphPresentationLoadFramebuffers.push_back(graphLoadFramebuffer);
        }

        LOG_INFO("Created %u swapchain framebuffers with depth buffer", imageCount);
        m_bSwapChainFramebuffersReady = true;
        return true;
    }

    bool RenderingCoordinator::RecreateSwapChainPresentationResources()
    {
        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain || !m_Device)
        {
            return false;
        }

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = swapChain->GetFormat();
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = true;
        colorAttachment.clearColor[0] = 0.392f;
        colorAttachment.clearColor[1] = 0.584f;
        colorAttachment.clearColor[2] = 0.929f;
        colorAttachment.clearColor[3] = 1.0f;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::Present;

        RHI::AttachmentDesc depthAttachment;
        depthAttachment.format = RHI::Format::D32_FLOAT;
        depthAttachment.isDepthStencil = true;
        depthAttachment.clear = true;
        depthAttachment.clearDepth = 1.0f;
        depthAttachment.clearStencil = 0;
        depthAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        depthAttachment.storeOp = RHI::AttachmentStoreOp::DontCare;
        depthAttachment.initialState = RHI::ResourceState::Undefined;
        depthAttachment.finalState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.depthStencilAttachment = depthAttachment;
        renderPassDesc.hasDepthStencil = true;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate swapchain render pass");
            return false;
        }

        RHI::AttachmentDesc loadColorAttachment = colorAttachment;
        loadColorAttachment.clear = false;
        loadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadColorAttachment.initialState = RHI::ResourceState::Present;

        RHI::AttachmentDesc loadDepthAttachment = depthAttachment;
        loadDepthAttachment.clear = false;
        loadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc loadRenderPassDesc;
        loadRenderPassDesc.colorAttachments.push_back(loadColorAttachment);
        loadRenderPassDesc.depthStencilAttachment = loadDepthAttachment;
        loadRenderPassDesc.hasDepthStencil = true;

        m_PresentationLoadRenderPass = m_Device->CreateRenderPass(loadRenderPassDesc);
        if (!m_PresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate presentation load render pass");
            return false;
        }

        RHI::AttachmentDesc graphClearColorAttachment = colorAttachment;
        graphClearColorAttachment.initialState = RHI::ResourceState::RenderTarget;

        RHI::AttachmentDesc graphLoadColorAttachment = graphClearColorAttachment;
        graphLoadColorAttachment.clear = false;
        graphLoadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;

        RHI::AttachmentDesc graphClearDepthAttachment = depthAttachment;
        graphClearDepthAttachment.initialState = RHI::ResourceState::Undefined;

        RHI::AttachmentDesc graphLoadDepthAttachment = depthAttachment;
        graphLoadDepthAttachment.clear = false;
        graphLoadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        graphLoadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc graphClearRenderPassDesc;
        graphClearRenderPassDesc.colorAttachments.push_back(graphClearColorAttachment);
        graphClearRenderPassDesc.depthStencilAttachment = graphClearDepthAttachment;
        graphClearRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationClearRenderPass = m_Device->CreateRenderPass(graphClearRenderPassDesc);
        if (!m_GraphPresentationClearRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate graph presentation clear render pass");
            return false;
        }

        RHI::RenderPassDesc graphLoadRenderPassDesc;
        graphLoadRenderPassDesc.colorAttachments.push_back(graphLoadColorAttachment);
        graphLoadRenderPassDesc.depthStencilAttachment = graphLoadDepthAttachment;
        graphLoadRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationLoadRenderPass = m_Device->CreateRenderPass(graphLoadRenderPassDesc);
        if (!m_GraphPresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate graph presentation load render pass");
            return false;
        }
        m_SwapChainFormat = swapChain->GetFormat();

        if (m_BlitVertexShader && m_BlitFragmentShader && m_BlitDescriptorSet)
        {
            RHI::DescriptorSetDesc blitDsDesc;
            RHI::DescriptorBinding texBinding;
            texBinding.binding = 0;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(texBinding);

            RHI::GraphicsPipelineDesc blitPipelineDesc;
            blitPipelineDesc.vertexShader = m_BlitVertexShader;
            blitPipelineDesc.pixelShader = m_BlitFragmentShader;
            blitPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            blitPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            blitPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            blitPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            blitPipelineDesc.rasterState.lineWidth = 1.0f;
            blitPipelineDesc.depthStencilState.depthTestEnable = false;
            blitPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blitBlend;
            blitBlend.blendEnable = false;
            blitBlend.colorWriteMask = RHI::ColorWriteMask::All;
            blitPipelineDesc.blendState.attachments.push_back(blitBlend);
            blitPipelineDesc.renderPass = m_RenderPass;
            blitPipelineDesc.descriptorSetLayouts.push_back(blitDsDesc);

            m_BlitPipeline = m_Device->CreateGraphicsPipeline(blitPipelineDesc);
            if (!m_BlitPipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate blit pipeline");
                return false;
            }
        }

        if (m_TriangleVertexShader && m_TriangleFragmentShader)
        {
            RHI::GraphicsPipelineDesc triPipelineDesc;
            triPipelineDesc.vertexShader = m_TriangleVertexShader;
            triPipelineDesc.pixelShader = m_TriangleFragmentShader;
            triPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            triPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            triPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            triPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            triPipelineDesc.rasterState.lineWidth = 1.0f;
            triPipelineDesc.depthStencilState.depthTestEnable = false;
            triPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc triBlend;
            triBlend.blendEnable = false;
            triBlend.colorWriteMask = RHI::ColorWriteMask::All;
            triPipelineDesc.blendState.attachments.push_back(triBlend);
            triPipelineDesc.renderPass = m_RenderPass;

            m_TrianglePipeline = m_Device->CreateGraphicsPipeline(triPipelineDesc);
            if (!m_TrianglePipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate triangle pipeline");
                return false;
            }
        }

        const bool bCreated = CreateSwapChainFramebuffers();
        if (bCreated)
        {
            swapChain->ConsumePresentationDirty();
        }
        return bCreated;
    }

} // namespace NorvesLib::Core::Rendering
