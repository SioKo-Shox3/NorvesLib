#include "Rendering/ShadowMapPass.h"
#include "Rendering/SkinnedShadowComponentBindings.h"
#include "Rendering/SkinnedShadowStorage.h"
#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/CascadedShadowLightMatrices.h"
#include "Rendering/PointShadowSnapshot.h"
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

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 点光源のキューブシャドウの1面のUBO（point_shadow.vert/.fragのPointShadowFaceに対応）
        struct PointShadowFaceUBO
        {
            float lightView[16];
            float lightProjection[16];
            float lightPositionAndInvRange[4];
            // worldSource[0]=1の描画（MegaGeometry）はインスタンスではなくこの行列で置く
            float world[16];
            float worldSource[4];
        };
        static_assert(sizeof(PointShadowFaceUBO) == 224);

        // 非スキンは面ごとに1枠、スキンはcomponentごと・面ごとに1枠、MegaGeometryは描画ごと・面ごとに1枠。
        constexpr uint32_t PointShadowMaxSkinnedComponentsPerFace = SkinnedPointShadowComponentCapacity;
        constexpr uint32_t PointShadowMaxMegaDrawsPerFace = 8u;
        constexpr uint32_t PointShadowUniformSlotCount =
            PointShadowMaxLights * PointShadowFaceCount *
            (1u + PointShadowMaxSkinnedComponentsPerFace + PointShadowMaxMegaDrawsPerFace);

        // MegaGeometryの影のキャスター（LOD0の範囲を1回で描く）
        struct PointShadowMegaCaster
        {
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            uint32_t IndexCount = 0;
            float World[16] = {};
            BoundingSphere Bounds;
        };

        // ローカルの境界球をワールド行列（行ベクトル規約、並進は行3）で写す。半径は最大の軸の伸びで広げる。
        BoundingSphere TransformMegaBounds(const BoundingSphere& local, const float* world)
        {
            BoundingSphere result;
            result.CenterX = local.CenterX * world[0] + local.CenterY * world[4] +
                             local.CenterZ * world[8] + world[12];
            result.CenterY = local.CenterX * world[1] + local.CenterY * world[5] +
                             local.CenterZ * world[9] + world[13];
            result.CenterZ = local.CenterX * world[2] + local.CenterY * world[6] +
                             local.CenterZ * world[10] + world[14];
            float maxScaleSquared = 0.0f;
            for (uint32_t row = 0; row < 3; ++row)
            {
                const float lengthSquared = world[row * 4 + 0] * world[row * 4 + 0] +
                                            world[row * 4 + 1] * world[row * 4 + 1] +
                                            world[row * 4 + 2] * world[row * 4 + 2];
                maxScaleSquared = std::max(maxScaleSquared, lengthSquared);
            }
            result.Radius = local.Radius * std::sqrt(maxScaleSquared);
            return result;
        }

        // 物体IDで引くMeshProxyの境界球
        struct PointShadowMeshBounds
        {
            uint64_t ObjectId = 0;
            MeshDataHandle Mesh;
            BoundingSphere Bounds;
        };

        BoundingSphere MergeBoundingSpheres(const BoundingSphere& lhs, const BoundingSphere& rhs)
        {
            if (!(lhs.Radius > 0.0f))
            {
                return rhs;
            }
            if (!(rhs.Radius > 0.0f))
            {
                return lhs;
            }
            const float dx = rhs.CenterX - lhs.CenterX;
            const float dy = rhs.CenterY - lhs.CenterY;
            const float dz = rhs.CenterZ - lhs.CenterZ;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance + rhs.Radius <= lhs.Radius)
            {
                return lhs;
            }
            if (distance + lhs.Radius <= rhs.Radius)
            {
                return rhs;
            }
            BoundingSphere merged;
            merged.Radius = 0.5f * (distance + lhs.Radius + rhs.Radius);
            const float t = distance > 0.0f ? (merged.Radius - lhs.Radius) / distance : 0.0f;
            merged.CenterX = lhs.CenterX + dx * t;
            merged.CenterY = lhs.CenterY + dy * t;
            merged.CenterZ = lhs.CenterZ + dz * t;
            return merged;
        }

        // 描画コマンドのワールド境界球を求める。求められないときはfalse（選別せずに描く）。
        // インスタンシング描画は同じメッシュを持つ影を落とすMeshProxyの境界球をすべて包む球にする。
        bool ResolvePointShadowDrawBounds(
            const DrawCommand& command,
            const Container::VariableArray<PointShadowMeshBounds>& meshBoundsByObject,
            const Container::VariableArray<SkinnedMeshProxy>* skinnedMeshProxies,
            BoundingSphere& outBounds)
        {
            if (command.Draw.PayloadKind == DrawPayloadKind::Skinned)
            {
                if (skinnedMeshProxies == nullptr)
                {
                    return false;
                }
                for (const SkinnedMeshProxy& proxy : *skinnedMeshProxies)
                {
                    if (proxy.ComponentId == command.Draw.SourceMeshComponentId)
                    {
                        return BuildSkinnedShadowCasterWorldBounds(proxy, outBounds);
                    }
                }
                return false;
            }

            if (command.Draw.bInstanced || command.Draw.InstanceCount != 1)
            {
                BoundingSphere merged;
                bool bFound = false;
                for (const PointShadowMeshBounds& entry : meshBoundsByObject)
                {
                    if (entry.Mesh == command.Draw.MeshHandle)
                    {
                        merged = MergeBoundingSpheres(merged, entry.Bounds);
                        bFound = true;
                    }
                }
                outBounds = merged;
                return bFound && merged.Radius > 0.0f;
            }

            const auto found = std::lower_bound(
                meshBoundsByObject.begin(),
                meshBoundsByObject.end(),
                command.Draw.ObjectId,
                [](const PointShadowMeshBounds& entry, uint64_t objectId)
                {
                    return entry.ObjectId < objectId;
                });
            for (auto it = found;
                 it != meshBoundsByObject.end() && it->ObjectId == command.Draw.ObjectId;
                 ++it)
            {
                if (it->Mesh == command.Draw.MeshHandle)
                {
                    outBounds = it->Bounds;
                    return outBounds.Radius > 0.0f;
                }
            }
            return false;
        }
    } // namespace

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
        m_PointShadowCubeTexture.reset();
        m_PointShadowCubeHandle = {};
        m_PointShadowFramebuffers.clear();
        m_PointShadowVertexShader.reset();
        m_SkinnedPointShadowVertexShader.reset();
        m_PointShadowFragmentShader.reset();
        m_PointShadowPipeline.reset();
        m_SkinnedPointShadowPipeline.reset();
        m_PointShadowUniformAllocator.Shutdown();
        m_bPointShadowResourcesReady = false;
        m_bPointShadowResourcesFailed = false;
        m_LoggedPointShadowLightCount = 0;
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

        // 影を落とす点光源があるフレームだけキューブ配列を公開する（無いフレームは何も描かない）。
        m_PointShadowCubeHandle = {};
        const ViewRenderContext* declareContext = builder.GetContext();
        if (m_bInitialized &&
            declareContext != nullptr &&
            declareContext->SnapshotPointShadows != nullptr &&
            !declareContext->SnapshotPointShadows->IsEmpty() &&
            EnsurePointShadowResources(*declareContext))
        {
            m_PointShadowCubeHandle = builder.ImportTexture(m_PointShadowCubeTexture,
                                                            RHI::ResourceState::DepthWrite,
                                                            "PointShadowCubeMap");
            if (m_PointShadowCubeHandle.IsValid())
            {
                builder.Write(m_PointShadowCubeHandle,
                              RHI::ResourceState::DepthWrite,
                              RHI::ResourceState::ShaderResource);
                builder.PublishTexture(RenderGraphResourceNames::PointShadowCubeMap,
                                       m_PointShadowCubeHandle);
                builder.ExportTexture(RenderGraphResourceNames::PointShadowCubeMap,
                                      m_PointShadowCubeHandle);
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
        cascadedSettings.MaxShadowDistance = m_Settings.MaxShadowDistance;
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

            // 分割とテクセルの大きさはカメラのnear・画角・影の最大距離だけで決まるため、
            // 分割の奥が変わったときだけ記録する。
            const float splitFar = cascadedShadowMatrices.SplitDistances[CSM_CASCADE_COUNT];
            if (splitFar != m_LoggedCascadeSplitFar)
            {
                m_LoggedCascadeSplitFar = splitFar;
                NORVES_LOG_INFO("ShadowMapPass",
                                "CSMの分割: csm_splits=%.3f,%.3f,%.3f,%.3f,%.3f m "
                                "csm_texel_m=%.4f,%.4f,%.4f,%.4f",
                                cascadedShadowMatrices.SplitDistances[0],
                                cascadedShadowMatrices.SplitDistances[1],
                                cascadedShadowMatrices.SplitDistances[2],
                                cascadedShadowMatrices.SplitDistances[3],
                                cascadedShadowMatrices.SplitDistances[4],
                                cascadedShadowMatrices.Cascades[0].TexelSize,
                                cascadedShadowMatrices.Cascades[1].TexelSize,
                                cascadedShadowMatrices.Cascades[2].TexelSize,
                                cascadedShadowMatrices.Cascades[3].TexelSize);
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

        ExecutePointShadows(context);
    }

    bool ShadowMapPass::EnsurePointShadowResources(const ViewRenderContext& context)
    {
        if (m_bPointShadowResourcesReady)
        {
            return true;
        }
        if (m_bPointShadowResourcesFailed || !m_Device || !m_ShadowRenderPass || !context.ShaderMgr)
        {
            return false;
        }

        // 途中で失敗したら作りかけを捨て、以後は点光源の影を描かない（CSMと既存の描画は続ける）。
        const auto failPointShadow = [this](const char* reason)
        {
            NORVES_LOG_ERROR("ShadowMapPass", "点光源のキューブシャドウを作れません: %s", reason);
            m_PointShadowCubeTexture.reset();
            m_PointShadowFramebuffers.clear();
            m_PointShadowVertexShader.reset();
            m_SkinnedPointShadowVertexShader.reset();
            m_PointShadowFragmentShader.reset();
            m_PointShadowPipeline.reset();
            m_SkinnedPointShadowPipeline.reset();
            m_PointShadowUniformAllocator.Shutdown();
            m_bPointShadowResourcesFailed = true;
            return false;
        };

        m_PointShadowVertexShader =
            context.ShaderMgr->LoadShader("point_shadow.vert", RHI::ShaderStage::Vertex);
        m_SkinnedPointShadowVertexShader =
            context.ShaderMgr->LoadShader("skinned_point_shadow.vert", RHI::ShaderStage::Vertex);
        m_PointShadowFragmentShader =
            context.ShaderMgr->LoadShader("point_shadow.frag", RHI::ShaderStage::Pixel);
        if (!m_PointShadowVertexShader || !m_SkinnedPointShadowVertexShader ||
            !m_PointShadowFragmentShader)
        {
            return failPointShadow("shader");
        }

        // 灯ごとに6面（層 = 灯の番号 * 6 + 面）のキューブ配列。CSMと同じ深度形式で描き、
        // 深度には光源からの線形距離を範囲で割った値を書く。
        const uint32_t faceResolution = m_Settings.PointShadowResolution;
        RHI::TextureDesc cubeDesc = RHI::TextureDesc::DepthStencil(
            faceResolution, faceResolution, m_Settings.DepthFormat, "PointShadowCubeMap");
        cubeDesc.ArraySize = PointShadowMaxLights;
        cubeDesc.Dimension = RHI::TextureDimension::Texture2D;
        cubeDesc.IsCubemap = true;
        m_PointShadowCubeTexture = m_Device->CreateTexture(cubeDesc);
        if (!m_PointShadowCubeTexture)
        {
            return failPointShadow("cube texture");
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_ShadowRenderPass;
        fbDesc.depthStencilTarget = m_PointShadowCubeTexture;
        fbDesc.width = faceResolution;
        fbDesc.height = faceResolution;
        m_PointShadowFramebuffers.clear();
        m_PointShadowFramebuffers.reserve(PointShadowMaxLights * PointShadowFaceCount);
        for (uint32_t layer = 0; layer < PointShadowMaxLights * PointShadowFaceCount; ++layer)
        {
            fbDesc.depthStencilArrayLayer = layer;
            RHI::FramebufferPtr framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!framebuffer)
            {
                return failPointShadow("framebuffer");
            }
            m_PointShadowFramebuffers.push_back(framebuffer);
        }

        // set=0: 0=面のUBO（頂点・フラグメント）、7=インスタンス、8・9=スキニング
        RHI::DescriptorSetDesc setDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        setDesc.bindings.push_back(uboBinding);
        for (uint32_t bindingIndex = 7; bindingIndex <= 9; ++bindingIndex)
        {
            RHI::DescriptorBinding storageBinding;
            storageBinding.binding = bindingIndex;
            storageBinding.type = RHI::ResourceBindType::StructuredBuffer;
            storageBinding.stages = RHI::ShaderStage::Vertex;
            setDesc.bindings.push_back(storageBinding);
        }
        if (!m_PointShadowUniformAllocator.Initialize(m_Device,
                                                      sizeof(PointShadowFaceUBO),
                                                      PointShadowUniformSlotCount,
                                                      setDesc))
        {
            return failPointShadow("uniform allocator");
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_PointShadowVertexShader;
        pipelineDesc.pixelShader = m_PointShadowFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        RHI::VertexAttributeDesc positionAttribute;
        positionAttribute.location = 0;
        positionAttribute.binding = 0;
        positionAttribute.format = RHI::Format::R32G32B32_FLOAT;
        positionAttribute.offset = 0;
        pipelineDesc.vertexAttributes.push_back(positionAttribute);

        RHI::VertexAttributeDesc normalAttribute;
        normalAttribute.location = 1;
        normalAttribute.binding = 0;
        normalAttribute.format = RHI::Format::R32G32B32_FLOAT;
        normalAttribute.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttribute);

        // CSMと同じく、光源に背を向けた面や両面の材質もキューブへ描く。
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;
        pipelineDesc.renderPass = m_ShadowRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(setDesc);

        m_PointShadowPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_PointShadowPipeline)
        {
            return failPointShadow("pipeline");
        }

        RHI::GraphicsPipelineDesc skinnedPipelineDesc = pipelineDesc;
        skinnedPipelineDesc.vertexShader = m_SkinnedPointShadowVertexShader;
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

        m_SkinnedPointShadowPipeline = m_Device->CreateGraphicsPipeline(skinnedPipelineDesc);
        if (!m_SkinnedPointShadowPipeline)
        {
            return failPointShadow("skinned pipeline");
        }

        m_bPointShadowResourcesReady = true;
        NORVES_LOG_INFO("ShadowMapPass",
                        "点光源のキューブシャドウを作成: point_shadow_face_resolution=%u "
                        "point_shadow_max_lights=%u",
                        faceResolution,
                        PointShadowMaxLights);
        return true;
    }

    void ShadowMapPass::ExecutePointShadows(ViewRenderContext& context)
    {
        const PointShadowSnapshot* snapshot = context.SnapshotPointShadows;
        const uint32_t lightCount =
            snapshot != nullptr ? std::min(snapshot->LightCount, PointShadowMaxLights) : 0u;
        if (lightCount != m_LoggedPointShadowLightCount)
        {
            m_LoggedPointShadowLightCount = lightCount;
            NORVES_LOG_INFO("ShadowMapPass",
                            "点光源のキューブシャドウ: point_shadow_lights=%u",
                            lightCount);
        }
        // 影を落とす点光源が無いフレームは何も描かない（Declareも公開しない）。
        if (lightCount == 0 || !m_PointShadowCubeHandle.IsValid() ||
            !EnsurePointShadowResources(context))
        {
            return;
        }

        m_PointShadowUniformAllocator.Reset();

        // キャスターの境界球を物体IDで引けるように並べる。
        Container::VariableArray<PointShadowMeshBounds> meshBoundsByObject;
        if (context.SnapshotMeshProxies != nullptr)
        {
            meshBoundsByObject.reserve(context.SnapshotMeshProxies->size());
            for (const MeshProxy& proxy : *context.SnapshotMeshProxies)
            {
                if (!proxy.bVisible || !proxy.bCastShadow)
                {
                    continue;
                }
                PointShadowMeshBounds entry;
                entry.ObjectId = proxy.ObjectId;
                entry.Mesh = proxy.MeshHandle;
                entry.Bounds = proxy.WorldBounds;
                meshBoundsByObject.push_back(entry);
            }
            std::sort(meshBoundsByObject.begin(),
                      meshBoundsByObject.end(),
                      [](const PointShadowMeshBounds& lhs, const PointShadowMeshBounds& rhs)
                      {
                          return lhs.ObjectId < rhs.ObjectId;
                      });
        }

        const uint64_t instanceDataSize64 = context.InstanceDataBuffer
                                                ? context.InstanceDataBuffer->GetSize()
                                                : 0;
        const uint32_t instanceDataSize = instanceDataSize64 > 0xFFFFFFFFull
                                              ? 0xFFFFFFFFu
                                              : static_cast<uint32_t>(instanceDataSize64);

        // 影を落とす描画を1回だけ用意し、境界球を求めておく（スキンはパレットをここで確定する）。
        struct PointShadowCaster
        {
            DrawCommand Command;
            BoundingSphere Bounds;
            bool bHasBounds = false;
            bool bSkinned = false;
        };
        Container::VariableArray<PointShadowCaster> casters;
        const DrawCommandView drawCommands = context.GetActiveDrawCommands();
        auto* meshes = context.Resources.Meshes;
        if (m_SceneRenderer && (meshes || context.SkinnedMeshes))
        {
            for (const DrawCommand& command : drawCommands)
            {
                if (!command.Draw.bCastShadow)
                {
                    continue;
                }
                PointShadowCaster caster;
                caster.bSkinned = command.Draw.PayloadKind == DrawPayloadKind::Skinned;
                if (caster.bSkinned)
                {
                    if (!TryPrepareSkinnedCommand(context, command, caster.Command))
                    {
                        continue;
                    }
                    caster.Command.Pipeline = m_SkinnedPointShadowPipeline;
                }
                else
                {
                    if (!context.InstanceDataBuffer || instanceDataSize == 0)
                    {
                        continue;
                    }
                    caster.Command = command;
                    caster.Command.Pipeline = m_PointShadowPipeline;
                }
                caster.bHasBounds = ResolvePointShadowDrawBounds(command,
                                                                 meshBoundsByObject,
                                                                 context.SnapshotSkinnedMeshProxies,
                                                                 caster.Bounds);
                casters.push_back(caster);
            }
        }

        // MegaGeometry（岩・小屋など）はGBufferではクラスタ単位でGPUが選ぶが、キューブへはLOD0をそのまま描く。
        // 行列はMegaGeometryPassと同じくプロキシの行列をそのままシェーダーへ渡す。
        Container::VariableArray<PointShadowMegaCaster> megaCasters;
        if (context.SnapshotMegaGeometryProxies && context.Resources.MegaGeometry &&
            context.InstanceDataBuffer && instanceDataSize > 0)
        {
            for (const MegaGeometryProxy& proxy : *context.SnapshotMegaGeometryProxies)
            {
                if (!proxy.IsValid() || !proxy.bCastShadow)
                {
                    continue;
                }
                const MegaGeometry::MegaMeshGPUData* gpuData =
                    context.Resources.MegaGeometry->GetMegaMeshGPUData(proxy.MegaMeshHandle);
                if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer ||
                    gpuData->ShadowIndexCount == 0u)
                {
                    continue;
                }
                PointShadowMegaCaster caster;
                caster.VertexBuffer = gpuData->VertexBuffer;
                caster.IndexBuffer = gpuData->IndexBuffer;
                caster.IndexCount = gpuData->ShadowIndexCount;
                std::memcpy(caster.World, &proxy.WorldTransform, sizeof(caster.World));
                caster.Bounds = TransformMegaBounds(gpuData->TotalBounds, caster.World);
                megaCasters.push_back(caster);
            }
        }

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_Settings.PointShadowResolution);
        viewport.height = static_cast<float>(m_Settings.PointShadowResolution);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_Settings.PointShadowResolution);
        scissor.bottom = static_cast<int32_t>(m_Settings.PointShadowResolution);

        // 使わないキューブの面も消去だけ行い、配列の全層を読める状態にする。
        for (uint32_t cubeIndex = 0; cubeIndex < PointShadowMaxLights; ++cubeIndex)
        {
            const bool bActiveLight = cubeIndex < lightCount;
            const PointShadowLightSnapshot* light =
                bActiveLight ? &snapshot->Lights[cubeIndex] : nullptr;
            const Math::Matrix4x4 projection =
                bActiveLight ? context.Device->AdjustProjectionForClipSpace(light->Faces.Projection,
                                                                            false)
                             : Math::Matrix4x4::Identity;

            for (uint32_t faceIndex = 0; faceIndex < PointShadowFaceCount; ++faceIndex)
            {
                auto faceCommands = MakeShared<Container::VariableArray<DrawCommand>>();
                if (bActiveLight && (!casters.empty() || !megaCasters.empty()))
                {
                    PointShadowFaceUBO faceData = {};
                    CopyShadowMatrixToShaderData(light->Faces.Views[faceIndex], faceData.lightView);
                    CopyShadowMatrixToShaderData(projection, faceData.lightProjection);
                    faceData.lightPositionAndInvRange[0] = light->Position.x;
                    faceData.lightPositionAndInvRange[1] = light->Position.y;
                    faceData.lightPositionAndInvRange[2] = light->Position.z;
                    faceData.lightPositionAndInvRange[3] = 1.0f / light->Range;

                    // 非スキンの描画はこの面のUBOとインスタンスを1つの記述子セットで共有する。
                    RHI::DescriptorSetPtr sharedFaceSet;
                    SkinnedShadowComponentBindings<RHI::DescriptorSetPtr> skinnedFaceSets;
                    for (const PointShadowCaster& caster : casters)
                    {
                        if (caster.bHasBounds &&
                            !PointShadowCasterIntersectsLight(caster.Bounds, *light))
                        {
                            continue;
                        }

                        DrawCommand drawCommand = caster.Command;
                        if (caster.bSkinned)
                        {
                            const auto& prepared = drawCommand.Skinned.Prepared;
                            const SkinnedShadowBindingKey key{prepared.ComponentId,prepared.PreparationEpoch,
                                prepared.MeshHandle.Id,prepared.MeshHandle.Generation,
                                prepared.PaletteBuffer.get(),prepared.VertexBuffer.get()};
                            const auto createSet = [&]() -> RHI::DescriptorSetPtr
                            {
                                auto allocation = m_PointShadowUniformAllocator.Allocate();
                                if (!allocation.UniformBuffer || !allocation.DescriptorSet)
                                {
                                    NORVES_LOG_WARNING("ShadowMapPass",
                                        "点光源の影のUBOが足りないためcomponent全体を省きます");
                                    return {};
                                }
                                allocation.UniformBuffer->Update(&faceData,sizeof(faceData));
                                if (!BindSkinnedShadowStorage(prepared,allocation.DescriptorSet.get()))
                                {
                                    return {};
                                }
                                return allocation.DescriptorSet;
                            };
                            if (!skinnedFaceSets.TryGet(key,createSet,drawCommand.DescriptorSet))
                            {
                                continue;
                            }
                        }
                        else
                        {
                            if (!sharedFaceSet)
                            {
                                auto allocation = m_PointShadowUniformAllocator.Allocate();
                                if (!allocation.UniformBuffer)
                                {
                                    NORVES_LOG_WARNING("ShadowMapPass",
                                                       "点光源の影のUBOが足りないため面の描画を省きます");
                                    break;
                                }
                                allocation.UniformBuffer->Update(&faceData, sizeof(faceData));
                                allocation.DescriptorSet->BindStorageBuffer(7,
                                                                            context.InstanceDataBuffer,
                                                                            0,
                                                                            instanceDataSize);
                                allocation.DescriptorSet->Update();
                                sharedFaceSet = allocation.DescriptorSet;
                            }
                            drawCommand.DescriptorSet = sharedFaceSet;
                        }
                        drawCommand.DescriptorSetSlot = 0;
                        faceCommands->push_back(drawCommand);
                    }

                    uint32_t megaDrawCount = 0;
                    for (const PointShadowMegaCaster& megaCaster : megaCasters)
                    {
                        if (!PointShadowCasterIntersectsLight(megaCaster.Bounds, *light))
                        {
                            continue;
                        }
                        if (megaDrawCount >= PointShadowMaxMegaDrawsPerFace)
                        {
                            NORVES_LOG_WARNING("ShadowMapPass",
                                               "点光源の影へ描くMegaGeometryが1面の上限（%u）を超えたため省きます",
                                               PointShadowMaxMegaDrawsPerFace);
                            break;
                        }
                        auto allocation = m_PointShadowUniformAllocator.Allocate();
                        if (!allocation.UniformBuffer)
                        {
                            NORVES_LOG_WARNING("ShadowMapPass",
                                               "点光源の影のUBOが足りないためMegaGeometryの描画を省きます");
                            break;
                        }
                        PointShadowFaceUBO megaFaceData = faceData;
                        std::memcpy(megaFaceData.world, megaCaster.World, sizeof(megaFaceData.world));
                        megaFaceData.worldSource[0] = 1.0f;
                        allocation.UniformBuffer->Update(&megaFaceData, sizeof(megaFaceData));
                        // インスタンスは読まないが、レイアウトの束縛を満たすために結ぶ。
                        allocation.DescriptorSet->BindStorageBuffer(7,
                                                                    context.InstanceDataBuffer,
                                                                    0,
                                                                    instanceDataSize);
                        allocation.DescriptorSet->Update();

                        DrawCommand megaCommand;
                        megaCommand.Type = DrawCommandType::DrawIndexed;
                        megaCommand.Pipeline = m_PointShadowPipeline;
                        megaCommand.DescriptorSet = allocation.DescriptorSet;
                        megaCommand.DescriptorSetSlot = 0;
                        megaCommand.Draw.PayloadKind = DrawPayloadKind::Mesh2D;
                        megaCommand.Draw.bCastShadow = true;
                        megaCommand.Mesh2D.VertexBuffer = megaCaster.VertexBuffer;
                        megaCommand.Mesh2D.IndexBuffer = megaCaster.IndexBuffer;
                        megaCommand.Mesh2D.IndexCount = megaCaster.IndexCount;
                        megaCommand.Mesh2D.IndexOffset = 0;
                        megaCommand.Mesh2D.VertexOffset = 0;
                        megaCommand.Mesh2D.IndexType = RHI::IndexType::Uint32;
                        faceCommands->push_back(megaCommand);
                        ++megaDrawCount;
                    }
                }

                context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass(
                    m_ShadowRenderPass,
                    m_PointShadowFramebuffers[cubeIndex * PointShadowFaceCount + faceIndex],
                    faceCommands,
                    viewport,
                    scissor,
                    meshes));
            }
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
        if (!frameLease || (frameLease->ComponentId != 0 && frameLease->ComponentId != source.Draw.SourceMeshComponentId))
        {
            return false;
        }
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
