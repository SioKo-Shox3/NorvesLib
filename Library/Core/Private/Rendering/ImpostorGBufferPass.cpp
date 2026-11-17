#include "Rendering/ImpostorGBufferPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DepthConvention.h"
#include "Rendering/EnvironmentGpuParams.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ImpostorBatch.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t kColorTargetCount = GBufferOverlayTargets::kColorTargetCount;

        /// GBuffer の 5 枚の名前をシェーダーの出力順に返す。
        const Identity &ColorTargetName(uint32_t index)
        {
            static const Identity kNames[] = {
                RenderGraphResourceNames::GBufferAlbedo, RenderGraphResourceNames::GBufferNormal,
                RenderGraphResourceNames::GBufferMaterial, RenderGraphResourceNames::GBufferEmissive,
                RenderGraphResourceNames::GBufferVelocity,
            };
            return kNames[index];
        }

        /// インポスターのシェーダーが使うセット 0 の束縛を作る。
        RHI::DescriptorSetDesc CreateDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding uniform;
            uniform.binding = 0;
            uniform.type = RHI::ResourceBindType::ConstantBuffer;
            uniform.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            desc.bindings.push_back(uniform);

            for (uint32_t binding = 1; binding <= 4; ++binding)
            {
                RHI::DescriptorBinding atlas;
                atlas.binding = binding;
                atlas.type = RHI::ResourceBindType::CombinedImageSampler;
                atlas.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(atlas);
            }

            RHI::DescriptorBinding environment;
            environment.binding = 5;
            environment.type = RHI::ResourceBindType::ConstantBuffer;
            environment.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(environment);

            RHI::DescriptorBinding instances;
            instances.binding = 7;
            instances.type = RHI::ResourceBindType::StructuredBuffer;
            instances.stages = RHI::ShaderStage::Vertex;
            desc.bindings.push_back(instances);
            return desc;
        }
    } // namespace

    ImpostorGBufferPass::~ImpostorGBufferPass()
    {
        Shutdown();
    }

    bool ImpostorGBufferPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        // View のグラフ構築時には初期化せず、描き先を宣言できた Execute まで待つ。
        if (!m_bInitializeResources)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("ImpostorGBufferPass", "IMPOSTOR_GBUFFER_INIT_FAILED デバイスかシェーダー管理がありません");
            return false;
        }

        try
        {
            m_Device = context.Device;
            m_VertexShader = context.ShaderMgr->LoadShader("impostor_gbuffer.vert", RHI::ShaderStage::Vertex);
            m_FragmentShader = context.ShaderMgr->LoadShader("impostor_gbuffer.frag", RHI::ShaderStage::Pixel);
            const RHI::DescriptorSetDesc descriptorSetDesc = CreateDescriptorSetDesc();
            m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(descriptorSetDesc);

            RHI::TextureDesc textureDesc;
            textureDesc.Width = 1;
            textureDesc.Height = 1;
            textureDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            textureDesc.DebugName = "ImpostorGBuffer.DefaultAtlas";
            m_DefaultTexture = m_Device->CreateTexture(textureDesc);

            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_AtlasSampler = m_Device->CreateSampler(samplerDesc);

            if (!m_VertexShader || !m_FragmentShader || !m_DescriptorSetLayout || !m_DefaultTexture || !m_AtlasSampler)
            {
                NORVES_LOG_ERROR("ImpostorGBufferPass", "IMPOSTOR_GBUFFER_INIT_FAILED シェーダーか描画資源を作れません");
                Shutdown();
                return false;
            }
            const uint32_t zero = 0;
            m_DefaultTexture->Update(&zero, sizeof(zero), sizeof(zero));

            m_OverlayPipelineDesc = {};
            m_OverlayPipelineDesc.vertexShader = m_VertexShader;
            m_OverlayPipelineDesc.pixelShader = m_FragmentShader;
            m_OverlayPipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);
            m_OverlayPipelineDesc.depthCompareOp = DepthConvention::DepthCompareOpOrEqual();
            m_OverlayPipelineDesc.cullMode = RHI::CullMode::None;
            m_OverlayPipelineDesc.debugName = "ImpostorGBufferPass";
            m_OverlayPipelineDesc.failureLogPrefix = "IMPOSTOR_GBUFFER";
            m_bInitialized = true;
            return true;
        }
        catch (...)
        {
            NORVES_LOG_ERROR("ImpostorGBufferPass", "IMPOSTOR_GBUFFER_INIT_FAILED 描画資源の初期化中に例外が発生しました");
            Shutdown();
            return false;
        }
    }

    void ImpostorGBufferPass::Shutdown()
    {
        m_OverlayTargets.Reset();
        m_OverlayPipelineDesc = {};
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_DescriptorSetLayout.reset();
        m_DefaultTexture.reset();
        m_AtlasSampler.reset();
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};
        m_Device = nullptr;
        m_LoggedBatchCount = 0;
        m_LoggedInstanceCount = 0;
        m_bInitialized = false;
    }

    void ImpostorGBufferPass::Setup(ViewRenderContext &context)
    {
        (void)context;
    }

    void ImpostorGBufferPass::Execute(ViewRenderContext &context)
    {
        (void)context;
    }

    void ImpostorGBufferPass::Declare(RenderGraphBuilder &builder)
    {
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};

        const ViewRenderContext *context = builder.GetContext();
        if (!context || context->GetActiveImpostorBatches().empty() || !context->GetActiveCamera())
        {
            builder.PreserveInsertionOrder();
            return;
        }

        // すべての描き先がそろう構成でだけ書き込みを宣言する。
        RGTextureHandle available;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            if (!builder.TryGetTexture(ColorTargetName(index), available))
            {
                builder.PreserveInsertionOrder();
                return;
            }
        }
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, available))
        {
            builder.PreserveInsertionOrder();
            return;
        }

        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RGTextureHandle handle;
            if (builder.TryUseAttachment(ColorTargetName(index), handle, RGAttachmentKind::Color,
                                         RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store, RHI::ResourceState::RenderTarget,
                                         RHI::ResourceState::ShaderResource))
            {
                m_ColorHandles[index] = handle.ToResourceHandle();
            }
        }
        RGTextureHandle depth;
        if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth, depth, RGAttachmentKind::DepthStencil,
                                     RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                     RHI::AttachmentStoreOp::Store, RHI::ResourceState::DepthWrite,
                                     RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depth.ToResourceHandle();
        }
        builder.PreserveInsertionOrder();
    }

    void ImpostorGBufferPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        const auto batches = context.GetActiveImpostorBatches();
        const CameraProxy *camera = context.GetActiveCamera();
        if (batches.empty() || !camera || !context.CommandList || !m_DepthHandle.IsValid())
        {
            return;
        }
        for (const RGResourceHandle &handle : m_ColorHandles)
        {
            if (!handle.IsValid())
            {
                return;
            }
        }
        RHI::TexturePtr colorTargets[kColorTargetCount];
        bool allTargetsAvailable = true;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            colorTargets[index] = resources.GetTexture(m_ColorHandles[index]);
            allTargetsAvailable = allTargetsAvailable && static_cast<bool>(colorTargets[index]);
        }
        RHI::TexturePtr depthTarget = resources.GetTexture(m_DepthHandle);
        // render pass を開けないときも、宣言した描き先の最終状態を後続へ渡す。
        const auto finishWithoutDrawing = [&]()
        {
            for (const RHI::TexturePtr &target : colorTargets)
            {
                if (target)
                {
                    context.CommandList->TextureBarrier(target, RHI::ResourceState::RenderTarget,
                                                        RHI::ResourceState::ShaderResource);
                }
            }
            if (depthTarget)
            {
                context.CommandList->TextureBarrier(depthTarget, RHI::ResourceState::DepthWrite,
                                                    RHI::ResourceState::ShaderResource);
            }
        };
        if (!allTargetsAvailable || !depthTarget)
        {
            finishWithoutDrawing();
            return;
        }
        if (!m_bInitialized)
        {
            m_bInitializeResources = true;
            const bool initialized = Initialize(context);
            m_bInitializeResources = false;
            if (!initialized)
            {
                finishWithoutDrawing();
                return;
            }
        }
        bool overlayReady = false;
        try
        {
            overlayReady = m_OverlayTargets.Ensure(m_Device, colorTargets, depthTarget, m_OverlayPipelineDesc);
        }
        catch (...)
        {
            NORVES_LOG_ERROR("ImpostorGBufferPass", "IMPOSTOR_GBUFFER_INIT_FAILED パイプラインの作成中に例外が発生しました");
            finishWithoutDrawing();
            return;
        }
        if (!overlayReady)
        {
            finishWithoutDrawing();
            return;
        }

        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        const CameraProxy *previousCamera = context.GetPreviousCamera();
        const CameraViewConstants previousConstants =
            previousCamera ? CameraViewConstants::BuildForDevice(*previousCamera, context.GetActiveAspectRatio(), context.Device)
                           : cameraConstants;
        ImpostorGBufferUniform frameTemplate{};
        cameraConstants.CopyShaderView(frameTemplate.view);
        cameraConstants.CopyShaderProjection(frameTemplate.projection);
        previousConstants.CopyShaderView(frameTemplate.previousView);
        previousConstants.CopyShaderProjection(frameTemplate.previousProjection);
        cameraConstants.CopyCameraPosition(frameTemplate.cameraPosition);
        frameTemplate.cameraRight[0] = camera->RightX;
        frameTemplate.cameraRight[1] = camera->RightY;
        frameTemplate.cameraRight[2] = camera->RightZ;
        frameTemplate.cameraUp[0] = camera->UpX;
        frameTemplate.cameraUp[1] = camera->UpY;
        frameTemplate.cameraUp[2] = camera->UpZ;
        frameTemplate.frameParams[0] = previousCamera ? 1.0f : 0.0f;
        frameTemplate.atlasParams[1] = 0.5f;
        frameTemplate.atlasParams[2] = 0.8f;
        frameTemplate.atlasParams[3] = 0.0f;

        struct PreparedBatch
        {
            RHI::DescriptorSetPtr DescriptorSet;
            uint32_t InstanceCount = 0;
            uint32_t InstanceDataOffset = 0;
        };
        Container::VariableArray<PreparedBatch> prepared;
        prepared.reserve(batches.size());
        uint64_t drawnInstances = 0;

        FrameResourceLane *frameResources = context.FrameResources;
        auto *textures = context.Resources.Textures;
        const uint64_t bufferSize = context.InstanceDataBuffer ? context.InstanceDataBuffer->GetSize() : 0;
        const uint32_t instanceDataSize = bufferSize > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(bufferSize);
        if (frameResources && textures && context.SnapshotInstanceData && instanceDataSize > 0)
        {
            const FrameUploadAllocation environment = AcquireEnvironmentConstants(context);
            // 記録済みのコマンドが参照するページへ後から書かないよう、全部の束を先に準備する。
            for (const ImpostorBatch &batch : batches)
            {
                const uint64_t instanceEnd = static_cast<uint64_t>(batch.InstanceDataOffset) + batch.InstanceCount;
                if (batch.InstanceCount == 0 || instanceEnd > context.SnapshotInstanceData->size() ||
                    instanceEnd > instanceDataSize / sizeof(GPUSceneInstanceData))
                {
                    continue;
                }

                RHI::TexturePtr atlases[4];
                const TextureHandle handles[4] = {batch.AlbedoAtlas, batch.NormalAtlas, batch.OrmAtlas, batch.DepthAtlas};
                for (uint32_t index = 0; index < 4; ++index)
                {
                    if (handles[index].IsValid())
                    {
                        atlases[index] = textures->GetRHITexturePtr(handles[index]);
                    }
                }
                if (!atlases[0])
                {
                    continue;
                }

                ImpostorGBufferUniform uniform = frameTemplate;
                for (uint32_t index = 1; index < 4; ++index)
                {
                    uniform.frameParams[index] = atlases[index] ? 1.0f : 0.0f;
                    if (!atlases[index])
                    {
                        atlases[index] = m_DefaultTexture;
                    }
                }
                uniform.atlasParams[0] = static_cast<float>(batch.Layout);
                const FrameUploadAllocation constants = frameResources->AllocateConstants(uniform);
                const FrameDescriptorAllocation descriptor = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
                if (!constants.IsValid() || !environment.IsValid() || !descriptor.IsValid())
                {
                    continue;
                }

                descriptor.Set->BindConstantBuffer(0, constants.Buffer, constants.GetOffset32(), constants.GetSize32());
                for (uint32_t index = 0; index < 4; ++index)
                {
                    descriptor.Set->BindTexture(index + 1, atlases[index]);
                    descriptor.Set->BindSampler(index + 1, m_AtlasSampler);
                }
                descriptor.Set->BindConstantBuffer(5, environment.Buffer, environment.GetOffset32(), environment.GetSize32());
                descriptor.Set->BindStorageBuffer(7, context.InstanceDataBuffer, 0, instanceDataSize);
                descriptor.Set->Update();
                prepared.push_back(PreparedBatch{descriptor.Set, batch.InstanceCount, batch.InstanceDataOffset});
                drawnInstances += batch.InstanceCount;
            }
        }

        RHI::ICommandList *commandList = context.CommandList;
        // 描ける束が無い場合も、宣言した最終状態へ描き先を移す。
        commandList->BeginRenderPass(m_OverlayTargets.GetRenderPass(), m_OverlayTargets.GetFramebuffer());
        if (!prepared.empty())
        {
            commandList->SetViewport(context.GetActiveLocalViewport());
            commandList->SetScissor(context.GetActiveLocalScissor());
            commandList->SetPipeline(m_OverlayTargets.GetPipeline());
            for (const PreparedBatch &batch : prepared)
            {
                commandList->SetDescriptorSet(batch.DescriptorSet, 0);
                commandList->DrawInstanced(6, batch.InstanceCount, 0, batch.InstanceDataOffset);
            }
        }
        commandList->EndRenderPass();
        LogDrawCounts(static_cast<uint32_t>(prepared.size()), drawnInstances);
    }

    void ImpostorGBufferPass::LogDrawCounts(uint32_t batches, uint64_t instances)
    {
        if (batches != m_LoggedBatchCount || instances != m_LoggedInstanceCount)
        {
            m_LoggedBatchCount = batches;
            m_LoggedInstanceCount = instances;
            NORVES_LOG_INFO("ImpostorGBufferPass", "IMPOSTOR_GBUFFER_DRAW batches=%u instances=%llu",
                            batches, static_cast<unsigned long long>(instances));
        }
    }
} // namespace NorvesLib::Core::Rendering
