#include "Rendering/GBufferPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/RenderResourceManager.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

// GBuffer用SPIR-Vシェーダー
#include "Shaders/GBufferShaders.h"

namespace NorvesLib::Core::Rendering
{

    GBufferPass::GBufferPass(const GBufferPassSettings &settings)
        : m_Settings(settings)
    {
    }

    GBufferPass::~GBufferPass()
    {
        Shutdown();
    }

    bool GBufferPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("GBufferPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // GBuffer用シェーダーの作成
        // ========================================
        RHI::ShaderDesc vertexShaderDesc;
        vertexShaderDesc.stage = RHI::ShaderStage::Vertex;
        vertexShaderDesc.entryPoint = "main";
        vertexShaderDesc.byteCode.assign(
            GBufferVertexShaderSpirV,
            GBufferVertexShaderSpirV + sizeof(GBufferVertexShaderSpirV));

        m_GBufferVertexShader = m_Device->CreateShader(vertexShaderDesc);
        if (!m_GBufferVertexShader)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer vertex shader");
            return false;
        }

        RHI::ShaderDesc fragmentShaderDesc;
        fragmentShaderDesc.stage = RHI::ShaderStage::Pixel;
        fragmentShaderDesc.entryPoint = "main";
        fragmentShaderDesc.byteCode.assign(
            GBufferFragmentShaderSpirV,
            GBufferFragmentShaderSpirV + sizeof(GBufferFragmentShaderSpirV));

        m_GBufferFragmentShader = m_Device->CreateShader(fragmentShaderDesc);
        if (!m_GBufferFragmentShader)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer fragment shader");
            return false;
        }

        m_bInitialized = true;

        // ========================================
        // DynamicUniformAllocator初期化
        // ========================================
        {
            // UBOレイアウト: world(64) + view(64) + projection(64) + cameraPos(16) + objectColor(16) = 224 bytes
            constexpr uint32_t UBO_SIZE = 224;
            constexpr uint32_t MAX_OBJECTS = 256; // 1フレームあたりの最大オブジェクト数

            RHI::DescriptorSetDesc uboDescSetDesc;
            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex;
            uboDescSetDesc.bindings.push_back(uboBinding);

            if (!m_UniformAllocator.Initialize(m_Device, UBO_SIZE, MAX_OBJECTS, uboDescSetDesc))
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to initialize DynamicUniformAllocator");
                return false;
            }
        }

        NORVES_LOG_INFO("GBufferPass", "GBufferPass initialized");
        return true;
    }

    void GBufferPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_DepthTexture.reset();
        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_GBufferPipeline.reset();
        m_GBufferVertexShader.reset();
        m_GBufferFragmentShader.reset();
        m_UniformAllocator.Shutdown();
        m_SceneView = nullptr;
        m_SceneRenderer = nullptr;
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("GBufferPass", "GBufferPass shutdown");
    }

    void GBufferPass::Setup(ViewRenderContext &context)
    {
        // GBufferサイズを決定
        uint32_t width = m_Settings.Width > 0 ? m_Settings.Width : context.ScreenWidth;
        uint32_t height = m_Settings.Height > 0 ? m_Settings.Height : context.ScreenHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        // サイズ変更があればGBufferリソースを再作成
        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            CreateGBufferResources(width, height, context);
        }
    }

    void GBufferPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !m_GBufferPipeline)
        {
            NORVES_LOG_WARNING("GBufferPass", "GBuffer resources not ready, skipping");
            return;
        }

        // GBufferテクスチャをSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("GBuffer_Albedo", m_AlbedoTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Normal", m_NormalTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Material", m_MaterialTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Depth", m_DepthTexture);
        }

        // GBufferレンダーパス開始
        context.CommandList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);

        // ビューポート・シザー設定
        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_CurrentWidth);
        viewport.height = static_cast<float>(m_CurrentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context.CommandList->SetViewport(viewport);

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_CurrentWidth);
        scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
        context.CommandList->SetScissor(scissor);

        // GBufferパイプラインを設定
        context.CommandList->SetPipeline(m_GBufferPipeline);

        // ========================================
        // MeshProxy駆動の描画
        // ========================================
        if (m_SceneView && context.ResourceManager)
        {
            // カメラ行列の構築
            using namespace NorvesLib::Math;

            Matrix4x4 viewMat = Matrix4x4::Identity;
            Matrix4x4 projMat = Matrix4x4::Identity;
            float cameraPos[4] = {0.0f, 1.5f, 4.0f, 1.0f};

            if (context.MainCamera)
            {
                const auto& cam = *context.MainCamera;
                Vector3 camPos(cam.PositionX, cam.PositionY, cam.PositionZ);
                Vector3 forward(cam.ForwardX, cam.ForwardY, cam.ForwardZ);
                Vector3 lookAt = camPos + forward;
                Vector3 upDir(cam.UpX, cam.UpY, cam.UpZ);

                viewMat = MatrixUtils::CreateLookAt(camPos, lookAt, upDir);

                float aspectRatio = static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight);
                float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
                projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                    fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);

                // 右手系変換
                projMat.m22 *= -1.0f;
                projMat.m32 *= -1.0f;
                // Vulkan Y反転
                projMat.m11 *= -1.0f;

                cameraPos[0] = cam.PositionX;
                cameraPos[1] = cam.PositionY;
                cameraPos[2] = cam.PositionZ;
                cameraPos[3] = 1.0f;
            }

            // RowMajor → ColumnMajor転置ヘルパー
            auto TransposeToFloat = [](const Matrix4x4& mat, float* out)
            {
                for (int row = 0; row < 4; ++row)
                {
                    for (int col = 0; col < 4; ++col)
                    {
                        out[col * 4 + row] = mat.m[row][col];
                    }
                }
            };

            // UBOデータ構造体（std140レイアウト）
            struct PerObjectUBO
            {
                float world[16];
                float view[16];
                float projection[16];
                float cameraPosition[4];
                float objectColor[4];
            };

            // ビュー・プロジェクション行列を事前変換
            float viewData[16];
            float projData[16];
            TransposeToFloat(viewMat, viewData);
            TransposeToFloat(projMat, projData);

            // フレーム開始時にアロケータリセット
            m_UniformAllocator.Reset();

            const auto& meshProxies = m_SceneView->GetMeshProxies();
            for (const auto& proxy : meshProxies)
            {
                if (!proxy.IsValid())
                {
                    continue;
                }

                // RenderResourceManagerからGPUデータを取得
                const auto* gpuData = context.ResourceManager->GetMeshGPUData(proxy.MeshHandle);
                if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer)
                {
                    continue;
                }

                // UBOスロット確保
                auto allocation = m_UniformAllocator.Allocate();
                if (!allocation.UniformBuffer)
                {
                    NORVES_LOG_WARNING("GBufferPass", "UBO allocation failed, skipping remaining objects");
                    break;
                }

                // UBOデータ構築
                PerObjectUBO uboData;
                TransposeToFloat(proxy.WorldTransform, uboData.world);
                std::memcpy(uboData.view, viewData, sizeof(viewData));
                std::memcpy(uboData.projection, projData, sizeof(projData));
                std::memcpy(uboData.cameraPosition, cameraPos, sizeof(cameraPos));

                // オブジェクトカラー（CustomDataから取得、未設定時はデフォルト白）
                uboData.objectColor[0] = proxy.CustomData[0] != 0.0f ? proxy.CustomData[0] : 1.0f;
                uboData.objectColor[1] = proxy.CustomData[1] != 0.0f ? proxy.CustomData[1] : 1.0f;
                uboData.objectColor[2] = proxy.CustomData[2] != 0.0f ? proxy.CustomData[2] : 1.0f;
                uboData.objectColor[3] = proxy.CustomData[3] != 0.0f ? proxy.CustomData[3] : 1.0f;

                // UBO更新
                allocation.UniformBuffer->Update(&uboData, sizeof(PerObjectUBO));

                // 描画
                context.CommandList->SetDescriptorSet(allocation.DescriptorSet, 0);
                context.CommandList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
                context.CommandList->SetIndexBuffer(gpuData->IndexBuffer, 0);
                context.CommandList->DrawIndexed(gpuData->IndexCount, 0, 0);
            }
        }

        // レンダーパス終了
        context.CommandList->EndRenderPass();
    }

    bool GBufferPass::CreateGBufferResources(uint32_t width, uint32_t height, ViewRenderContext &context)
    {
        if (!m_Device)
        {
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;

        // ========================================
        // GBufferテクスチャ作成
        // ========================================
        m_AlbedoTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.AlbedoFormat, "GBuffer_Albedo"));
        m_NormalTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.NormalFormat, "GBuffer_Normal"));
        m_MaterialTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.MaterialFormat, "GBuffer_Material"));
        m_DepthTexture = m_Device->CreateTexture(
            RHI::TextureDesc::DepthStencil(width, height, m_Settings.DepthFormat, "GBuffer_Depth"));

        if (!m_AlbedoTexture || !m_NormalTexture || !m_MaterialTexture || !m_DepthTexture)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer textures");
            return false;
        }

        // ========================================
        // MRT対応レンダーパス作成（3カラー + 1デプス）
        // ========================================
        RHI::RenderPassDesc rpDesc;

        // Albedo アタッチメント
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = m_Settings.AlbedoFormat;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = true;
        albedoAttach.clearColor[0] = 0.0f;
        albedoAttach.clearColor[1] = 0.0f;
        albedoAttach.clearColor[2] = 0.0f;
        albedoAttach.clearColor[3] = 0.0f;
        albedoAttach.loadOp = RHI::AttachmentLoadOp::Clear;
        albedoAttach.storeOp = RHI::AttachmentStoreOp::Store;
        albedoAttach.initialState = RHI::ResourceState::Undefined;
        albedoAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal アタッチメント
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = m_Settings.NormalFormat;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = true;
        normalAttach.clearColor[0] = 0.0f;
        normalAttach.clearColor[1] = 0.0f;
        normalAttach.clearColor[2] = 0.0f;
        normalAttach.clearColor[3] = 0.0f;
        normalAttach.loadOp = RHI::AttachmentLoadOp::Clear;
        normalAttach.storeOp = RHI::AttachmentStoreOp::Store;
        normalAttach.initialState = RHI::ResourceState::Undefined;
        normalAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material アタッチメント
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = m_Settings.MaterialFormat;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = true;
        materialAttach.clearColor[0] = 0.0f;
        materialAttach.clearColor[1] = 0.0f;
        materialAttach.clearColor[2] = 0.0f;
        materialAttach.clearColor[3] = 0.0f;
        materialAttach.loadOp = RHI::AttachmentLoadOp::Clear;
        materialAttach.storeOp = RHI::AttachmentStoreOp::Store;
        materialAttach.initialState = RHI::ResourceState::Undefined;
        materialAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Depth アタッチメント
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = m_Settings.DepthFormat;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = true;
        rpDesc.depthStencilAttachment.clearDepth = 1.0f;
        rpDesc.depthStencilAttachment.clearStencil = 0;
        rpDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        rpDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        rpDesc.depthStencilAttachment.initialState = RHI::ResourceState::Undefined;
        rpDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer render pass");
            return false;
        }

        // ========================================
        // フレームバッファ作成
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = width;
        fbDesc.height = height;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer framebuffer");
            return false;
        }

        // ========================================
        // GBuffer用パイプライン作成（初回のみ、またはレンダーパス変更時）
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_GBufferVertexShader;
        pipelineDesc.pixelShader = m_GBufferFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Position + Normal = Mesh3DVertexと同じ）
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
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // MRT用ブレンドステート（3カラーアタッチメント分）
        for (int i = 0; i < 3; ++i)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }

        pipelineDesc.renderPass = m_GBufferRenderPass;

        // ディスクリプタセットレイアウト（set=0: MVP UBO）
        // GBufferシェーダーはmesh3dと同じUBOレイアウトを使用
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(uboBinding);
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_GBufferPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_GBufferPipeline)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer pipeline");
            return false;
        }

        NORVES_LOG_INFO("GBufferPass", "GBuffer resources created (%ux%u)", width, height);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
