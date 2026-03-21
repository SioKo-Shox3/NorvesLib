#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderResourceManager.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Math/VectorUtils.h"
#include "Math/MatrixUtils.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/DeviceCapabilities.h"
#include "Text/IdentityPool.h"
#include "Logging/LogMacros.h"
#include <cstring>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    using namespace Container;

    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    MegaGeometryPass::MegaGeometryPass(const MegaGeometryPassSettings& settings)
        : m_Settings(settings)
    {
    }

    MegaGeometryPass::~MegaGeometryPass()
    {
        if (IsInitialized())
        {
            Shutdown();
        }
    }

    // ========================================
    // Initialize
    // ========================================

    bool MegaGeometryPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        if (!m_Device)
        {
            return false;
        }

        // カリングコンピュートシェーダーの読み込み
        if (context.ShaderMgr)
        {
            m_CullShader = context.ShaderMgr->LoadShader(
                "cluster_cull.comp", RHI::ShaderStage::Compute);
        }
        if (!m_CullShader)
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "カリングシェーダーの読み込みに失敗。パスは無効化されます");
            m_bInitialized = true;
            return true;
        }

        // カリングコンピュートパイプライン作成
        RHI::ComputePipelineDesc cullPipelineDesc;
        cullPipelineDesc.computeShader = m_CullShader;
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // カリング用GPUリソース作成
        if (!CreateCullResources(m_Device))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリング用GPUリソースの作成に失敗");
            return false;
        }

        // GBuffer描画用シェーダーの読み込み（通常のGBufferシェーダーを再利用）
        if (context.ShaderMgr)
        {
            m_DrawVertexShader = context.ShaderMgr->LoadShader(
                "gbuffer.vert", RHI::ShaderStage::Vertex);
            m_DrawFragmentShader = context.ShaderMgr->LoadShader(
                "gbuffer.frag", RHI::ShaderStage::Pixel);
        }
        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer描画シェーダーの読み込みに失敗");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("MegaGeometryPass", "初期化完了 (MaxDrawCount: %u)", m_Settings.MaxDrawCount);
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void MegaGeometryPass::Shutdown()
    {
        m_CullPipeline.reset();
        m_CullShader.reset();
        m_CullDescriptorSet.reset();
        m_CullUniformBuffer.reset();
        m_IndirectDrawBuffer.reset();
        m_DrawCountBuffer.reset();

        m_DrawPipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();
        m_DrawDescriptorSet.reset();
        m_DrawUniformBuffer.reset();

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();

        m_Instances.clear();

        m_bInitialized = false;
    }

    // ========================================
    // Setup
    // ========================================

    void MegaGeometryPass::Setup(ViewRenderContext& context)
    {
        // MegaMeshインスタンスが登録されていなければスキップ
        if (m_Instances.empty() || !m_CullPipeline)
        {
            return;
        }

        // GBufferテクスチャをSharedResourcesから取得
        if (context.SharedResources)
        {
            m_AlbedoTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Albedo"));
            m_NormalTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Normal"));
            m_MaterialTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Material"));
            m_EmissiveTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Emissive"));
            m_DepthTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Depth"));
        }

        // GBufferテクスチャが利用可能か確認
        if (!m_AlbedoTexture || !m_DepthTexture)
        {
            return;
        }

        // 画面サイズ変更に対応
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;

        if (width != m_CurrentWidth || height != m_CurrentHeight || !m_GBufferRenderPass)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // GBuffer互換レンダーパス作成（Load既存内容）
            if (!CreateDrawPipeline(context))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "描画パイプラインの作成に失敗");
                return;
            }
        }
    }

    // ========================================
    // Execute
    // ========================================

    void MegaGeometryPass::Execute(ViewRenderContext& context)
    {
        if (m_Instances.empty() || !m_CullPipeline || !context.CommandList || !context.ResourceManager)
        {
            return;
        }

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !m_DrawPipeline)
        {
            return;
        }

        if (!context.MainCamera)
        {
            return;
        }

        auto* cmdList = context.CommandList;

        // ========================================
        // 各MegaMeshインスタンスに対してカリング + IndirectDraw
        // ========================================
        for (const auto& instance : m_Instances)
        {
            const auto* gpuData = context.ResourceManager->GetMegaMeshGPUData(instance.Handle);
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                continue;
            }

            // ----------------------------------------
            // 1. DrawCountバッファをゼロクリア
            // ----------------------------------------
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                  RHI::ResourceState::Common,
                                  RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_DrawCountBuffer, 0, sizeof(uint32_t), 0);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                  RHI::ResourceState::CopyDest,
                                  RHI::ResourceState::UnorderedAccess);

            // IndirectDrawバッファもUAV状態に
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                  RHI::ResourceState::IndirectArgument,
                                  RHI::ResourceState::UnorderedAccess);

            // ----------------------------------------
            // 2. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData{};

            // CameraProxyからView/Projection行列を構築
            using namespace NorvesLib::Math;

            const auto& cam = *context.MainCamera;
            Vector3 camPos(cam.PositionX, cam.PositionY, cam.PositionZ);
            Vector3 forward(cam.ForwardX, cam.ForwardY, cam.ForwardZ);
            Vector3 lookAt = camPos + forward;
            Vector3 upDir(cam.UpX, cam.UpY, cam.UpZ);

            Matrix4x4 viewMat = MatrixUtils::CreateLookAt(camPos, lookAt, upDir);

            float aspectRatio = static_cast<float>(context.ScreenWidth) / static_cast<float>(context.ScreenHeight);
            float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
            Matrix4x4 projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);
            projMat = context.Device->AdjustProjectionForClipSpace(projMat);

            std::memcpy(uniformData.ViewMatrix, &viewMat, sizeof(float) * 16);
            std::memcpy(uniformData.ProjectionMatrix, &projMat, sizeof(float) * 16);

            uniformData.CameraPosition[0] = cam.PositionX;
            uniformData.CameraPosition[1] = cam.PositionY;
            uniformData.CameraPosition[2] = cam.PositionZ;
            uniformData.CameraPosition[3] = 0.0f;

            // ViewProjection行列を計算して視錐台平面を抽出
            Matrix4x4 vpMat = projMat * viewMat;
            float viewProj[16];
            std::memcpy(viewProj, &vpMat, sizeof(float) * 16);
            ExtractFrustumPlanes(viewProj, uniformData.FrustumPlanes);

            uniformData.TotalClusterCount = gpuData->ClusterCount;
            uniformData.MaxDrawCount = m_Settings.MaxDrawCount;
            uniformData.LODBias = m_Settings.LODBias;
            uniformData.Pad0 = 0;

            m_CullUniformBuffer->Update(&uniformData, sizeof(CullUniformData));

            // ----------------------------------------
            // 3. カリングディスクリプタセット更新
            // ----------------------------------------
            m_CullDescriptorSet->BindConstantBuffer(0, m_CullUniformBuffer, 0,
                                                    static_cast<uint32_t>(sizeof(CullUniformData)));
            m_CullDescriptorSet->BindStorageBuffer(1, gpuData->ClusterBuffer, 0,
                                                   static_cast<uint32_t>(gpuData->ClusterCount * sizeof(MegaGeometry::GPUClusterData)));
            m_CullDescriptorSet->BindStorageBuffer(2, m_IndirectDrawBuffer, 0,
                                                   static_cast<uint32_t>(m_Settings.MaxDrawCount * sizeof(MegaGeometry::DrawIndexedIndirectCommand)));
            m_CullDescriptorSet->BindStorageBuffer(3, m_DrawCountBuffer, 0,
                                                   sizeof(uint32_t));
            m_CullDescriptorSet->Update();

            // ----------------------------------------
            // 4. カリングコンピュートディスパッチ
            // ----------------------------------------
            cmdList->SetPipeline(m_CullPipeline);
            cmdList->SetDescriptorSet(m_CullDescriptorSet, 0);

            uint32_t groupCount = (gpuData->ClusterCount + 63) / 64;
            cmdList->Dispatch(groupCount, 1, 1);

            // ----------------------------------------
            // 5. バリア: Compute UAV → IndirectArgument
            // ----------------------------------------
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                  RHI::ResourceState::UnorderedAccess,
                                  RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                  RHI::ResourceState::UnorderedAccess,
                                  RHI::ResourceState::IndirectArgument);

            // ----------------------------------------
            // 6. GBuffer描画（IndirectDraw）
            // ----------------------------------------

            // PerObject UBO更新（ワールド変換行列）
            struct PerObjectUBO
            {
                float World[16];
                float View[16];
                float Projection[16];
                float CameraPosition[4];
                float ObjectColor[4];
                float EmissiveColor[4];
                float PomParams[4];
            };

            PerObjectUBO perObject{};
            std::memcpy(perObject.World, instance.WorldMatrix, sizeof(float) * 16);
            std::memcpy(perObject.View, &viewMat, sizeof(float) * 16);
            std::memcpy(perObject.Projection, &projMat, sizeof(float) * 16);
            perObject.CameraPosition[0] = cam.PositionX;
            perObject.CameraPosition[1] = cam.PositionY;
            perObject.CameraPosition[2] = cam.PositionZ;
            perObject.CameraPosition[3] = 1.0f;
            perObject.ObjectColor[0] = 1.0f;
            perObject.ObjectColor[1] = 1.0f;
            perObject.ObjectColor[2] = 1.0f;
            perObject.ObjectColor[3] = 1.0f;
            // EmissiveとPomParamsはゼロのまま

            m_DrawUniformBuffer->Update(&perObject, sizeof(PerObjectUBO));

            // GBufferレンダーパス開始
            cmdList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);

            // ビューポート・シザー設定
            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_CurrentWidth);
            viewport.height = static_cast<float>(m_CurrentHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmdList->SetViewport(viewport);

            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32_t>(m_CurrentWidth);
            scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
            cmdList->SetScissor(scissor);

            // パイプラインとデスクリプタ設定
            cmdList->SetPipeline(m_DrawPipeline);

            m_DrawDescriptorSet->BindConstantBuffer(0, m_DrawUniformBuffer, 0,
                                                    static_cast<uint32_t>(sizeof(PerObjectUBO)));
            m_DrawDescriptorSet->Update();
            cmdList->SetDescriptorSet(m_DrawDescriptorSet, 0);

            // 頂点/インデックスバッファ設定
            cmdList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
            cmdList->SetIndexBuffer(gpuData->IndexBuffer, 0);

            // IndirectDraw発行
            // 注意: drawCountはGPU側で計算された値に基づく。
            // 最大値としてMaxDrawCountを渡し、実際の発行数はGPUが決定。
            // ただし、Vulkan の drawIndexedIndirect は固定カウントのため、
            // ここではMaxDrawCountをそのまま使う（無効なエントリはinstanceCount=0で無視される）。
            // TODO: VK_KHR_draw_indirect_count対応でGPU側カウント参照
            cmdList->DrawIndexedIndirect(
                m_IndirectDrawBuffer, 0,
                m_Settings.MaxDrawCount,
                sizeof(MegaGeometry::DrawIndexedIndirectCommand));

            cmdList->EndRenderPass();

            // IndirectDrawバッファを次のフレーム用に戻す
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                  RHI::ResourceState::IndirectArgument,
                                  RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                  RHI::ResourceState::IndirectArgument,
                                  RHI::ResourceState::Common);
        }
    }

    // ========================================
    // MegaMeshインスタンス管理
    // ========================================

    void MegaGeometryPass::AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float* worldMatrix)
    {
        MegaMeshInstance instance;
        instance.Handle = handle;
        if (worldMatrix)
        {
            std::memcpy(instance.WorldMatrix, worldMatrix, sizeof(float) * 16);
        }
        else
        {
            // 単位行列
            std::memset(instance.WorldMatrix, 0, sizeof(float) * 16);
            instance.WorldMatrix[0] = 1.0f;
            instance.WorldMatrix[5] = 1.0f;
            instance.WorldMatrix[10] = 1.0f;
            instance.WorldMatrix[15] = 1.0f;
        }
        m_Instances.push_back(instance);
    }

    void MegaGeometryPass::ClearMegaMeshInstances()
    {
        m_Instances.clear();
    }

    // ========================================
    // カリング用GPUリソース作成
    // ========================================

    bool MegaGeometryPass::CreateCullResources(RHI::IDevice* device)
    {
        // カリングユニフォームバッファ (UBO)
        RHI::BufferDesc uboDesc(
            sizeof(CullUniformData),
            RHI::ResourceUsage::ConstantBuffer,
            true,
            "MegaGeometry_CullUBO");
        m_CullUniformBuffer = device->CreateBuffer(uboDesc);
        if (!m_CullUniformBuffer)
        {
            return false;
        }

        // IndirectDrawコマンドバッファ (SSBO + IndirectBuffer)
        uint64_t indirectSize = static_cast<uint64_t>(m_Settings.MaxDrawCount)
                              * sizeof(MegaGeometry::DrawIndexedIndirectCommand);
        RHI::BufferDesc indirectDesc(
            indirectSize,
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
            false,
            "MegaGeometry_IndirectDraw");
        m_IndirectDrawBuffer = device->CreateBuffer(indirectDesc);
        if (!m_IndirectDrawBuffer)
        {
            return false;
        }

        // DrawCountバッファ（atomic counter用 SSBO）
        RHI::BufferDesc countDesc(
            sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DrawCount");
        m_DrawCountBuffer = device->CreateBuffer(countDesc);
        if (!m_DrawCountBuffer)
        {
            return false;
        }

        // カリング用ディスクリプタセット作成
        RHI::DescriptorSetDesc cullDsDesc;

        // binding 0: CullUniforms (UBO)
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(uboBinding);

        // binding 1: ClusterBuffer (SSBO, readonly)
        RHI::DescriptorBinding clusterBinding;
        clusterBinding.binding = 1;
        clusterBinding.type = RHI::ResourceBindType::StructuredBuffer;
        clusterBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(clusterBinding);

        // binding 2: IndirectDrawBuffer (SSBO, writeonly)
        RHI::DescriptorBinding indirectBinding;
        indirectBinding.binding = 2;
        indirectBinding.type = RHI::ResourceBindType::RWBuffer;
        indirectBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(indirectBinding);

        // binding 3: DrawCountBuffer (SSBO, read-write)
        RHI::DescriptorBinding countBinding;
        countBinding.binding = 3;
        countBinding.type = RHI::ResourceBindType::RWBuffer;
        countBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(countBinding);

        m_CullDescriptorSet = device->CreateDescriptorSet(cullDsDesc);
        if (!m_CullDescriptorSet)
        {
            return false;
        }

        return true;
    }

    // ========================================
    // GBuffer互換グラフィックスパイプライン作成
    // ========================================

    bool MegaGeometryPass::CreateDrawPipeline(ViewRenderContext& context)
    {
        if (!m_Device || !m_DrawVertexShader || !m_DrawFragmentShader)
        {
            return false;
        }

        // GBuffer互換レンダーパス作成（Load既存内容）
        RHI::RenderPassDesc rpDesc;

        // Albedo: Load（GBufferPassで書いた内容を保持）
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = RHI::Format::R8G8B8A8_UNORM;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = false;
        albedoAttach.loadOp = RHI::AttachmentLoadOp::Load;
        albedoAttach.storeOp = RHI::AttachmentStoreOp::Store;
        albedoAttach.initialState = RHI::ResourceState::ShaderResource;
        albedoAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal: Load
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = false;
        normalAttach.loadOp = RHI::AttachmentLoadOp::Load;
        normalAttach.storeOp = RHI::AttachmentStoreOp::Store;
        normalAttach.initialState = RHI::ResourceState::ShaderResource;
        normalAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material: Load
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = RHI::Format::R8G8B8A8_UNORM;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = false;
        materialAttach.loadOp = RHI::AttachmentLoadOp::Load;
        materialAttach.storeOp = RHI::AttachmentStoreOp::Store;
        materialAttach.initialState = RHI::ResourceState::ShaderResource;
        materialAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Emissive: Load
        RHI::AttachmentDesc emissiveAttach;
        emissiveAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        emissiveAttach.isDepthStencil = false;
        emissiveAttach.clear = false;
        emissiveAttach.loadOp = RHI::AttachmentLoadOp::Load;
        emissiveAttach.storeOp = RHI::AttachmentStoreOp::Store;
        emissiveAttach.initialState = RHI::ResourceState::ShaderResource;
        emissiveAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(emissiveAttach);

        // Depth: Load + DepthTest
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = false;
        rpDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        rpDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        rpDesc.depthStencilAttachment.initialState = RHI::ResourceState::ShaderResource;
        rpDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer互換レンダーパスの作成に失敗");
            return false;
        }

        // フレームバッファ作成（GBufferPassと同じテクスチャを参照）
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.colorTargets.push_back(m_EmissiveTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = m_CurrentWidth;
        fbDesc.height = m_CurrentHeight;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "フレームバッファの作成に失敗");
            return false;
        }

        // グラフィックスパイプライン作成
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_DrawVertexShader;
        pipelineDesc.pixelShader = m_DrawFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Mesh3DVertex互換）
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

        // TexCoord: location=2, vec2
        RHI::VertexAttributeDesc texCoordAttr;
        texCoordAttr.location = 2;
        texCoordAttr.binding = 0;
        texCoordAttr.format = RHI::Format::R32G32_FLOAT;
        texCoordAttr.offset = sizeof(float) * 6;
        pipelineDesc.vertexAttributes.push_back(texCoordAttr);

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // MRT用ブレンドステート（4カラーアタッチメント分）
        for (int i = 0; i < 4; ++i)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }

        pipelineDesc.renderPass = m_GBufferRenderPass;

        // ディスクリプタセットレイアウト（GBufferPassと同一: set=0, binding 0=UBO, 1-6=textures）
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(texBinding);
        }

        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_DrawPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_DrawPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "グラフィックスパイプラインの作成に失敗");
            return false;
        }

        // PerObject UBO作成
        constexpr uint32_t PER_OBJECT_UBO_SIZE = 256; // world(64) + view(64) + proj(64) + cam(16) + color(16) + emissive(16) + pom(16)
        RHI::BufferDesc drawUboDesc(
            PER_OBJECT_UBO_SIZE,
            RHI::ResourceUsage::ConstantBuffer,
            true,
            "MegaGeometry_DrawUBO");
        m_DrawUniformBuffer = m_Device->CreateBuffer(drawUboDesc);
        if (!m_DrawUniformBuffer)
        {
            return false;
        }

        // 描画用ディスクリプタセット作成
        m_DrawDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_DrawDescriptorSet)
        {
            return false;
        }

        return true;
    }

    // ========================================
    // 視錐台平面抽出（Gribb/Hartmann法）
    // ========================================

    void MegaGeometryPass::ExtractFrustumPlanes(const float* vp, float planes[6][4])
    {
        // ViewProjection行列（列優先）から6つの視錐台平面を抽出
        // 各平面: ax + by + cz + d = 0 (法線は内側を向く)

        // Left plane
        planes[0][0] = vp[3] + vp[0];
        planes[0][1] = vp[7] + vp[4];
        planes[0][2] = vp[11] + vp[8];
        planes[0][3] = vp[15] + vp[12];

        // Right plane
        planes[1][0] = vp[3] - vp[0];
        planes[1][1] = vp[7] - vp[4];
        planes[1][2] = vp[11] - vp[8];
        planes[1][3] = vp[15] - vp[12];

        // Bottom plane
        planes[2][0] = vp[3] + vp[1];
        planes[2][1] = vp[7] + vp[5];
        planes[2][2] = vp[11] + vp[9];
        planes[2][3] = vp[15] + vp[13];

        // Top plane
        planes[3][0] = vp[3] - vp[1];
        planes[3][1] = vp[7] - vp[5];
        planes[3][2] = vp[11] - vp[9];
        planes[3][3] = vp[15] - vp[13];

        // Near plane
        planes[4][0] = vp[3] + vp[2];
        planes[4][1] = vp[7] + vp[6];
        planes[4][2] = vp[11] + vp[10];
        planes[4][3] = vp[15] + vp[14];

        // Far plane
        planes[5][0] = vp[3] - vp[2];
        planes[5][1] = vp[7] - vp[6];
        planes[5][2] = vp[11] - vp[10];
        planes[5][3] = vp[15] - vp[14];

        // 正規化
        for (int i = 0; i < 6; ++i)
        {
            float len = std::sqrt(planes[i][0] * planes[i][0] +
                                  planes[i][1] * planes[i][1] +
                                  planes[i][2] * planes[i][2]);
            if (len > 1e-8f)
            {
                planes[i][0] /= len;
                planes[i][1] /= len;
                planes[i][2] /= len;
                planes[i][3] /= len;
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
