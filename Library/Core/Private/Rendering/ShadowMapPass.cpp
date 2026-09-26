#include "Rendering/ShadowMapPass.h"
#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/CascadedShadowLightMatrices.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
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

        const auto failInitialize = [this]()
        {
            Shutdown();
            return false;
        };

        if (!context.Device)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Device is null");
            return failInitialize();
        }

        m_Device = context.Device;

        // ========================================
        // シャドウ用シェーダーの作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "ShaderManager is null");
            return failInitialize();
        }

        m_ShadowVertexShader = context.ShaderMgr->LoadShader("shadow.vert", RHI::ShaderStage::Vertex);
        if (!m_ShadowVertexShader)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow vertex shader");
            return failInitialize();
        }

        m_SkinnedShadowVertexShader =
            context.ShaderMgr->LoadShader("skinned_shadow.vert", RHI::ShaderStage::Vertex);
        if (!m_SkinnedShadowVertexShader)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create skinned shadow vertex shader");
            return failInitialize();
        }
        m_ShadowFragmentShader = context.ShaderMgr->LoadShader("shadow.frag", RHI::ShaderStage::Pixel);
        if (!m_ShadowFragmentShader)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow fragment shader");
            return failInitialize();
        }

        // ========================================
        // シャドウマップ深度テクスチャ作成
        // ========================================
        RHI::TextureDesc shadowMapDesc = RHI::TextureDesc::DepthStencil(
            m_Settings.Resolution, m_Settings.Resolution,
            m_Settings.DepthFormat, "ShadowMap");
        shadowMapDesc.ArraySize = CSM_CASCADE_COUNT;
        shadowMapDesc.Dimension = RHI::TextureDimension::Texture2D;
        m_ShadowMapTexture = m_Device->CreateTexture(shadowMapDesc);

        if (!m_ShadowMapTexture)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow map texture");
            return failInitialize();
        }

        RHI::SamplerDesc shadowSamplerDesc;
        shadowSamplerDesc.filterMin = RHI::FilterMode::Linear;
        shadowSamplerDesc.filterMag = RHI::FilterMode::Linear;
        shadowSamplerDesc.filterMip = RHI::FilterMode::Point;
        shadowSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        shadowSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        shadowSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_ShadowSampler = m_Device->CreateSampler(shadowSamplerDesc);
        if (!m_ShadowSampler)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow sampler");
            return failInitialize();
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
            return failInitialize();
        }

        // ========================================
        // フレームバッファ作成（深度のみ）
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_ShadowRenderPass;
        fbDesc.depthStencilTarget = m_ShadowMapTexture;
        fbDesc.width = m_Settings.Resolution;
        fbDesc.height = m_Settings.Resolution;

        m_ShadowFramebuffers.clear();
        m_ShadowFramebuffers.reserve(CSM_CASCADE_COUNT);
        for (uint32_t cascadeIndex = 0; cascadeIndex < CSM_CASCADE_COUNT; ++cascadeIndex)
        {
            fbDesc.depthStencilArrayLayer = cascadeIndex;
            RHI::FramebufferPtr framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!framebuffer)
            {
                NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow framebuffer for cascade %u", cascadeIndex);
                return failInitialize();
            }
            m_ShadowFramebuffers.push_back(framebuffer);
        }

        // ========================================
        // DynamicUniformAllocator初期化
        // ========================================
        {
            // UBO: lightView(64) + lightProjection(64) = 128 bytes
            constexpr uint32_t UBO_SIZE = 128;
            constexpr uint32_t MAX_OBJECTS = 256 * CSM_CASCADE_COUNT;

            RHI::DescriptorSetDesc uboDescSetDesc;
            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex;
            uboDescSetDesc.bindings.push_back(uboBinding);

            RHI::DescriptorBinding instanceBinding;
            instanceBinding.binding = 7;
            instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
            instanceBinding.stages = RHI::ShaderStage::Vertex;
            uboDescSetDesc.bindings.push_back(instanceBinding);

            for (uint32_t bindingIndex = 8; bindingIndex <= 9; ++bindingIndex)
            {
                RHI::DescriptorBinding storageBinding;
                storageBinding.binding = bindingIndex;
                storageBinding.type = RHI::ResourceBindType::StructuredBuffer;
                storageBinding.stages = RHI::ShaderStage::Vertex;
                uboDescSetDesc.bindings.push_back(storageBinding);
            }
            if (!m_UniformAllocator.Initialize(m_Device, UBO_SIZE, MAX_OBJECTS, uboDescSetDesc))
            {
                NORVES_LOG_ERROR("ShadowMapPass", "Failed to initialize DynamicUniformAllocator");
                return failInitialize();
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
        // Shadow casters are allowed to face away from the light; the fixture and
        // two-sided materials must still contribute to the depth map.
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
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

        RHI::DescriptorBinding instanceBinding;
        instanceBinding.binding = 7;
        instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
        instanceBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(instanceBinding);

        for (uint32_t bindingIndex = 8; bindingIndex <= 9; ++bindingIndex)
        {
            RHI::DescriptorBinding storageBinding;
            storageBinding.binding = bindingIndex;
            storageBinding.type = RHI::ResourceBindType::StructuredBuffer;
            storageBinding.stages = RHI::ShaderStage::Vertex;
            dsDesc.bindings.push_back(storageBinding);
        }
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_ShadowPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_ShadowPipeline)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create shadow pipeline");
            return failInitialize();
        }

        RHI::GraphicsPipelineDesc skinnedPipelineDesc = pipelineDesc;
        skinnedPipelineDesc.vertexShader = m_SkinnedShadowVertexShader;
        skinnedPipelineDesc.vertexBindings.clear();
        skinnedPipelineDesc.vertexAttributes.clear();

        RHI::VertexBindingDesc skinnedVertexBinding;
        skinnedVertexBinding.binding = 0;
        skinnedVertexBinding.stride = sizeof(SkinnedMeshVertex);
        skinnedVertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        skinnedPipelineDesc.vertexBindings.push_back(skinnedVertexBinding);

        RHI::VertexAttributeDesc skinnedPosition;
        skinnedPosition.location = 0;
        skinnedPosition.binding = 0;
        skinnedPosition.format = RHI::Format::R32G32B32_FLOAT;
        skinnedPosition.offset = 0;
        skinnedPipelineDesc.vertexAttributes.push_back(skinnedPosition);

        m_SkinnedShadowPipeline = m_Device->CreateGraphicsPipeline(skinnedPipelineDesc);
        if (!m_SkinnedShadowPipeline)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "Failed to create skinned shadow pipeline");
            return failInitialize();
        }
        m_bInitialized = true;
        NORVES_LOG_INFO("ShadowMapPass", "ShadowMapPass initialized");
        return true;
    }

    void ShadowMapPass::Shutdown()
    {
        m_ShadowMapTexture.reset();
        m_ShadowSampler.reset();
        m_ShadowMapHandle = {};
        m_ShadowRenderPass.reset();
        m_SkinnedShadowPipeline.reset();
        m_ShadowFramebuffers.clear();
        m_SkinnedShadowVertexShader.reset();
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

    void ShadowMapPass::Declare(RenderGraphBuilder &builder)
    {
        m_ShadowMapHandle = {};
        if (m_bInitialized &&
            m_ShadowMapTexture &&
            m_ShadowFramebuffers.size() == CSM_CASCADE_COUNT)
        {
            m_ShadowMapHandle = builder.ImportTexture(m_ShadowMapTexture,
                                                      RHI::ResourceState::DepthWrite,
                                                      "ShadowMap");
            if (m_ShadowMapHandle.IsValid())
            {
                builder.Write(m_ShadowMapHandle,
                              RHI::ResourceState::DepthWrite,
                              RHI::ResourceState::ShaderResource);
                builder.PublishTexture(RenderGraphResourceNames::ShadowMap, m_ShadowMapHandle);
                builder.ExportTexture(RenderGraphResourceNames::ShadowMap, m_ShadowMapHandle);
            }
        }

        builder.PreserveInsertionOrder();
    }

    void ShadowMapPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        (void)resources;
        Execute(context);
    }

    void ShadowMapPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_ShadowRenderPass ||
            m_ShadowFramebuffers.size() != CSM_CASCADE_COUNT ||
            !m_ShadowPipeline || !m_SkinnedShadowPipeline)
        {
            NORVES_LOG_WARNING("ShadowMapPass", "Shadow resources not ready, skipping");
            return;
        }

        context.ActiveShadowMapSettings = &m_Settings;
        CascadedShadowMatrixSettings cascadedSettings =
            MakeDefaultCascadedShadowMatrixSettings();
        cascadedSettings.ShadowMapResolution = m_Settings.Resolution;
        cascadedSettings.Directional = MakeDirectionalShadowMatrixSettings(m_Settings);
        // カスケードの視錐台の切片より光源側の遮蔽物もnear面で切り取らないよう、影を落とす
        // 物体の境界球を光源側の深度範囲に含める。
        Container::VariableArray<BoundingSphere> casterBounds;
        CollectDirectionalShadowCasterBounds(context.SnapshotMeshProxies,
                                             context.SnapshotSkinnedMeshProxies,
                                             context.SnapshotMegaGeometryProxies,
                                             casterBounds);
        const CascadedShadowMatrixResult cascadedShadowMatrices =
            BuildCascadedShadowLightMatrices(context.SnapshotLightProxies,
                                             context.GetActiveCamera(),
                                             cascadedSettings,
                                             &casterBounds);

        // 各カスケードのライトビュー・プロジェクションをGPU用データへ変換する。
        float lightViewData[PhysicalLightingShadowCascadeCount][16] = {};
        float lightProjData[PhysicalLightingShadowCascadeCount][16] = {};
        float splitDistances[PhysicalLightingShadowSplitCount] = {};
        for (uint32_t cascadeIndex = 0;
             cascadeIndex < PhysicalLightingShadowCascadeCount;
             ++cascadeIndex)
        {
            if (cascadedShadowMatrices.bEnabled)
            {
                const Math::Matrix4x4 lightProjMat =
                    context.Device->AdjustProjectionForClipSpace(
                        cascadedShadowMatrices.Cascades[cascadeIndex].Projection,
                        false);
                CopyShadowMatrixToShaderData(
                    cascadedShadowMatrices.Cascades[cascadeIndex].View,
                    lightViewData[cascadeIndex]);
                CopyShadowMatrixToShaderData(lightProjMat, lightProjData[cascadeIndex]);
            }
            else
            {
                CopyIdentityShadowMatricesToShaderData(
                    lightViewData[cascadeIndex],
                    lightProjData[cascadeIndex]);
            }
        }
        if (cascadedShadowMatrices.bEnabled)
        {
            for (uint32_t splitIndex = 0;
                 splitIndex < PhysicalLightingShadowSplitCount;
                 ++splitIndex)
            {
                splitDistances[splitIndex] = cascadedShadowMatrices.SplitDistances[splitIndex];
            }
        }
        context.PhysicalLighting.PublishCascadedShadow(
            &lightViewData[0][0],
            &lightProjData[0][0],
            splitDistances,
            cascadedShadowMatrices.bEnabled ? cascadedShadowMatrices.CascadeCount : 0u,
            cascadedShadowMatrices.LightId,
            cascadedShadowMatrices.bEnabled);
        // R1の単一行列利用者にはcascade 0を公開し、P6移行まで互換性を保つ。
        context.PhysicalLighting.PublishDirectionalShadow(
            lightViewData[0],
            lightProjData[0],
            cascadedShadowMatrices.LightId,
            cascadedShadowMatrices.bEnabled,
            m_ShadowMapTexture,
            m_ShadowSampler);

        // SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ公開する。
        if (m_bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ShadowMap", m_ShadowMapTexture);
        }

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_Settings.Resolution);
        viewport.height = static_cast<float>(m_Settings.Resolution);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_Settings.Resolution);
        scissor.bottom = static_cast<int32_t>(m_Settings.Resolution);

        // ========================================
        // DrawCommand駆動の描画（影を落とすメッシュのみ）
        // ========================================
        const DrawCommandView drawCommands = context.GetActiveDrawCommands();
        auto *meshes = context.Resources.Meshes;
        const bool bCanBuildShadowCommands =
            cascadedShadowMatrices.bEnabled &&
            m_SceneRenderer &&
            (meshes || context.SkinnedMeshes) &&
            !drawCommands.empty();

        const uint64_t instanceDataSize64 = context.InstanceDataBuffer
                                                ? context.InstanceDataBuffer->GetSize()
                                                : 0;
        const uint32_t instanceDataSize = instanceDataSize64 > 0xFFFFFFFFull
                                              ? 0xFFFFFFFFu
                                              : static_cast<uint32_t>(instanceDataSize64);

        // UBOデータ構造体（shadow.vertのShadowMVPに対応）
        struct ShadowPerObjectUBO
        {
            float lightView[16];
            float lightProjection[16];
        };

        if (bCanBuildShadowCommands)
        {
            // 4つのFramebufferで同じキャスターを記録するため、カスケードごとに
            // 別のUBOスロットを使用する。
            m_UniformAllocator.Reset();
        }

        for (uint32_t cascadeIndex = 0;
             cascadeIndex < PhysicalLightingShadowCascadeCount;
             ++cascadeIndex)
        {
            auto shadowCommands = MakeShared<Container::VariableArray<DrawCommand>>();
            if (bCanBuildShadowCommands)
            {
                // DrawCommand配列を取得し、影を落とすコマンドのみ描画
                for (const auto &cmd : drawCommands)
                {
                    if (!cmd.Draw.bCastShadow)
                    {
                        continue;
                    }

                    DrawCommand drawCommand = cmd;
                    const bool bSkinned = cmd.Draw.PayloadKind == DrawPayloadKind::Skinned;
                    if (bSkinned)
                    {
                        if (!TryPrepareSkinnedCommand(context, cmd, drawCommand))
                        {
                            continue;
                        }
                    }
                    else if (!context.InstanceDataBuffer || instanceDataSize == 0)
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
                    std::memcpy(uboData.lightView,
                                lightViewData[cascadeIndex],
                                sizeof(uboData.lightView));
                    std::memcpy(uboData.lightProjection,
                                lightProjData[cascadeIndex],
                                sizeof(uboData.lightProjection));

                    // UBO更新
                    allocation.UniformBuffer->Update(&uboData, sizeof(ShadowPerObjectUBO));
                    if (bSkinned)
                    {
                        allocation.DescriptorSet->BindStorageBuffer(
                            8,
                            drawCommand.Skinned.Prepared.PaletteBuffer,
                            0,
                            static_cast<uint32_t>(drawCommand.Skinned.Prepared.PaletteBuffer->GetSize()));
                        allocation.DescriptorSet->BindStorageBuffer(
                            9,
                            drawCommand.Skinned.Prepared.VertexBuffer,
                            0,
                            static_cast<uint32_t>(drawCommand.Skinned.Prepared.VertexBuffer->GetSize()));
                    }
                    else
                    {
                        allocation.DescriptorSet->BindStorageBuffer(7,
                                                                    context.InstanceDataBuffer,
                                                                    0,
                                                                    instanceDataSize);
                    }
                    allocation.DescriptorSet->Update();

                    if (!bSkinned)
                    {
                        drawCommand.Pipeline = m_ShadowPipeline;
                    }
                    drawCommand.DescriptorSet = allocation.DescriptorSet;
                    drawCommand.DescriptorSetSlot = 0;
                    shadowCommands->push_back(drawCommand);
                }
            }

            context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass(
                m_ShadowRenderPass,
                m_ShadowFramebuffers[cascadeIndex],
                shadowCommands,
                viewport,
                scissor,
                meshes));
        }
    }

    bool ShadowMapPass::TryPrepareSkinnedCommand(
        ViewRenderContext& context,
        const DrawCommand& source,
        DrawCommand& outCommand) const
    {
        if (source.Draw.PayloadKind != DrawPayloadKind::Skinned ||
            source.Draw.bInstanced || source.Draw.InstanceCount != 1 ||
            !context.SkinnedMeshes || !context.SnapshotSkinnedMeshFrameLeases ||
            source.Skinned.FrameLeaseIndex >= context.SnapshotSkinnedMeshFrameLeases->size())
        {
            return false;
        }

        const auto& frameLease =
            (*context.SnapshotSkinnedMeshFrameLeases)[source.Skinned.FrameLeaseIndex];
        SkinnedMeshPreparedDraw prepared;
        if (!context.SkinnedMeshes->PrepareDraw(frameLease,
                                                source.Skinned.BonePalette,
                                                source.Draw.WorldMatrix,
                                                prepared))
        {
            return false;
        }

        outCommand = source;
        outCommand.Draw.InstanceCount = 1;
        outCommand.Draw.bInstanced = false;
        outCommand.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
        outCommand.Skinned.FrameLease = frameLease;
        outCommand.Skinned.Prepared = prepared;
        outCommand.Pipeline = m_SkinnedShadowPipeline;
        return true;
    }

} // namespace NorvesLib::Core::Rendering
