// 常駐プールの GPU の粒を TAA の前後で HDR の色へ重ねる。
#include "Rendering/GpuParticle/GpuParticleDrawPass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/RenderExtension/RenderExtensionHelpers.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t PoolBytes = kGpuParticlePoolCapacity * sizeof(GpuParticle);
        constexpr uint32_t DrawListBytes = kGpuParticleDrawListCapacity * sizeof(uint32_t);

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t index = 0; index < kGpuParticleDrawBindingCount; ++index)
            {
                RHI::DescriptorBinding binding;
                binding.binding = index;
                binding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
                binding.type = index == kGpuParticleDrawBindingConstants ? RHI::ResourceBindType::ConstantBuffer
                    : (index >= kGpuParticleDrawBindingSceneDepth && index <= kGpuParticleDrawBindingHeightFogSky
                        ? RHI::ResourceBindType::CombinedImageSampler : RHI::ResourceBindType::StructuredBuffer);
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        RHI::SamplerPtr MakeSampler(RHI::IDevice& device, RHI::FilterMode filter)
        {
            RHI::SamplerDesc desc;
            desc.filterMin = filter;
            desc.filterMag = filter;
            desc.filterMip = filter;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            return device.CreateSampler(desc);
        }

        bool IsDrawEmitter(const GpuParticleEmitterFrame& emitter, uint32_t drawPass)
        {
            const GpuEmitterParams& params = emitter.Params;
            return ((params.RenderFlags >> kGpuEmitterRenderPassShift) & kGpuEmitterRenderPassMask) == drawPass &&
                params.Capacity > 0 && emitter.PoolSlot < kGpuParticleMaxEmitters &&
                params.PoolOffset <= kGpuParticlePoolCapacity &&
                params.Capacity <= kGpuParticlePoolCapacity - params.PoolOffset;
        }

        void CopyNormalizedDirection(float (&destination)[4], float x, float y, float z,
                                     float fallbackX, float fallbackY, float fallbackZ)
        {
            const float lengthSquared = x * x + y * y + z * z;
            if (std::isfinite(lengthSquared) && lengthSquared > 1.0e-10f)
            {
                const float inverseLength = 1.0f / std::sqrt(lengthSquared);
                destination[0] = x * inverseLength;
                destination[1] = y * inverseLength;
                destination[2] = z * inverseLength;
            }
            else
            {
                destination[0] = fallbackX;
                destination[1] = fallbackY;
                destination[2] = fallbackZ;
            }
        }
    } // 無名名前空間

    GpuParticleDrawPass::GpuParticleDrawPass(uint32_t drawPass)
        : m_DrawPass(drawPass)
    {
    }

    GpuParticleDrawPass::~GpuParticleDrawPass()
    {
        Shutdown();
    }

    const char* GpuParticleDrawPass::GetName() const
    {
        return m_DrawPass == kGpuParticleDrawPassPreTAA ? kGpuParticleDrawPreTAAExtensionName
            : kGpuParticleDrawPostTAAExtensionName;
    }

    bool GpuParticleDrawPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        const auto fail = [&]()
        {
            if (!m_bReportedInitializationFailure)
            {
                NORVES_LOG_ERROR("GpuParticleDraw", "粒の描画に必要なシェーダーまたは資源を準備できませんでした");
                m_bReportedInitializationFailure = true;
            }
            Shutdown();
            return false;
        };
        if (!context.Device || !context.ShaderMgr)
        {
            return fail();
        }
        m_Device = context.Device;
        m_ReactiveTargetBinder.Initialize(m_Device);
        m_VertexShader = context.ShaderMgr->LoadShader("Particles/ParticleDraw.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("Particles/ParticleDraw.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            return fail();
        }
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(MakeDescriptorSetDesc());
        m_DepthSampler = MakeSampler(*m_Device, RHI::FilterMode::Point);
        m_LinearSampler = MakeSampler(*m_Device, RHI::FilterMode::Linear);
        RHI::TextureDesc textureDesc;
        textureDesc.Width = 1;
        textureDesc.Height = 1;
        textureDesc.TextureFormat = RHI::Format::R32G32B32A32_FLOAT;
        textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        textureDesc.DebugName = "GpuParticleDraw.Black";
        m_BlackTexture = m_Device->CreateTexture(textureDesc);
        if (!m_DescriptorSetLayout || !m_DepthSampler || !m_LinearSampler || !m_BlackTexture)
        {
            return fail();
        }
        const float black[4] = {};
        m_BlackTexture->Update(black, sizeof(black), sizeof(black));
        m_bInitialized = true;
        return true;
    }

    void GpuParticleDrawPass::ResetHandles()
    {
        m_PoolHandle = {};
        m_DrawListHandle = {};
        m_DrawArgsHandle = {};
        m_ColorHandle = {};
        m_DepthHandle = {};
        m_SkyHandle = {};
        m_bReactiveDeclared = false;
    }

    void GpuParticleDrawPass::Shutdown()
    {
        m_Framebuffer.reset();
        m_PremultipliedPipeline.reset();
        m_AdditivePipeline.reset();
        m_ReactivePipeline.reset();
        m_ReactiveRenderPass.reset();
        m_ReactiveTargetBinder.Reset();
        m_RenderPass.reset();
        m_BlackTexture.reset();
        m_DepthSampler.reset();
        m_LinearSampler.reset();
        m_DescriptorSetLayout.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ColorTarget = nullptr;
        m_TargetFormat = RHI::Format::UNKNOWN;
        ResetHandles();
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void GpuParticleDrawPass::Setup(ViewRenderContext&)
    {
    }

    void GpuParticleDrawPass::Execute(ViewRenderContext&)
    {
        // RenderGraph 経由でのみ描画する。
    }

    void GpuParticleDrawPass::Declare(RenderGraphBuilder& builder)
    {
        ResetHandles();
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bInitialized || !context || !context->SnapshotScene ||
            context->SnapshotScene->GpuParticles.IsEmpty() || !context->GetActiveCamera())
        {
            return;
        }
        bool bHasEmitters = false;
        bool bHasPremultipliedEmitters = false;
        for (const GpuParticleEmitterFrame& emitter : context->SnapshotScene->GpuParticles.Emitters)
        {
            if (IsDrawEmitter(emitter, m_DrawPass))
            {
                bHasEmitters = true;
                bHasPremultipliedEmitters = bHasPremultipliedEmitters ||
                    ((emitter.Params.RenderFlags >> kGpuEmitterRenderBlendShift) & kGpuEmitterRenderBlendMask) == 0u;
            }
        }
        if (!bHasEmitters || !builder.HasBuffer(kGpuParticlePoolResourceName) ||
            !builder.TryGetBuffer(kGpuParticlePoolResourceName, m_PoolHandle))
        {
            ResetHandles();
            return;
        }
        // 必須の画像の存在を先に確かめ、描けないフレームではアクセスを宣言しない。
        RGTextureHandle available;
        const Identity colorName = m_DrawPass == kGpuParticleDrawPassPostTAA ? ResolveHdrChainHead(builder)
            : (builder.TryGetTexture(RenderGraphResourceNames::SSRSceneColor, available)
                   ? RenderGraphResourceNames::SSRSceneColor : RenderGraphResourceNames::SceneColor);
        if (!colorName.IsValid() || !builder.TryGetTexture(colorName, available) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, available))
        {
            ResetHandles();
            return;
        }
        // グラフの入口と出口は UAV。頂点の読みは Execute 内で GenericRead に移してから戻す。
        builder.Read(m_PoolHandle.ToResourceHandle(), RHI::ResourceState::UnorderedAccess);
        if (builder.TryGetBuffer(kGpuParticleDrawListResourceName, m_DrawListHandle) &&
            builder.TryGetBuffer(kGpuParticleDrawArgsResourceName, m_DrawArgsHandle))
        {
            builder.Read(m_DrawListHandle.ToResourceHandle(), RHI::ResourceState::UnorderedAccess);
            builder.Read(m_DrawArgsHandle.ToResourceHandle(), RHI::ResourceState::UnorderedAccess);
        }
        else
        {
            m_DrawListHandle = {};
            m_DrawArgsHandle = {};
        }
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth, m_DepthHandle, RHI::ResourceState::ShaderResource) ||
            !builder.TryLoadStoreColorAttachment(colorName, m_ColorHandle, RHI::AttachmentLoadOp::Load,
                RHI::AttachmentStoreOp::Store, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource))
        {
            ResetHandles();
            return;
        }
        builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance, m_SkyHandle, RHI::ResourceState::ShaderResource);
        if (m_DrawPass == kGpuParticleDrawPassPreTAA && bHasPremultipliedEmitters)
        {
            m_bReactiveDeclared = m_ReactiveTargetBinder.Declare(builder, false);
            if (!m_bReactiveDeclared)
            {
                ReportReactiveFailure("描画先を宣言できませんでした");
            }
        }
        builder.PreserveInsertionOrder();
    }

    bool GpuParticleDrawPass::EnsureTarget(const RHI::TexturePtr& target)
    {
        if (!m_RenderPass || m_TargetFormat != target->GetFormat())
        {
            m_Framebuffer.reset();
            m_ColorTarget = nullptr;
            m_PremultipliedPipeline.reset();
            m_AdditivePipeline.reset();
            RHI::RenderPassDesc desc;
            RHI::AttachmentDesc color;
            color.format = target->GetFormat();
            color.clear = false;
            color.loadOp = RHI::AttachmentLoadOp::Load;
            color.storeOp = RHI::AttachmentStoreOp::Store;
            color.initialState = RHI::ResourceState::RenderTarget;
            color.finalState = RHI::ResourceState::ShaderResource;
            desc.colorAttachments.push_back(color);
            desc.hasDepthStencil = false;
            m_RenderPass = m_Device->CreateRenderPass(desc);
            m_TargetFormat = target->GetFormat();
        }
        if (!m_RenderPass)
        {
            return false;
        }
        if (m_ColorTarget != target.get() || !m_Framebuffer)
        {
            RHI::FramebufferDesc desc;
            desc.renderPass = m_RenderPass;
            desc.colorTargets.push_back(target);
            desc.width = target->GetWidth();
            desc.height = target->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(desc);
            m_ColorTarget = m_Framebuffer ? target.get() : nullptr;
        }
        const auto makePipeline = [&](bool bAdditive)
        {
            RHI::GraphicsPipelineDesc desc;
            desc.vertexShader = m_VertexShader;
            desc.pixelShader = m_FragmentShader;
            desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            desc.rasterState.cullMode = RHI::CullMode::None;
            desc.depthStencilState.depthTestEnable = false;
            desc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = true;
            blend.srcColorBlendFactor = RHI::BlendFactor::One;
            blend.dstColorBlendFactor = bAdditive ? RHI::BlendFactor::One : RHI::BlendFactor::InvSrcAlpha;
            blend.srcAlphaBlendFactor = bAdditive ? RHI::BlendFactor::Zero : RHI::BlendFactor::One;
            blend.dstAlphaBlendFactor = bAdditive ? RHI::BlendFactor::One : RHI::BlendFactor::InvSrcAlpha;
            desc.blendState.attachments.push_back(blend);
            desc.renderPass = m_RenderPass;
            desc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
            return m_Device->CreateGraphicsPipeline(desc);
        };
        if (!m_PremultipliedPipeline)
        {
            m_PremultipliedPipeline = makePipeline(false);
        }
        if (!m_AdditivePipeline)
        {
            m_AdditivePipeline = makePipeline(true);
        }
        return m_Framebuffer && m_PremultipliedPipeline && m_AdditivePipeline;
    }

    bool GpuParticleDrawPass::EnsureReactivePipeline()
    {
        const RHI::RenderPassPtr renderPass = m_ReactiveTargetBinder.GetRenderPass();
        if (m_ReactivePipeline && m_ReactiveRenderPass == renderPass)
        {
            return true;
        }
        m_ReactivePipeline.reset();
        m_ReactiveRenderPass.reset();
        if (!m_Device || !renderPass)
        {
            return false;
        }

        RHI::GraphicsPipelineDesc desc;
        desc.vertexShader = m_VertexShader;
        desc.pixelShader = m_FragmentShader;
        desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        desc.rasterState.cullMode = RHI::CullMode::None;
        desc.depthStencilState.depthTestEnable = false;
        desc.depthStencilState.depthWriteEnable = false;
        RHI::BlendAttachmentDesc blend;
        blend.blendEnable = true;
        blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blend.srcAlphaBlendFactor = RHI::BlendFactor::SrcAlpha;
        blend.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blend.colorWriteMask = RHI::ColorWriteMask::R;
        desc.blendState.attachments.push_back(blend);
        desc.renderPass = renderPass;
        desc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
        m_ReactivePipeline = m_Device->CreateGraphicsPipeline(desc);
        if (!m_ReactivePipeline)
        {
            return false;
        }
        m_ReactiveRenderPass = renderPass;
        return true;
    }

    void GpuParticleDrawPass::ReportReactiveFailure(const char* reason)
    {
        (void)reason;
        if (!m_bReportedReactiveFailure)
        {
            NORVES_LOG_ERROR("GpuParticleDraw", "粒のリアクティブ出力に失敗しました: %s", reason);
            m_bReportedReactiveFailure = true;
        }
    }

    void GpuParticleDrawPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        const bool bReactivePrepared = m_bReactiveDeclared && m_ReactiveTargetBinder.Prepare(resources);
        if (m_bReactiveDeclared && !bReactivePrepared)
        {
            ReportReactiveFailure("描画先を準備できませんでした");
        }
        if (!context.CommandList)
        {
            if (bReactivePrepared)
            {
                ReportReactiveFailure("コマンドリストがありません");
            }
            return;
        }
        RHI::ICommandList& commands = *context.CommandList;
        const auto finishReactiveWithoutDraw = [&]()
        {
            if (bReactivePrepared)
            {
                // 描画を見送っても、最初の書き手によるゼロクリアを実行する。
                commands.BeginRenderPass(m_ReactiveTargetBinder.GetRenderPass(), m_ReactiveTargetBinder.GetFramebuffer());
                commands.EndRenderPass();
            }
        };
        if (!m_PoolHandle.IsValid() || !m_ColorHandle.IsValid() || !m_DepthHandle.IsValid())
        {
            finishReactiveWithoutDraw();
            return;
        }
        const RHI::TexturePtr color = resources.GetTexture(m_ColorHandle);
        if (!color)
        {
            finishReactiveWithoutDraw();
            return;
        }
        const auto finishWithoutDraw = [&]()
        {
            commands.TextureBarrier(color, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
            finishReactiveWithoutDraw();
        };
        const RHI::BufferPtr pool = resources.GetBuffer(m_PoolHandle);
        const RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);
        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (!m_bInitialized || !context.FrameResources || !context.FrameResources->IsRecording() ||
            !context.SnapshotScene || !activeCamera || !pool || pool->GetSize() < PoolBytes || !depth ||
            depth->GetWidth() == 0 || depth->GetHeight() == 0 || color->GetWidth() == 0 || color->GetHeight() == 0 ||
            color->GetWidth() > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
            color->GetHeight() > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) || !EnsureTarget(color))
        {
            finishWithoutDraw();
            return;
        }

        const GpuParticleScene& scene = context.SnapshotScene->GpuParticles;
        Container::VariableArray<GpuEmitterParams> emitters;
        emitters.reserve(scene.Emitters.size());
        bool bHasBlendEmitters[2] = {};
        bool bHasCompactEmitters[2] = {};
        uint32_t slotToEmitter[kGpuParticleMaxEmitters];
        for (uint32_t& index : slotToEmitter)
        {
            index = kGpuParticleInvalidEmitter;
        }
        for (const GpuParticleEmitterFrame& emitter : scene.Emitters)
        {
            if (IsDrawEmitter(emitter, m_DrawPass))
            {
                slotToEmitter[emitter.PoolSlot] = static_cast<uint32_t>(emitters.size());
                emitters.push_back(emitter.Params);
                const uint32_t blend = (emitter.Params.RenderFlags >> kGpuEmitterRenderBlendShift) & kGpuEmitterRenderBlendMask;
                const uint32_t slotOrder = (emitter.Params.RenderFlags >> kGpuEmitterRenderSlotOrderShift) & kGpuEmitterRenderSlotOrderMask;
                bHasBlendEmitters[blend] = true;
                bHasCompactEmitters[blend] = bHasCompactEmitters[blend] || slotOrder == 0u;
            }
        }
        if (emitters.empty())
        {
            finishWithoutDraw();
            return;
        }
        const RHI::BufferPtr drawList = m_DrawListHandle.IsValid() ? resources.GetBuffer(m_DrawListHandle) : nullptr;
        const RHI::BufferPtr drawArgs = m_DrawArgsHandle.IsValid() ? resources.GetBuffer(m_DrawArgsHandle) : nullptr;
        const bool bIndirectResourcesAvailable = drawList && drawArgs &&
            drawList->GetSize() >= DrawListBytes && drawArgs->GetSize() >= sizeof(GpuParticleDrawArgs);
        if (!bIndirectResourcesAvailable && !m_bReportedCompactionUnavailable)
        {
            NORVES_LOG_WARNING("GpuParticleDraw", "粒の詰めの資源が無いため、直描画を使います");
            m_bReportedCompactionUnavailable = true;
        }
        bool bUseIndirect = bIndirectResourcesAvailable;
        FrameResourceLane& lane = *context.FrameResources;
        const uint32_t zeroSlot = 0u;
        FrameUploadAllocation dummyListData;
        if (!bIndirectResourcesAvailable)
        {
            dummyListData = lane.AllocateUploadData(&zeroSlot, sizeof(zeroSlot), FrameUploadUsage::Storage);
            if (!dummyListData.IsValid())
            {
                finishWithoutDraw();
                return;
            }
        }
        const RHI::BufferPtr descriptorList = bIndirectResourcesAvailable ? drawList : dummyListData.Buffer;
        const uint32_t descriptorListOffset = bIndirectResourcesAvailable ? 0u : dummyListData.GetOffset32();
        const uint32_t descriptorListSize = bIndirectResourcesAvailable ? DrawListBytes : dummyListData.GetSize32();
        const GpuCurveKey zeroKey{};
        const FrameUploadAllocation emitterData = lane.AllocateUploadData(
            emitters.data(), emitters.size() * sizeof(GpuEmitterParams), FrameUploadUsage::Storage);
        const FrameUploadAllocation slotData = lane.AllocateUploadData(slotToEmitter, sizeof(slotToEmitter), FrameUploadUsage::Storage);
        const FrameUploadAllocation curveData = lane.AllocateUploadData(
            scene.CurveKeys.empty() ? &zeroKey : scene.CurveKeys.data(),
            scene.CurveKeys.empty() ? sizeof(zeroKey) : scene.CurveKeys.size() * sizeof(GpuCurveKey), FrameUploadUsage::Storage);

        CameraProxy camera = *activeCamera;
        if (m_DrawPass == kGpuParticleDrawPassPostTAA)
        {
            camera.ProjectionJitterNdcX = 0.0f;
            camera.ProjectionJitterNdcY = 0.0f;
        }
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(camera, context.GetActiveAspectRatio(), context.Device);
        GpuParticleDrawConstants constants{};
        cameraConstants.CopyShaderView(constants.View);
        cameraConstants.CopyShaderProjection(constants.Projection);
        constants.CameraPositionPreExposure[0] = camera.PositionX;
        constants.CameraPositionPreExposure[1] = camera.PositionY;
        constants.CameraPositionPreExposure[2] = camera.PositionZ;
        constants.CameraPositionPreExposure[3] = ResolveSceneColorPreExposure(&camera);
        CopyNormalizedDirection(constants.CameraRight, camera.RightX, camera.RightY, camera.RightZ, 1.0f, 0.0f, 0.0f);
        CopyNormalizedDirection(constants.CameraUp, camera.UpX, camera.UpY, camera.UpZ, 0.0f, 1.0f, 0.0f);
        CopyNormalizedDirection(constants.CameraForward, camera.ForwardX, camera.ForwardY, camera.ForwardZ, 0.0f, 0.0f, -1.0f);
        if (const CameraProxy* previous = context.GetPreviousCamera(); previous && std::isfinite(scene.DeltaTime) && scene.DeltaTime > 1.0e-6f)
        {
            const float velocity[3] = {(camera.PositionX - previous->PositionX) / scene.DeltaTime,
                (camera.PositionY - previous->PositionY) / scene.DeltaTime,
                (camera.PositionZ - previous->PositionZ) / scene.DeltaTime};
            if (std::isfinite(velocity[0]) && std::isfinite(velocity[1]) && std::isfinite(velocity[2]))
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    constants.CameraVelocity[axis] = velocity[axis];
                }
            }
        }
        constants.TargetSizeInvSize[0] = static_cast<float>(color->GetWidth());
        constants.TargetSizeInvSize[1] = static_cast<float>(color->GetHeight());
        constants.TargetSizeInvSize[2] = 1.0f / constants.TargetSizeInvSize[0];
        constants.TargetSizeInvSize[3] = 1.0f / constants.TargetSizeInvSize[1];
        constants.NearFarParams[0] = camera.NearPlane;
        constants.NearFarParams[1] = camera.FarPlane;
        constants.NearFarParams[2] = camera.NearPlane / camera.FarPlane;
        constants.AmbientColorIblIntensity[0] = context.SnapshotScene->AmbientColorR * context.SnapshotScene->AmbientIntensity;
        constants.AmbientColorIblIntensity[1] = context.SnapshotScene->AmbientColorG * context.SnapshotScene->AmbientIntensity;
        constants.AmbientColorIblIntensity[2] = context.SnapshotScene->AmbientColorB * context.SnapshotScene->AmbientIntensity;
        const PhysicalLightingResources& published = context.PhysicalLighting;
        const bool bValidViewport = context.CurrentViewport && context.CurrentViewport->HasDrawableExtent() &&
            context.CurrentViewport->ViewId != UINT32_MAX && context.CurrentViewport->ViewportId != UINT32_MAX;
        const bool bLightingAvailable = bValidViewport &&
            published.Matches(context.FrameNumber, context.CurrentViewport->ViewId, context.CurrentViewport->ViewportId) &&
            published.bLightingPublished && published.bIBLEnabled &&
            published.DiffuseIrradianceTexture && published.DiffuseIrradianceSampler &&
            std::isfinite(published.IBLIntensity) && published.IBLIntensity >= 0.0f;
        constants.AmbientColorIblIntensity[3] = bLightingAvailable ? published.IBLIntensity : 0.0f;
        constants.Flags[0] = m_DrawPass;
        constants.Flags[1] = bLightingAvailable ? 1u : 0u;
        constants.Flags[2] = kGpuParticleDrawOutputColor;
        constants.Flags[3] = kGpuParticleDrawSourceDirect;
        const RHI::TexturePtr sky = m_SkyHandle.IsValid() ? resources.GetTexture(m_SkyHandle) : nullptr;
        constants.HeightFog = BuildHeightFogGpuParams(context, camera, static_cast<bool>(sky));
        if (!emitterData.IsValid() || !slotData.IsValid() || !curveData.IsValid())
        {
            finishWithoutDraw();
            return;
        }
        const auto makeDescriptor = [&](const GpuParticleDrawConstants& setConstants) -> FrameDescriptorAllocation
        {
            const FrameUploadAllocation uniform = lane.AllocateConstants(setConstants);
            const FrameDescriptorAllocation descriptor = lane.AllocateDescriptorSet(m_DescriptorSetLayout);
            if (!uniform.IsValid() || !descriptor.IsValid())
            {
                return {};
            }
            RHI::IDescriptorSet& set = *descriptor.Set;
            set.BindConstantBuffer(kGpuParticleDrawBindingConstants, uniform.Buffer, uniform.GetOffset32(), uniform.GetSize32());
            set.BindStorageBuffer(kGpuParticleDrawBindingPool, pool, 0, PoolBytes);
            set.BindStorageBuffer(kGpuParticleDrawBindingEmitters, emitterData.Buffer, emitterData.GetOffset32(), emitterData.GetSize32());
            set.BindStorageBuffer(kGpuParticleDrawBindingSlotToEmitter, slotData.Buffer, slotData.GetOffset32(), slotData.GetSize32());
            set.BindStorageBuffer(kGpuParticleDrawBindingCurveKeys, curveData.Buffer, curveData.GetOffset32(), curveData.GetSize32());
            set.BindTexture(kGpuParticleDrawBindingSceneDepth, depth);
            set.BindSampler(kGpuParticleDrawBindingSceneDepth, m_DepthSampler);
            set.BindTexture(kGpuParticleDrawBindingDiffuseIrradiance, bLightingAvailable ? published.DiffuseIrradianceTexture : m_BlackTexture);
            set.BindSampler(kGpuParticleDrawBindingDiffuseIrradiance, m_LinearSampler);
            set.BindTexture(kGpuParticleDrawBindingHeightFogSky, sky ? sky : m_BlackTexture);
            set.BindSampler(kGpuParticleDrawBindingHeightFogSky, m_LinearSampler);
            set.BindStorageBuffer(kGpuParticleDrawBindingDrawList, descriptorList, descriptorListOffset, descriptorListSize);
            set.Update();
            return descriptor;
        };
        const FrameDescriptorAllocation descriptor = makeDescriptor(constants);
        if (!descriptor.IsValid())
        {
            finishWithoutDraw();
            return;
        }

        FrameDescriptorAllocation reactiveDescriptor;
        GpuParticleDrawConstants reactiveConstants = constants;
        reactiveConstants.Flags[2] = kGpuParticleDrawOutputReactive;
        if (bReactivePrepared)
        {
            if (EnsureReactivePipeline())
            {
                reactiveDescriptor = makeDescriptor(reactiveConstants);
                if (!reactiveDescriptor.IsValid())
                {
                    ReportReactiveFailure("定数または記述子を確保できませんでした");
                }
            }
            else
            {
                ReportReactiveFailure("パイプラインを作成できませんでした");
            }
        }

        FrameDescriptorAllocation compactDescriptors[2];
        FrameDescriptorAllocation reactiveCompactDescriptor;
        if (bUseIndirect)
        {
            for (uint32_t blend = 0; blend < 2; ++blend)
            {
                if (bHasCompactEmitters[blend])
                {
                    GpuParticleDrawConstants compactConstants = constants;
                    compactConstants.Flags[3] = GpuParticleDrawSourceList(GpuParticleDrawBucket(m_DrawPass, blend));
                    compactDescriptors[blend] = makeDescriptor(compactConstants);
                    if (!compactDescriptors[blend].IsValid())
                    {
                        bUseIndirect = false;
                        break;
                    }
                }
            }
            if (bUseIndirect && reactiveDescriptor.IsValid() && bHasCompactEmitters[0])
            {
                reactiveConstants.Flags[3] = GpuParticleDrawSourceList(GpuParticleDrawBucket(m_DrawPass, 0u));
                reactiveCompactDescriptor = makeDescriptor(reactiveConstants);
                bUseIndirect = reactiveCompactDescriptor.IsValid();
            }
            if (!bUseIndirect && !m_bReportedIndirectPreparationFailure)
            {
                NORVES_LOG_WARNING("GpuParticleDraw", "粒の間接描画の定数または記述子を確保できないため、直描画を使います");
                m_bReportedIndirectPreparationFailure = true;
            }
        }

        // ShaderResource の同期段には頂点が無いので、頂点の storage 読みを含む GenericRead へ明示的に移す。
        commands.BufferBarrier(pool, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        if (bIndirectResourcesAvailable)
        {
            commands.BufferBarrier(drawArgs, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndirectArgument);
            commands.BufferBarrier(drawList, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        }
        commands.BeginRenderPass(m_RenderPass, m_Framebuffer);
        RHI::Viewport viewport;
        viewport.width = constants.TargetSizeInvSize[0];
        viewport.height = constants.TargetSizeInvSize[1];
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(color->GetWidth());
        scissor.bottom = static_cast<int32_t>(color->GetHeight());
        commands.SetViewport(viewport);
        commands.SetScissor(scissor);
        // フレームの写しにテクスチャが無いためテクスチャとフリップブック UV は使わず手続きの形だけを描き、粒の深度順は並べ替えない。
        // 詰めは atomic の順なので描く順が毎フレーム変わりうる。重なりの順に依る premultiplied のエミッタは DrawInSlotOrder を選ぶ。
        const auto drawBlend = [&](uint32_t blend, const FrameDescriptorAllocation& directSet,
                                   const FrameDescriptorAllocation& compactSet)
        {
            bool bDrawCompactDirect = bHasCompactEmitters[blend];
            if (bDrawCompactDirect && bUseIndirect)
            {
                const uint32_t bucket = GpuParticleDrawBucket(m_DrawPass, blend);
                const uint64_t commandOffset = offsetof(GpuParticleDrawArgs, Commands) +
                    static_cast<uint64_t>(bucket) * sizeof(RHI::DrawIndirectCommand);
                const uint64_t countOffset = offsetof(GpuParticleDrawArgs, DrawCounts) +
                    static_cast<uint64_t>(bucket) * sizeof(uint32_t);
                constexpr uint32_t stride = sizeof(RHI::DrawIndirectCommand);
                commands.SetDescriptorSet(compactSet.Set, 0);
                const bool bRecorded = m_Device->GetCapabilities().bDrawIndirectCount
                    ? commands.DrawIndirectCount(drawArgs, commandOffset, drawArgs, countOffset, 1u, stride)
                    : commands.DrawIndirect(drawArgs, commandOffset, 1u, stride);
                if (bRecorded)
                {
                    bDrawCompactDirect = false;
                }
                else
                {
                    if (!m_bReportedIndirectFailure)
                    {
                        NORVES_LOG_WARNING("GpuParticleDraw", "粒の間接描画を記録できないため、このフレームの残りは直描画を使います");
                        m_bReportedIndirectFailure = true;
                    }
                    bUseIndirect = false;
                }
            }

            bool bDirectBound = false;
            for (uint32_t slotOrder = 0; slotOrder < 2; ++slotOrder)
            {
                if (slotOrder == 0u && !bDrawCompactDirect)
                {
                    continue;
                }
                for (const GpuEmitterParams& emitter : emitters)
                {
                    const uint32_t emitterBlend = (emitter.RenderFlags >> kGpuEmitterRenderBlendShift) & kGpuEmitterRenderBlendMask;
                    const uint32_t emitterSlotOrder = (emitter.RenderFlags >> kGpuEmitterRenderSlotOrderShift) & kGpuEmitterRenderSlotOrderMask;
                    if (emitterBlend != blend || emitterSlotOrder != slotOrder)
                    {
                        continue;
                    }
                    if (!bDirectBound)
                    {
                        commands.SetDescriptorSet(directSet.Set, 0);
                        bDirectBound = true;
                    }
                    commands.DrawInstanced(6, emitter.Capacity, 0, emitter.PoolOffset);
                }
            }
        };
        for (uint32_t blend = 0; blend < 2; ++blend)
        {
            if (bHasBlendEmitters[blend])
            {
                commands.SetPipeline(blend == 0 ? m_PremultipliedPipeline : m_AdditivePipeline);
                drawBlend(blend, descriptor, compactDescriptors[blend]);
            }
        }
        commands.EndRenderPass();
        if (bReactivePrepared)
        {
            commands.BeginRenderPass(m_ReactiveTargetBinder.GetRenderPass(), m_ReactiveTargetBinder.GetFramebuffer());
            const RHI::TexturePtr reactive = m_ReactiveTargetBinder.GetTexture();
            RHI::Viewport reactiveViewport;
            reactiveViewport.width = static_cast<float>(reactive->GetWidth());
            reactiveViewport.height = static_cast<float>(reactive->GetHeight());
            RHI::ScissorRect reactiveScissor;
            reactiveScissor.right = static_cast<int32_t>(reactive->GetWidth());
            reactiveScissor.bottom = static_cast<int32_t>(reactive->GetHeight());
            commands.SetViewport(reactiveViewport);
            commands.SetScissor(reactiveScissor);
            if (m_ReactivePipeline && reactiveDescriptor.IsValid())
            {
                commands.SetPipeline(m_ReactivePipeline);
                drawBlend(0u, reactiveDescriptor, reactiveCompactDescriptor);
            }
            commands.EndRenderPass();
        }
        // インポートの最終状態はグラフが自動では戻さないため、次の描画段と次フレームの入口へ戻す。
        commands.BufferBarrier(pool, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        if (bIndirectResourcesAvailable)
        {
            commands.BufferBarrier(drawArgs, RHI::ResourceState::IndirectArgument, RHI::ResourceState::UnorderedAccess);
            commands.BufferBarrier(drawList, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        }
    }

    Container::TUniquePtr<IViewPass> CreateGpuParticleDrawPassPreTAA(void*)
    {
        return Container::MakeUnique<GpuParticleDrawPass>(kGpuParticleDrawPassPreTAA);
    }

    Container::TUniquePtr<IViewPass> CreateGpuParticleDrawPassPostTAA(void*)
    {
        return Container::MakeUnique<GpuParticleDrawPass>(kGpuParticleDrawPassPostTAA);
    }

    bool RegisterGpuParticleDrawExtensions(RenderExtensionRegistry& registry)
    {
        RenderExtensionDesc desc;
        desc.Name = kGpuParticleDrawPreTAAExtensionName;
        desc.Stage = RenderExtensionStage::Transparent;
        desc.Order = 0;
        const bool bPreRegistered = registry.Register(desc, {CreateGpuParticleDrawPassPreTAA, nullptr});
        desc.Name = kGpuParticleDrawPostTAAExtensionName;
        desc.Stage = RenderExtensionStage::PostTemporalAA;
        const bool bPostRegistered = registry.Register(desc, {CreateGpuParticleDrawPassPostTAA, nullptr});
        return bPreRegistered && bPostRegistered;
    }
} // 名前空間 NorvesLib::Core::Rendering
