#include "Rendering/ShadowMapPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/RenderResourceManager.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

#include <cstring>

namespace NorvesLib::Core::Rendering
{

    ShadowMapPass::ShadowMapPass(const ShadowMapPassSettings &settings)
        : m_Settings(settings)
    {
    }

    ShadowMapPass::~ShadowMapPass()
    {
        Shutdown();
    }

    bool ShadowMapPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // シャドウ用シェーダーの作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "ShaderManager is null");
            return false;
        }

        m_ShadowVertexShader = context.ShaderMgr->LoadShader("shadow.vert", RHI::ShaderStage::Vertex);
        if (!m_ShadowVertexShader)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow vertex shader");
            return false;
        }

        m_ShadowFragmentShader = context.ShaderMgr->LoadShader("shadow.frag", RHI::ShaderStage::Pixel);
        if (!m_ShadowFragmentShader)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow fragment shader");
            return false;
        }

        // ========================================
        // シャドウマップ深度テクスチャ作成
        // ========================================
        m_ShadowMapTexture = m_Device->CreateTexture(
            RHI::TextureDesc::DepthStencil(
                m_Settings.Resolution, m_Settings.Resolution,
                m_Settings.DepthFormat, "ShadowMap"));

        if (!m_ShadowMapTexture)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow map texture");
            return false;
        }

        // ========================================
        // 深度オンリーレンダーパス作成
        // ========================================
        RHI::RenderPassDesc rpDesc;
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

        m_ShadowRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_ShadowRenderPass)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow render pass");
            return false;
        }

        // ========================================
        // フレームバッファ作成（深度のみ）
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_ShadowRenderPass;
        fbDesc.depthStencilTarget = m_ShadowMapTexture;
        fbDesc.width = m_Settings.Resolution;
        fbDesc.height = m_Settings.Resolution;

        m_ShadowFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_ShadowFramebuffer)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow framebuffer");
            return false;
        }

        // ========================================
        // DynamicUniformAllocator初期化
        // ========================================
        {
            // UBO: world(64) + lightView(64) + lightProjection(64) = 192 bytes
            constexpr uint32_t UBO_SIZE = 192;
            constexpr uint32_t MAX_OBJECTS = 256;

            RHI::DescriptorSetDesc uboDescSetDesc;
            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex;
            uboDescSetDesc.bindings.push_back(uboBinding);

            if (!m_UniformAllocator.Initialize(m_Device, UBO_SIZE, MAX_OBJECTS, uboDescSetDesc))
            {
                NORVES_LOG_ERROR("ShadowMapPass", "Failed to initialize DynamicUniformAllocator");
                return false;
            }
        }

        // ========================================
        // パイプライン作成（深度オンリー）
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_ShadowVertexShader;
        pipelineDesc.pixelShader = m_ShadowFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（GBufferPassと同じ: Position + Normal）
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

        // Normal: location=1, vec3（頂点レイアウト一致のため含む）
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

        // カラーアタッチメントなし（深度描画のみ）

        pipelineDesc.renderPass = m_ShadowRenderPass;

        // ディスクリプタセットレイアウト（set=0: UBO）
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding dsUboBinding;
        dsUboBinding.binding = 0;
        dsUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        dsUboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(dsUboBinding);
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_ShadowPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_ShadowPipeline)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow pipeline");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("ShadowMapPass", "ShadowMapPass initialized");
        return true;
    }

    void ShadowMapPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_ShadowMapTexture.reset();
        m_ShadowRenderPass.reset();
        m_ShadowFramebuffer.reset();
        m_ShadowPipeline.reset();
        m_ShadowVertexShader.reset();
        m_ShadowFragmentShader.reset();
        m_UniformAllocator.Shutdown();
        m_SceneView = nullptr;
        m_SceneRenderer = nullptr;
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("ShadowMapPass", "ShadowMapPass shutdown");
    }

    void ShadowMapPass::Setup(ViewRenderContext &context)
    {
        // シャドウマップは固定解像度のため、リサイズ処理は不要
    }

    void ShadowMapPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_ShadowRenderPass || !m_ShadowFramebuffer || !m_ShadowPipeline)
        {
            NORVES_LOG_WARNING("ShadowMapPass", "Shadow resources not ready, skipping");
            return;
        }

        // ========================================
        // ライトビュー・プロジェクション行列の構築
        // ========================================
        using namespace NorvesLib::Math;

        // ディレクショナルライト方向（LightingPassと同じ値）
        float lightDirX = -0.577f;
        float lightDirY = -0.577f;
        float lightDirZ = -0.577f;

        // ライト"位置"（方向光なので、シーンを覆う十分な距離に配置）
        float lightDistance = 20.0f;
        Vector3 lightPos(-lightDirX * lightDistance, -lightDirY * lightDistance, -lightDirZ * lightDistance);
        Vector3 lightTarget(0.0f, 0.0f, 0.0f);
        Vector3 upDir(0.0f, 1.0f, 0.0f);

        Matrix4x4 lightViewMat = MatrixUtils::CreateLookAt(lightPos, lightTarget, upDir);

        // 正射影
        float orthoSize = m_Settings.OrthoSize;
        Matrix4x4 lightProjMat = MatrixUtils::CreateOrthographic(
            orthoSize * 2.0f, orthoSize * 2.0f,
            m_Settings.NearPlane, m_Settings.FarPlane);

        // RHI側でAPI固有のクリップ空間補正を適用（シャドウマップではY反転なし）
        lightProjMat = context.Device->AdjustProjectionForClipSpace(lightProjMat, false);

        // ライトビュー・プロジェクションをGPU用データに変換
        float lightViewData[16];
        float lightProjData[16];
        MatrixUtils::TransposeToShaderData(lightViewMat, lightViewData);
        MatrixUtils::TransposeToShaderData(lightProjMat, lightProjData);

        // シャドウマップテクスチャをSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ShadowMap", m_ShadowMapTexture);
        }

        // ========================================
        // シャドウマップレンダーパス開始
        // ========================================
        context.CommandList->BeginRenderPass(m_ShadowRenderPass, m_ShadowFramebuffer);

        // ビューポート・シザー設定
        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_Settings.Resolution);
        viewport.height = static_cast<float>(m_Settings.Resolution);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context.CommandList->SetViewport(viewport);

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_Settings.Resolution);
        scissor.bottom = static_cast<int32_t>(m_Settings.Resolution);
        context.CommandList->SetScissor(scissor);

        // パイプライン設定
        context.CommandList->SetPipeline(m_ShadowPipeline);

        // ========================================
        // DrawCommand駆動の描画（影を落とすメッシュのみ）
        // ========================================
        if (m_SceneView && m_SceneRenderer && context.ResourceManager)
        {
            // UBOデータ構造体（shadow.vertのShadowMVPに対応）
            struct ShadowPerObjectUBO
            {
                float world[16];
                float lightView[16];
                float lightProjection[16];
            };

            // フレーム開始時にアロケータリセット
            m_UniformAllocator.Reset();

            // DrawCommand配列を取得し、影を落とすコマンドのみ描画
            const auto &drawCommands = m_SceneView->GetDrawCommands();
            for (const auto &cmd : drawCommands)
            {
                if (!cmd.bCastShadow)
                {
                    continue;
                }

                // UBOスロット確保
                auto allocation = m_UniformAllocator.Allocate();
                if (!allocation.UniformBuffer)
                {
                    NORVES_LOG_WARNING("ShadowMapPass", "UBO allocation failed, skipping remaining objects");
                    break;
                }

                // UBOデータ構築
                ShadowPerObjectUBO uboData;
                MatrixUtils::CopyToShaderData(cmd.WorldMatrix, uboData.world);
                std::memcpy(uboData.lightView, lightViewData, sizeof(lightViewData));
                std::memcpy(uboData.lightProjection, lightProjData, sizeof(lightProjData));

                // UBO更新
                allocation.UniformBuffer->Update(&uboData, sizeof(ShadowPerObjectUBO));

                // SceneRenderer経由で描画記録
                m_SceneRenderer->RecordMeshDrawCall(cmd, context.CommandList,
                                                    context.ResourceManager, allocation.DescriptorSet, 0);
            }
        }

        // レンダーパス終了
        context.CommandList->EndRenderPass();
    }

} // namespace NorvesLib::Core::Rendering
