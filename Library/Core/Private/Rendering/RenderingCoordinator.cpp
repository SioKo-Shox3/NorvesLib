#include "Rendering/RenderingCoordinator.h"
#include "Rendering/Screen.h"
#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FramePacket.h"
#include "Rendering/Shaders/TriangleShaders.h"
#include "RHI/IDevice.h"
#include "RHI/ISwapChain.h"
#include "RHI/ICommandList.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <chrono>
#include <algorithm>

namespace NorvesLib::Core::Rendering
{

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
        m_bVSyncEnabled = settings.bVSync;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        m_MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;

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
        renderPassDesc.hasDepthStencil = false;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create render pass");
            return false;
        }

        // ========================================
        // 5. Framebuffers（スワップチェーンイメージごと）
        // ========================================
        if (!CreateSwapChainFramebuffers())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffers");
            return false;
        }

        // ========================================
        // 6. テスト三角形用シェーダー
        // ========================================
        RHI::ShaderDesc vertexShaderDesc;
        vertexShaderDesc.stage = RHI::ShaderStage::Vertex;
        vertexShaderDesc.entryPoint = "main";
        vertexShaderDesc.byteCode.assign(
            TriangleVertexShaderSpirV,
            TriangleVertexShaderSpirV + sizeof(TriangleVertexShaderSpirV));

        m_TriangleVertexShader = m_Device->CreateShader(vertexShaderDesc);
        if (!m_TriangleVertexShader)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create triangle vertex shader");
            return false;
        }

        RHI::ShaderDesc fragmentShaderDesc;
        fragmentShaderDesc.stage = RHI::ShaderStage::Pixel;
        fragmentShaderDesc.entryPoint = "main";
        fragmentShaderDesc.byteCode.assign(
            TriangleFragmentShaderSpirV,
            TriangleFragmentShaderSpirV + sizeof(TriangleFragmentShaderSpirV));

        m_TriangleFragmentShader = m_Device->CreateShader(fragmentShaderDesc);
        if (!m_TriangleFragmentShader)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create triangle fragment shader");
            return false;
        }

        // ========================================
        // 7. テスト三角形用グラフィックスパイプライン
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_TriangleVertexShader;
        pipelineDesc.pixelShader = m_TriangleFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_RenderPass;

        m_TrianglePipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_TrianglePipeline)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create triangle graphics pipeline");
            return false;
        }

        // ========================================
        // 8. メインSceneViewの作成
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

        m_Screen.AddView(m_MainSceneView, 0);
        m_Views.push_back(m_MainSceneView);

        // ========================================
        // 9. SceneRendererの初期化
        // ========================================
        if (!m_SceneRenderer.Initialize(m_Device.get(), nullptr))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize SceneRenderer");
            return false;
        }

        // テスト三角形用パイプラインをデフォルトパイプラインとして設定
        m_SceneRenderer.SetDefaultPipeline(m_TrianglePipeline);

        // ========================================
        // 10. FramePacketManagerの初期化
        // ========================================
        m_PacketManager.Initialize();

        // DrawCommand用バッファを確保
        m_FrameDrawCommands.reserve(m_MaxDrawCallsPerFrame);

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

        // GPU処理の完了を待機
        if (m_Device)
        {
            m_Device->WaitIdle();
        }

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

        // テスト三角形リソースの解放
        m_TrianglePipeline.reset();
        m_TriangleFragmentShader.reset();
        m_TriangleVertexShader.reset();

        // フレームバッファ・レンダーパスの解放
        m_SwapChainFramebuffers.clear();
        m_RenderPass.reset();

        // コマンドリストの解放
        m_CommandList.reset();

        // Screenの破棄（SwapChain解放を含む）
        m_Screen.Shutdown();

        // FramePacketManagerの終了
        m_PacketManager.Shutdown();

        // デバイス参照の解放（Engine層がRHIの終了を管理）
        m_Device.reset();

        m_bInitialized = false;
        LOG_INFO("RenderingCoordinator::Shutdown() - Shutdown completed");
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

        // 書き込み用パケットを取得
        m_CurrentPacket = m_PacketManager.AcquireForWrite();
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

        m_Screen.BeginFrame();
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

        m_FrameDrawCommands.clear();

        // 各SceneViewでDrawCommand生成
        // 注: カリングとバッチングはSceneView::Render()内でViewportごとに行われる
        NORVES_STAT_TIME_START(cmdGen);

        for (auto &view : m_Views)
        {
            if (SceneView *sceneView = dynamic_cast<SceneView *>(view.get()))
            {
                // DrawCommandを収集
                const auto &viewCommands = sceneView->GetDrawCommands();
                m_FrameDrawCommands.insert(m_FrameDrawCommands.end(),
                                           viewCommands.begin(), viewCommands.end());
            }
        }

        NORVES_STAT_TIME_END(cmdGen, m_Stats.CommandGenerationTimeMs);

        NORVES_STAT_ADD(m_Stats.DrawCalls, static_cast<uint32_t>(m_FrameDrawCommands.size()));
    }

    void RenderingCoordinator::EndFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 書き込み完了をマーク
        if (m_CurrentPacket)
        {
            m_PacketManager.FinishWrite(m_CurrentPacket);
            m_CurrentPacket = nullptr;
        }

        // Screen経由でコマンドリストをサブミット＆Present
        m_Screen.EndFrame(m_CommandList);
        m_Stats.FrameNumber++;
    }

    void RenderingCoordinator::RenderFrame(FramePacket *packet)
    {
        if (!m_bInitialized)
        {
            return;
        }

        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain)
        {
            return;
        }

        uint32_t imageIndex = swapChain->GetCurrentBackBufferIndex();

        // フレーム別コマンドバッファを選択（ダブルバッファリングでの同期問題を回避）
        m_CommandList->SetFrameIndex(swapChain->GetCurrentFrameIndex());

        // SceneRendererフレーム開始
        m_SceneRenderer.BeginFrame();

        // コマンド録画開始
        m_CommandList->BeginRecording();

        // レンダーパス開始（現在のスワップチェーンフレームバッファを使用）
        m_CommandList->BeginRenderPass(m_RenderPass, m_SwapChainFramebuffers[imageIndex]);

        // ビューポート設定
        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain->GetWidth());
        viewport.height = static_cast<float>(swapChain->GetHeight());
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        m_CommandList->SetViewport(viewport);

        // シザー設定
        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(swapChain->GetWidth());
        scissor.bottom = static_cast<int32_t>(swapChain->GetHeight());
        m_CommandList->SetScissor(scissor);

        // ========================================
        // DrawCommand描画フロー
        // ========================================

        // SceneViewからDrawCommandを収集して描画
        if (m_MainSceneView)
        {
            const auto &drawCommands = m_MainSceneView->GetDrawCommands();
            if (!drawCommands.empty())
            {
                // SceneRenderer経由でDrawCommandを実行
                m_SceneRenderer.ExecuteDrawCommands(drawCommands, m_CommandList.get());
            }
        }

        // フォールバック: DrawCommandがない場合はテスト三角形を直接描画
        if (m_SceneRenderer.GetStats().DrawCallCount == 0)
        {
            m_SceneRenderer.DrawDirect(m_CommandList.get(), m_TrianglePipeline, 3);
        }

        // レンダーパス終了
        m_CommandList->EndRenderPass();

        // コマンド録画終了
        m_CommandList->End();

        // SceneRendererフレーム終了
        m_SceneRenderer.EndFrame();

        // 統計更新
        const auto &rendererStats = m_SceneRenderer.GetStats();
        m_Stats.DrawCalls = rendererStats.DrawCallCount;
        m_Stats.TrianglesRendered = rendererStats.TriangleCount;
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

        m_Width = width;
        m_Height = height;

        // Screenのリサイズ（SwapChainリサイズを含む）
        m_Screen.Resize(width, height);

        // フレームバッファの再作成
        m_SwapChainFramebuffers.clear();
        CreateSwapChainFramebuffers();

        // 各Viewのリサイズ
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Resize(width, height);
            }
        }
    }

    bool RenderingCoordinator::CreateSwapChainFramebuffers()
    {
        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain || !m_RenderPass)
        {
            return false;
        }

        uint32_t imageCount = swapChain->GetBufferCount();
        m_SwapChainFramebuffers.reserve(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            RHI::FramebufferDesc fbDesc;
            fbDesc.colorTargets.push_back(swapChain->GetBackBuffer(i));
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
        }

        LOG_INFO("Created %u swapchain framebuffers", imageCount);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
