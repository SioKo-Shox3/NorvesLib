#include "Rendering/DistortionPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DistortionTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // distortion_offset の std140 と同じ並び。ベクトルの w と未使用の時刻成分はゼロ。
        struct DistortionUniform
        {
            float ViewProjection[16];
            float CameraRight[4];
            float CameraUp[4];
            float Params[4];
        };
        static_assert(sizeof(DistortionUniform) == 112);
        static_assert(sizeof(DistortionInstance) == 32);

        RHI::DescriptorSetDesc CreateDistortionLayout(uint32_t textureCount, bool bOffsets = false)
        {
            RHI::DescriptorSetDesc desc;
            if (bOffsets)
            {
                RHI::DescriptorBinding constants;
                constants.binding = 0;
                constants.type = RHI::ResourceBindType::ConstantBuffer;
                constants.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
                desc.bindings.push_back(constants);
            }
            for (uint32_t index = 0; index < textureCount; ++index)
            {
                RHI::DescriptorBinding texture;
                texture.binding = index + (bOffsets ? 1u : 0u);
                texture.type = RHI::ResourceBindType::CombinedImageSampler;
                texture.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(texture);
            }
            return desc;
        }

        RHI::SamplerPtr CreateDistortionSampler(RHI::IDevice& device, RHI::FilterMode filter)
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

        RHI::RenderPassPtr CreateDistortionRenderPass(RHI::IDevice& device, RHI::Format format, bool bClear)
        {
            RHI::RenderPassDesc desc;
            RHI::AttachmentDesc color;
            color.format = format;
            color.clear = bClear;
            for (float& component : color.clearColor)
            {
                component = 0.0f;
            }
            color.loadOp = bClear ? RHI::AttachmentLoadOp::Clear : RHI::AttachmentLoadOp::DontCare;
            color.storeOp = RHI::AttachmentStoreOp::Store;
            color.initialState = RHI::ResourceState::RenderTarget;
            color.finalState = RHI::ResourceState::ShaderResource;
            desc.colorAttachments.push_back(color);
            desc.hasDepthStencil = false;
            return device.CreateRenderPass(desc);
        }

        RHI::FramebufferPtr CreateDistortionFramebuffer(RHI::IDevice& device, const RHI::RenderPassPtr& pass,
                                                       const RHI::TexturePtr& target)
        {
            RHI::FramebufferDesc desc;
            desc.renderPass = pass;
            desc.colorTargets.push_back(target);
            desc.width = target->GetWidth();
            desc.height = target->GetHeight();
            return device.CreateFramebuffer(desc);
        }

        RHI::PipelinePtr CreateDistortionPipeline(RHI::IDevice& device, const RHI::ShaderPtr& vertex,
                                                 const RHI::ShaderPtr& pixel, const RHI::RenderPassPtr& pass,
                                                 const RHI::DescriptorSetDesc& layout, bool bOffsets = false)
        {
            RHI::GraphicsPipelineDesc desc;
            desc.vertexShader = vertex;
            desc.pixelShader = pixel;
            desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            desc.rasterState.cullMode = RHI::CullMode::None;
            desc.depthStencilState.depthTestEnable = false;
            desc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = bOffsets;
            blend.srcColorBlendFactor = RHI::BlendFactor::One;
            blend.dstColorBlendFactor = RHI::BlendFactor::One;
            blend.srcAlphaBlendFactor = RHI::BlendFactor::One;
            blend.dstAlphaBlendFactor = RHI::BlendFactor::One;
            if (bOffsets)
            {
                blend.colorWriteMask = RHI::ColorWriteMask::R | RHI::ColorWriteMask::G;
                RHI::VertexBindingDesc binding;
                binding.binding = 0;
                binding.stride = sizeof(DistortionInstance);
                binding.inputRate = RHI::VertexInputRate::Instance;
                desc.vertexBindings.push_back(binding);
                const uint32_t offsets[] = {
                    offsetof(DistortionInstance, Position), offsetof(DistortionInstance, Radius),
                    offsetof(DistortionInstance, Strength), offsetof(DistortionInstance, PatternScale),
                    offsetof(DistortionInstance, ScrollSpeed), offsetof(DistortionInstance, Seed)};
                for (uint32_t index = 0; index < 6; ++index)
                {
                    RHI::VertexAttributeDesc attribute;
                    attribute.location = index;
                    attribute.binding = 0;
                    attribute.offset = offsets[index];
                    attribute.format = index == 0 ? RHI::Format::R32G32B32_FLOAT
                                                 : (index == 5 ? RHI::Format::R32_UINT : RHI::Format::R32_FLOAT);
                    desc.vertexAttributes.push_back(attribute);
                }
            }
            desc.blendState.attachments.push_back(blend);
            desc.renderPass = pass;
            desc.descriptorSetLayouts.push_back(layout);
            return device.CreateGraphicsPipeline(desc);
        }

        bool AreDistortionTargetsValid(const RHI::TexturePtr& target, const RHI::TexturePtr& source)
        {
            return target && source && target.get() != source.get() && target->GetWidth() != 0 &&
                   target->GetHeight() != 0 && target->GetWidth() == source->GetWidth() &&
                   target->GetHeight() == source->GetHeight() &&
                   target->GetWidth() <= static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) &&
                   target->GetHeight() <= static_cast<uint32_t>((std::numeric_limits<int32_t>::max)());
        }

        void BeginDistortionDraw(RHI::ICommandList& commands, const RHI::RenderPassPtr& pass,
                                 const RHI::FramebufferPtr& framebuffer, const RHI::PipelinePtr& pipeline,
                                 const RHI::DescriptorSetPtr& descriptor, const RHI::TexturePtr& target)
        {
            RHI::Viewport viewport;
            viewport.width = static_cast<float>(target->GetWidth());
            viewport.height = static_cast<float>(target->GetHeight());
            RHI::ScissorRect scissor;
            scissor.right = static_cast<int32_t>(target->GetWidth());
            scissor.bottom = static_cast<int32_t>(target->GetHeight());
            commands.BeginRenderPass(pass, framebuffer);
            commands.SetViewport(viewport);
            commands.SetScissor(scissor);
            commands.SetPipeline(pipeline);
            commands.SetDescriptorSet(descriptor, 0);
        }
    } // namespace

    DistortionPass::~DistortionPass()
    {
        Shutdown();
    }

    bool DistortionPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("DistortionPass", "Device か ShaderManager がありません");
            return false;
        }
        m_Device = context.Device;
        m_OffsetVertexShader = context.ShaderMgr->LoadShader("distortion_offset.vert", RHI::ShaderStage::Vertex);
        m_OffsetFragmentShader = context.ShaderMgr->LoadShader("distortion_offset.frag", RHI::ShaderStage::Pixel);
        m_FullscreenVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_GatherShader = context.ShaderMgr->LoadShader("distortion_gather.frag", RHI::ShaderStage::Pixel);
        m_CopyShader = context.ShaderMgr->LoadShader("distortion_copy.frag", RHI::ShaderStage::Pixel);
        m_PointSampler = CreateDistortionSampler(*m_Device, RHI::FilterMode::Point);
        m_LinearSampler = CreateDistortionSampler(*m_Device, RHI::FilterMode::Linear);
        m_OffsetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateDistortionLayout(1, true));
        m_GatherLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateDistortionLayout(2));
        m_CopyLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateDistortionLayout(1));
        if (!m_OffsetVertexShader || !m_OffsetFragmentShader || !m_FullscreenVertexShader || !m_GatherShader ||
            !m_CopyShader || !m_PointSampler || !m_LinearSampler || !m_OffsetLayout || !m_GatherLayout || !m_CopyLayout)
        {
            NORVES_LOG_ERROR("DistortionPass", "DISTORTION_INIT_FAILED シェーダーか記述子の準備に失敗しました");
            Shutdown();
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void DistortionPass::Shutdown()
    {
        m_OffsetFramebuffer.reset();
        m_GatherFramebuffer.reset();
        m_CopyFramebuffer.reset();
        m_OffsetPipeline.reset();
        m_GatherPipeline.reset();
        m_CopyPipeline.reset();
        m_OffsetRenderPass.reset();
        m_ColorRenderPass.reset();
        m_OffsetTexture.reset();
        m_GatherTexture.reset();
        m_OffsetLayout.reset();
        m_GatherLayout.reset();
        m_CopyLayout.reset();
        m_PointSampler.reset();
        m_LinearSampler.reset();
        m_OffsetVertexShader.reset();
        m_OffsetFragmentShader.reset();
        m_FullscreenVertexShader.reset();
        m_GatherShader.reset();
        m_CopyShader.reset();
        m_OffsetTarget = nullptr;
        m_ColorTarget = nullptr;
        m_SceneColorHandle = {};
        m_DepthHandle = {};
        m_LoggedInstanceCount = 0;
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void DistortionPass::Setup(ViewRenderContext& context)
    {
        (void)context;
    }

    void DistortionPass::Execute(ViewRenderContext& context)
    {
        (void)context;
    }

    void DistortionPass::LogInstanceCount(uint32_t count)
    {
        if (m_LoggedInstanceCount != count)
        {
            m_LoggedInstanceCount = count;
            NORVES_LOG_INFO("DistortionPass", "DISTORTION_DRAW instances=%u", count);
        }
    }

    void DistortionPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_DepthHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!context || !context->SnapshotDistortions || context->SnapshotDistortions->IsEmpty() ||
            !context->GetActiveCamera())
        {
            if (context && (!context->SnapshotDistortions || context->SnapshotDistortions->IsEmpty()))
            {
                LogInstanceCount(0);
            }
            return;
        }
        // 両方が存在するときだけ読み書きを宣言する。
        RGTextureHandle available;
        if (!builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, available))
        {
            return;
        }
        const Identity colorName = builder.TryGetTexture(RenderGraphResourceNames::SSRSceneColor, available)
                                       ? RenderGraphResourceNames::SSRSceneColor : RenderGraphResourceNames::SceneColor;
        if (!builder.TryGetTexture(colorName, available))
        {
            return;
        }
        RGTextureHandle depth;
        RGTextureHandle color;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth, depth, RHI::ResourceState::ShaderResource) ||
            !builder.TryLoadStoreColorAttachment(colorName, color, RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store, RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource))
        {
            return;
        }
        m_DepthHandle = depth.ToResourceHandle();
        m_SceneColorHandle = color.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void DistortionPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_SceneColorHandle.IsValid() || !m_DepthHandle.IsValid() || !context.CommandList)
        {
            return;
        }
        const RHI::TexturePtr color = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);
        if (!color)
        {
            return;
        }
        const auto finishWithoutComposite = [&]()
        {
            context.CommandList->TextureBarrier(color, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
        };
        const CameraProxy* camera = context.GetActiveCamera();
        if (!context.FrameResources || !context.SnapshotDistortions || context.SnapshotDistortions->IsEmpty() ||
            !camera || !AreDistortionTargetsValid(color, depth) || (!m_bInitialized && !Initialize(context)))
        {
            finishWithoutComposite();
            return;
        }
        if (!m_OffsetTexture || m_OffsetTexture->GetWidth() != color->GetWidth() ||
            m_OffsetTexture->GetHeight() != color->GetHeight())
        {
            m_OffsetFramebuffer.reset();
            m_OffsetTarget = nullptr;
            m_OffsetTexture = m_Device->CreateTexture(RHI::TextureDesc::RenderTarget(
                color->GetWidth(), color->GetHeight(), RHI::Format::R16G16_FLOAT, "Distortion.Offsets"));
        }
        if (!m_OffsetTexture)
        {
            finishWithoutComposite();
            return;
        }
        const CameraViewConstants constants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        context.CommandList->TextureBarrier(m_OffsetTexture, RHI::ResourceState::Undefined, RHI::ResourceState::RenderTarget);
        if (!RecordOffsets(m_OffsetTexture, depth, *context.CommandList, *context.FrameResources,
                           *context.SnapshotDistortions, constants, context.TotalTime) ||
            !RecordComposite(color, m_OffsetTexture, *context.CommandList, *context.FrameResources))
        {
            finishWithoutComposite();
        }
    }

    bool DistortionPass::RecordOffsets(const RHI::TexturePtr& offsetTarget, const RHI::TexturePtr& depthTexture,
                                       RHI::ICommandList& commandList, FrameResourceLane& frameResources,
                                       const DistortionScene& scene, const CameraViewConstants& cameraConstants,
                                       double totalTime)
    {
        if (!m_bInitialized || !frameResources.IsRecording() || !AreDistortionTargetsValid(offsetTarget, depthTexture) ||
            offsetTarget->GetFormat() != RHI::Format::R16G16_FLOAT ||
            (!scene.IsEmpty() && scene.Distortions->size() > (std::numeric_limits<uint32_t>::max)()) ||
            !EnsureOffsetTarget(offsetTarget))
        {
            return false;
        }
        FrameUploadAllocation instances;
        FrameDescriptorAllocation descriptor;
        const uint32_t count = scene.IsEmpty() ? 0u : static_cast<uint32_t>(scene.Distortions->size());
        if (count != 0)
        {
            DistortionUniform uniform{};
            cameraConstants.CopyShaderViewProjection(uniform.ViewProjection);
            float inverseView[16];
            cameraConstants.CopyShaderInverseView(inverseView);
            // 逆ビューの最初の二列は、ワールド空間の右と上の単位ベクトル。
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                uniform.CameraRight[axis] = inverseView[axis];
                uniform.CameraUp[axis] = inverseView[4 + axis];
            }
            uniform.Params[0] = static_cast<float>(std::fmod(totalTime, 1024.0));
            const FrameUploadAllocation constants = frameResources.AllocateConstants(uniform);
            instances = frameResources.AllocateUploadData(scene.Distortions->data(),
                static_cast<uint64_t>(count) * sizeof(DistortionInstance), FrameUploadUsage::Vertex);
            descriptor = frameResources.AllocateDescriptorSet(m_OffsetLayout);
            if (!constants.IsValid() || !instances.IsValid() || !descriptor.IsValid())
            {
                return false;
            }
            descriptor.Set->BindConstantBuffer(0, constants.Buffer, constants.GetOffset32(), constants.GetSize32());
            descriptor.Set->BindTexture(1, depthTexture);
            descriptor.Set->BindSampler(1, m_PointSampler);
            descriptor.Set->Update();
        }
        if (count != 0)
        {
            BeginDistortionDraw(commandList, m_OffsetRenderPass, m_OffsetFramebuffer, m_OffsetPipeline,
                                descriptor.Set, offsetTarget);
            commandList.SetVertexBuffer(instances.Buffer, instances.Offset, 0);
            commandList.DrawInstanced(6, count);
        }
        else
        {
            commandList.BeginRenderPass(m_OffsetRenderPass, m_OffsetFramebuffer);
        }
        commandList.EndRenderPass();
        LogInstanceCount(count);
        return true;
    }

    bool DistortionPass::RecordComposite(const RHI::TexturePtr& sceneColorTarget, const RHI::TexturePtr& offsetTexture,
                                         RHI::ICommandList& commandList, FrameResourceLane& frameResources)
    {
        if (!m_bInitialized || !frameResources.IsRecording() || !AreDistortionTargetsValid(sceneColorTarget, offsetTexture) ||
            offsetTexture->GetFormat() != RHI::Format::R16G16_FLOAT || !EnsureCompositeTarget(sceneColorTarget))
        {
            return false;
        }
        const FrameDescriptorAllocation gather = frameResources.AllocateDescriptorSet(m_GatherLayout);
        const FrameDescriptorAllocation copy = frameResources.AllocateDescriptorSet(m_CopyLayout);
        if (!gather.IsValid() || !copy.IsValid())
        {
            return false;
        }
        gather.Set->BindTexture(0, sceneColorTarget);
        gather.Set->BindSampler(0, m_LinearSampler);
        gather.Set->BindTexture(1, offsetTexture);
        gather.Set->BindSampler(1, m_PointSampler);
        gather.Set->Update();
        copy.Set->BindTexture(0, m_GatherTexture);
        copy.Set->BindSampler(0, m_PointSampler);
        copy.Set->Update();

        commandList.TextureBarrier(sceneColorTarget, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
        commandList.TextureBarrier(m_GatherTexture, RHI::ResourceState::Undefined, RHI::ResourceState::RenderTarget);
        BeginDistortionDraw(commandList, m_ColorRenderPass, m_GatherFramebuffer, m_GatherPipeline, gather.Set, m_GatherTexture);
        commandList.Draw(3);
        commandList.EndRenderPass();
        commandList.TextureBarrier(sceneColorTarget, RHI::ResourceState::ShaderResource, RHI::ResourceState::RenderTarget);
        BeginDistortionDraw(commandList, m_ColorRenderPass, m_CopyFramebuffer, m_CopyPipeline, copy.Set, sceneColorTarget);
        commandList.Draw(3);
        commandList.EndRenderPass();
        return true;
    }

    bool DistortionPass::EnsureOffsetTarget(const RHI::TexturePtr& target)
    {
        if (!m_OffsetRenderPass)
        {
            m_OffsetRenderPass = CreateDistortionRenderPass(*m_Device, RHI::Format::R16G16_FLOAT, true);
        }
        if (!m_OffsetRenderPass)
        {
            return false;
        }
        if (!m_OffsetPipeline)
        {
            m_OffsetPipeline = CreateDistortionPipeline(*m_Device, m_OffsetVertexShader, m_OffsetFragmentShader,
                                                       m_OffsetRenderPass, CreateDistortionLayout(1, true), true);
        }
        if (m_OffsetTarget != target.get() || !m_OffsetFramebuffer)
        {
            m_OffsetFramebuffer = CreateDistortionFramebuffer(*m_Device, m_OffsetRenderPass, target);
            m_OffsetTarget = m_OffsetFramebuffer ? target.get() : nullptr;
        }
        return m_OffsetPipeline && m_OffsetFramebuffer;
    }

    bool DistortionPass::EnsureCompositeTarget(const RHI::TexturePtr& target)
    {
        if (!m_GatherTexture || m_GatherTexture->GetWidth() != target->GetWidth() ||
            m_GatherTexture->GetHeight() != target->GetHeight() || m_GatherTexture->GetFormat() != target->GetFormat())
        {
            m_GatherFramebuffer.reset();
            m_CopyFramebuffer.reset();
            m_GatherPipeline.reset();
            m_CopyPipeline.reset();
            m_ColorRenderPass.reset();
            m_ColorTarget = nullptr;
            m_GatherTexture = m_Device->CreateTexture(RHI::TextureDesc::RenderTarget(
                target->GetWidth(), target->GetHeight(), target->GetFormat(), "Distortion.Gather"));
        }
        if (!m_GatherTexture)
        {
            return false;
        }
        if (!m_ColorRenderPass)
        {
            m_ColorRenderPass = CreateDistortionRenderPass(*m_Device, target->GetFormat(), false);
        }
        if (!m_ColorRenderPass)
        {
            return false;
        }
        if (!m_GatherFramebuffer)
        {
            m_GatherFramebuffer = CreateDistortionFramebuffer(*m_Device, m_ColorRenderPass, m_GatherTexture);
        }
        if (!m_GatherPipeline)
        {
            m_GatherPipeline = CreateDistortionPipeline(*m_Device, m_FullscreenVertexShader, m_GatherShader,
                                                       m_ColorRenderPass, CreateDistortionLayout(2));
        }
        if (!m_CopyPipeline)
        {
            m_CopyPipeline = CreateDistortionPipeline(*m_Device, m_FullscreenVertexShader, m_CopyShader,
                                                     m_ColorRenderPass, CreateDistortionLayout(1));
        }
        if (m_ColorTarget != target.get() || !m_CopyFramebuffer)
        {
            m_CopyFramebuffer = CreateDistortionFramebuffer(*m_Device, m_ColorRenderPass, target);
            m_ColorTarget = m_CopyFramebuffer ? target.get() : nullptr;
        }
        return m_GatherFramebuffer && m_CopyFramebuffer && m_GatherPipeline && m_CopyPipeline;
    }
} // namespace NorvesLib::Core::Rendering
