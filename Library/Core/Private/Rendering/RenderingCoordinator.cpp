#include "Rendering/RenderingCoordinator.h"
#include "Rendering/Screen.h"
#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FramePacket.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/Shaders/TriangleShaders.h"
#include "Rendering/Shaders/Mesh3DShaders.h"
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
#include "Math/MatrixUtils.h"
#include "Math/Vector3.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <chrono>
#include <algorithm>
#include <cstring>

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

        // ========================================
        // 5. Framebuffers（スワップチェーンイメージごと）
        // ========================================
        if (!CreateSwapChainFramebuffers())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffers");
            return false;
        }

        // ========================================
        // 6. 3Dメッシュシェーダーの作成
        // ========================================
        RHI::ShaderDesc vertexShaderDesc;
        vertexShaderDesc.stage = RHI::ShaderStage::Vertex;
        vertexShaderDesc.entryPoint = "main";
        vertexShaderDesc.byteCode.assign(
            Mesh3DVertexShaderSpirV,
            Mesh3DVertexShaderSpirV + sizeof(Mesh3DVertexShaderSpirV));

        m_Mesh3DVertexShader = m_Device->CreateShader(vertexShaderDesc);
        if (!m_Mesh3DVertexShader)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create 3D mesh vertex shader");
            return false;
        }

        RHI::ShaderDesc fragmentShaderDesc;
        fragmentShaderDesc.stage = RHI::ShaderStage::Pixel;
        fragmentShaderDesc.entryPoint = "main";
        fragmentShaderDesc.byteCode.assign(
            Mesh3DFragmentShaderSpirV,
            Mesh3DFragmentShaderSpirV + sizeof(Mesh3DFragmentShaderSpirV));

        m_Mesh3DFragmentShader = m_Device->CreateShader(fragmentShaderDesc);
        if (!m_Mesh3DFragmentShader)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create 3D mesh fragment shader");
            return false;
        }

        // 旧三角形シェーダーも残す（フォールバック用）
        RHI::ShaderDesc triVertDesc;
        triVertDesc.stage = RHI::ShaderStage::Vertex;
        triVertDesc.entryPoint = "main";
        triVertDesc.byteCode.assign(
            TriangleVertexShaderSpirV,
            TriangleVertexShaderSpirV + sizeof(TriangleVertexShaderSpirV));
        m_TriangleVertexShader = m_Device->CreateShader(triVertDesc);

        RHI::ShaderDesc triFragDesc;
        triFragDesc.stage = RHI::ShaderStage::Pixel;
        triFragDesc.entryPoint = "main";
        triFragDesc.byteCode.assign(
            TriangleFragmentShaderSpirV,
            TriangleFragmentShaderSpirV + sizeof(TriangleFragmentShaderSpirV));
        m_TriangleFragmentShader = m_Device->CreateShader(triFragDesc);

        // ========================================
        // 7. MVPユニフォームバッファ作成
        // ========================================
        // UBOレイアウト: mat4 world(64), mat4 view(64), mat4 projection(64), vec4 cameraPosition(16) = 208 bytes
        // std140レイアウトに合わせて256バイト確保
        RHI::BufferDesc uboDesc(256, RHI::ResourceUsage::ConstantBuffer, true, "MVPUniformBuffer");
        m_MVPUniformBuffer = m_Device->CreateBuffer(uboDesc);
        if (!m_MVPUniformBuffer)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create MVP uniform buffer");
            return false;
        }

        // ========================================
        // 8. ディスクリプタセット作成（UBOバインディング）
        // ========================================
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(uboBinding);

        m_MVPDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_MVPDescriptorSet)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create MVP descriptor set");
            return false;
        }

        // UBOをディスクリプタセットにバインド
        m_MVPDescriptorSet->BindConstantBuffer(0, m_MVPUniformBuffer, 0, 224);
        m_MVPDescriptorSet->Update();

        // ========================================
        // 9. 3Dグラフィックスパイプライン作成
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_Mesh3DVertexShader;
        pipelineDesc.pixelShader = m_Mesh3DFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Position + Normal）
        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        // Position: location=0, vec3
        RHI::VertexAttributeDesc posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::Format::R32G32B32_FLOAT;
        posAttr.offset = 0;
        pipelineDesc.vertexAttributes.push_back(posAttr);

        // Normal: location=1, vec3
        RHI::VertexAttributeDesc normalAttr;
        normalAttr.location = 1;
        normalAttr.binding = 0;
        normalAttr.format = RHI::Format::R32G32B32_FLOAT;
        normalAttr.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttr);

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // ブレンド
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_RenderPass;

        // ディスクリプタセットレイアウト（set=0: UBO）
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_Mesh3DPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Mesh3DPipeline)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create 3D mesh pipeline");
            return false;
        }

        // 旧三角形パイプラインも残す
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
        }

        // ========================================
        // 10. 球体メッシュの生成とGPUバッファ作成
        // ========================================
        {
            Container::VariableArray<Mesh3DVertex> vertices;
            Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GenerateUVSphere(1.0f, 32, 16, vertices, indices);
            m_SphereIndexCount = static_cast<uint32_t>(indices.size());

            // 頂点バッファ
            uint64_t vbSize = vertices.size() * sizeof(Mesh3DVertex);
            RHI::BufferDesc vbDesc(vbSize, RHI::ResourceUsage::VertexBuffer, true, "SphereVertexBuffer");
            m_SphereVertexBuffer = m_Device->CreateBuffer(vbDesc);
            if (!m_SphereVertexBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create sphere vertex buffer");
                return false;
            }
            m_SphereVertexBuffer->Update(vertices.data(), vbSize);

            // インデックスバッファ
            uint64_t ibSize = indices.size() * sizeof(uint32_t);
            RHI::BufferDesc ibDesc(ibSize, RHI::ResourceUsage::IndexBuffer, true, "SphereIndexBuffer");
            m_SphereIndexBuffer = m_Device->CreateBuffer(ibDesc);
            if (!m_SphereIndexBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create sphere index buffer");
                return false;
            }
            m_SphereIndexBuffer->Update(indices.data(), ibSize);

            LOG_INFO("Sphere mesh created: %u vertices, %u indices",
                     static_cast<uint32_t>(vertices.size()), m_SphereIndexCount);
        }

        // ========================================
        // 10b. 地面メッシュの生成とGPUバッファ作成
        // ========================================
        {
            Container::VariableArray<Mesh3DVertex> vertices;
            Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(10.0f, 10.0f, 10, 10, vertices, indices);
            m_GroundIndexCount = static_cast<uint32_t>(indices.size());

            // 頂点バッファ
            uint64_t vbSize = vertices.size() * sizeof(Mesh3DVertex);
            RHI::BufferDesc vbDesc(vbSize, RHI::ResourceUsage::VertexBuffer, true, "GroundVertexBuffer");
            m_GroundVertexBuffer = m_Device->CreateBuffer(vbDesc);
            if (!m_GroundVertexBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create ground vertex buffer");
                return false;
            }
            m_GroundVertexBuffer->Update(vertices.data(), vbSize);

            // インデックスバッファ
            uint64_t ibSize = indices.size() * sizeof(uint32_t);
            RHI::BufferDesc ibDesc(ibSize, RHI::ResourceUsage::IndexBuffer, true, "GroundIndexBuffer");
            m_GroundIndexBuffer = m_Device->CreateBuffer(ibDesc);
            if (!m_GroundIndexBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create ground index buffer");
                return false;
            }
            m_GroundIndexBuffer->Update(indices.data(), ibSize);

            LOG_INFO("Ground plane mesh created: %u vertices, %u indices",
                     static_cast<uint32_t>(vertices.size()), m_GroundIndexCount);

            // 地面用の専用UBOとディスクリプタセット作成
            RHI::BufferDesc groundUboDesc(256, RHI::ResourceUsage::ConstantBuffer, true, "GroundMVPUniformBuffer");
            m_GroundMVPUniformBuffer = m_Device->CreateBuffer(groundUboDesc);
            if (!m_GroundMVPUniformBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create ground MVP uniform buffer");
                return false;
            }

            m_GroundMVPDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
            if (!m_GroundMVPDescriptorSet)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create ground MVP descriptor set");
                return false;
            }
            m_GroundMVPDescriptorSet->BindConstantBuffer(0, m_GroundMVPUniformBuffer, 0, 224);
            m_GroundMVPDescriptorSet->Update();
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

        m_Screen.AddView(m_MainSceneView, 0);
        m_Views.push_back(m_MainSceneView);

        // ========================================
        // 12. SceneRendererの初期化
        // ========================================
        if (!m_SceneRenderer.Initialize(m_Device.get(), nullptr))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize SceneRenderer");
            return false;
        }

        // デフォルトパイプラインは設定しない（球体描画で代替）
        // m_SceneRenderer.SetDefaultPipeline(m_TrianglePipeline);

        // ========================================
        // 13. MeshProxyはWorldから自動登録される
        // ========================================

        // ========================================
        // 14. FramePacketManagerの初期化
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

        // 3Dメッシュリソースの解放
        m_Mesh3DPipeline.reset();
        m_MVPDescriptorSet.reset();
        m_MVPUniformBuffer.reset();
        m_SphereIndexBuffer.reset();
        m_SphereVertexBuffer.reset();
        m_GroundIndexBuffer.reset();
        m_GroundVertexBuffer.reset();
        m_GroundMVPDescriptorSet.reset();
        m_GroundMVPUniformBuffer.reset();
        m_Mesh3DFragmentShader.reset();
        m_Mesh3DVertexShader.reset();
        m_DepthTexture.reset();

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
        // SceneView → MeshBatcher → DrawCommand → SceneRenderer → RHI
        // ========================================

        // SceneViewのDrawCommandパイプラインを実行
        if (m_MainSceneView)
        {
            // Cull → Batch → GenerateCommands（Viewport不要のフォールバックパス）
            m_MainSceneView->PrepareDrawCommands();

            const auto &drawCommands = m_MainSceneView->GetDrawCommands();
            if (!drawCommands.empty())
            {
                // SceneRenderer経由でDrawCommandを実行
                m_SceneRenderer.ExecuteDrawCommands(drawCommands, m_CommandList.get());
            }
        }

        // 3D球体を描画
        if (m_Mesh3DPipeline && m_SphereVertexBuffer)
        {
            // カメラ行列の計算（回転する球体）
            using namespace NorvesLib::Math;

            float time = static_cast<float>(m_TotalTime);

            // カメラ位置と注視点
            Vector3 cameraPos;
            Vector3 lookAt;
            Vector3 upDir;

            if (m_bCameraSet)
            {
                // MayaCameraController等から設定されたカメラを使用
                cameraPos = Vector3(m_MainCamera.PositionX, m_MainCamera.PositionY, m_MainCamera.PositionZ);
                Vector3 forward(m_MainCamera.ForwardX, m_MainCamera.ForwardY, m_MainCamera.ForwardZ);
                lookAt = cameraPos + forward;
                upDir = Vector3(m_MainCamera.UpX, m_MainCamera.UpY, m_MainCamera.UpZ);
            }
            else
            {
                // デフォルトカメラ（フォールバック）
                cameraPos = Vector3(0.0f, 1.5f, 4.0f);
                lookAt = Vector3(0.0f, 0.0f, 0.0f);
                upDir = Vector3(0.0f, 1.0f, 0.0f);
            }

            // ワールド行列（Y軸回転）
            float angle = time * 0.5f;
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // 行列はRowMajorレイアウト
            // Vulkan/GLSLのstd140はcolumn-majorを期待するため転置して渡す
            Matrix4x4 worldMat = Matrix4x4::Identity;
            worldMat.m00 = cosA;
            worldMat.m01 = 0.0f;
            worldMat.m02 = sinA;
            worldMat.m03 = 0.0f;
            worldMat.m10 = 0.0f;
            worldMat.m11 = 1.0f;
            worldMat.m12 = 0.0f;
            worldMat.m13 = 0.0f;
            worldMat.m20 = -sinA;
            worldMat.m21 = 0.0f;
            worldMat.m22 = cosA;
            worldMat.m23 = 0.0f;
            worldMat.m30 = 0.0f;
            worldMat.m31 = 0.0f;
            worldMat.m32 = 0.0f;
            worldMat.m33 = 1.0f;

            // ビュー行列
            Matrix4x4 viewMat = MatrixUtils::CreateLookAt(cameraPos, lookAt, upDir);

            // プロジェクション行列
            float aspectRatio = static_cast<float>(swapChain->GetWidth()) / static_cast<float>(swapChain->GetHeight());
            float fovRadians = m_bCameraSet
                                   ? m_MainCamera.FieldOfView * (3.14159265f / 180.0f)
                                   : 0.7853981f; // 45 degrees fallback
            float nearPlane = m_bCameraSet ? m_MainCamera.NearPlane : 0.1f;
            float farPlane = m_bCameraSet ? m_MainCamera.FarPlane : 100.0f;
            Matrix4x4 projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians,
                aspectRatio,
                nearPlane,
                farPlane);

            // CreatePerspectiveFieldOfViewは左手系(m[3][2]=+1)で作成される。
            // しかしCreateLookAtは右手系(オブジェクトはビュー空間でz<0)のため、
            // 投影行列を右手系に変換する必要がある。
            projMat.m22 *= -1.0f; // f/(f-n) → -f/(f-n)
            projMat.m32 *= -1.0f; // +1 → -1

            // Vulkanのクリップ空間はY反転（OpenGLとは異なる）
            projMat.m11 *= -1.0f;

            // UBOデータ構造体（std140レイアウトに合わせる）
            // RowMajor行列をcolumn-majorとして転置して渡す
            struct MVPData
            {
                float world[16];
                float view[16];
                float projection[16];
                float cameraPosition[4];
                float objectColor[4];
            };

            MVPData mvpData;

            // 転置してコピー（RowMajor → ColumnMajor）
            auto TransposeToFloat = [](const Matrix4x4 &mat, float *out)
            {
                for (int row = 0; row < 4; ++row)
                {
                    for (int col = 0; col < 4; ++col)
                    {
                        out[col * 4 + row] = mat.m[row][col];
                    }
                }
            };

            TransposeToFloat(worldMat, mvpData.world);
            TransposeToFloat(viewMat, mvpData.view);
            TransposeToFloat(projMat, mvpData.projection);

            mvpData.cameraPosition[0] = cameraPos.x;
            mvpData.cameraPosition[1] = cameraPos.y;
            mvpData.cameraPosition[2] = cameraPos.z;
            mvpData.cameraPosition[3] = 1.0f;

            // 球体の色（赤系）
            mvpData.objectColor[0] = 0.8f;
            mvpData.objectColor[1] = 0.2f;
            mvpData.objectColor[2] = 0.2f;
            mvpData.objectColor[3] = 1.0f;

            // UBO更新
            m_MVPUniformBuffer->Update(&mvpData, sizeof(MVPData));

            // 3D球体描画
            m_CommandList->SetPipeline(m_Mesh3DPipeline);
            m_CommandList->SetDescriptorSet(m_MVPDescriptorSet, 0);
            m_CommandList->SetVertexBuffer(m_SphereVertexBuffer, 0, 0);
            m_CommandList->SetIndexBuffer(m_SphereIndexBuffer, 0);
            m_CommandList->DrawIndexed(m_SphereIndexCount, 0, 0);

            // 地面描画（専用UBOを使用）
            if (m_GroundVertexBuffer && m_GroundMVPUniformBuffer)
            {
                // 地面は球体の下（Y = -1.0）に配置、回転なし
                Matrix4x4 groundWorld = Matrix4x4::Identity;
                groundWorld.m13 = -1.0f; // Y方向に-1.0オフセット（RowMajor: m[1][3] = ty）

                MVPData groundMvpData;
                TransposeToFloat(groundWorld, groundMvpData.world);
                TransposeToFloat(viewMat, groundMvpData.view);
                TransposeToFloat(projMat, groundMvpData.projection);
                groundMvpData.cameraPosition[0] = cameraPos.x;
                groundMvpData.cameraPosition[1] = cameraPos.y;
                groundMvpData.cameraPosition[2] = cameraPos.z;
                groundMvpData.cameraPosition[3] = 1.0f;

                // 地面の色（暗い緑灰色）
                groundMvpData.objectColor[0] = 0.35f;
                groundMvpData.objectColor[1] = 0.45f;
                groundMvpData.objectColor[2] = 0.3f;
                groundMvpData.objectColor[3] = 1.0f;

                m_GroundMVPUniformBuffer->Update(&groundMvpData, sizeof(MVPData));

                m_CommandList->SetPipeline(m_Mesh3DPipeline);
                m_CommandList->SetDescriptorSet(m_GroundMVPDescriptorSet, 0);
                m_CommandList->SetVertexBuffer(m_GroundVertexBuffer, 0, 0);
                m_CommandList->SetIndexBuffer(m_GroundIndexBuffer, 0);
                m_CommandList->DrawIndexed(m_GroundIndexCount, 0, 0);
            }
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

    void RenderingCoordinator::SetMainCamera(const CameraProxy &camera)
    {
        m_MainCamera = camera;
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

        m_Width = width;
        m_Height = height;

        // Screenのリサイズ（SwapChainリサイズを含む）
        m_Screen.Resize(width, height);

        // フレームバッファの再作成（デプスバッファも含む）
        m_SwapChainFramebuffers.clear();
        m_DepthTexture.reset();
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
        }

        LOG_INFO("Created %u swapchain framebuffers with depth buffer", imageCount);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
