#include "Rendering/DecalPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DecalTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cstddef>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 両シェーダーの定数と同じ std140 の並び
        struct DecalUniform
        {
            float ViewProjection[16];
            float InverseViewProjection[16];
            float TargetSize[4];
        };
        static_assert(sizeof(DecalUniform) == 144);
        static_assert(sizeof(DecalInstance) == 80);
        static_assert(offsetof(DecalInstance, Position) == 0);
        static_assert(offsetof(DecalInstance, Rotation) == 12);
        static_assert(offsetof(DecalInstance, HalfExtents) == 28);
        static_assert(offsetof(DecalInstance, Color) == 40);
        static_assert(offsetof(DecalInstance, NormalOpacity) == 56);
        static_assert(offsetof(DecalInstance, LocalNormal) == 60);
        static_assert(offsetof(DecalInstance, EdgeFade) == 72);
        static_assert(offsetof(DecalInstance, Flags) == 76);

        RHI::DescriptorSetDesc CreateDecalDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding constants;
            constants.binding = 0;
            constants.type = RHI::ResourceBindType::ConstantBuffer;
            constants.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            desc.bindings.push_back(constants);
            for (uint32_t binding = 1; binding <= 2; ++binding)
            {
                RHI::DescriptorBinding texture;
                texture.binding = binding;
                texture.type = RHI::ResourceBindType::CombinedImageSampler;
                texture.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(texture);
            }
            return desc;
        }
    } // namespace

    DecalPass::~DecalPass()
    {
        Shutdown();
    }

    bool DecalPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("DecalPass", "Device か ShaderManager がありません");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("decal.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("decal.frag", RHI::ShaderStage::Pixel);
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateDecalDescriptorSetDesc());
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_PointSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_VertexShader || !m_FragmentShader || !m_DescriptorSetLayout || !m_PointSampler)
        {
            NORVES_LOG_ERROR("DecalPass", "DECAL_INIT_FAILED シェーダーか記述子の準備に失敗しました");
            Shutdown();
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void DecalPass::Shutdown()
    {
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_RenderPass.reset();
        m_PointSampler.reset();
        m_DescriptorSetLayout.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_MaterialHandle = {};
        m_DepthHandle = {};
        for (const RHI::ITexture *&target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        m_LoggedDecalCount = 0;
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void DecalPass::Setup(ViewRenderContext &context)
    {
        (void)context;
    }

    void DecalPass::Execute(ViewRenderContext &context)
    {
        (void)context;
    }

    void DecalPass::Declare(RenderGraphBuilder &builder)
    {
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_MaterialHandle = {};
        m_DepthHandle = {};
        builder.PreserveInsertionOrder();

        const ViewRenderContext *context = builder.GetContext();
        if (!context || !context->SnapshotDecals || context->SnapshotDecals->IsEmpty() || !context->GetActiveCamera())
        {
            if (context && (!context->SnapshotDecals || context->SnapshotDecals->IsEmpty()) && m_LoggedDecalCount != 0)
            {
                m_LoggedDecalCount = 0;
                NORVES_LOG_INFO("DecalPass", "DECAL_DRAW instances=0");
            }
            return;
        }

        // 全部そろう構成だけで宣言し、深度は添付せず読み取る
        const Identity names[] = {RenderGraphResourceNames::GBufferAlbedo, RenderGraphResourceNames::GBufferNormal,
                                  RenderGraphResourceNames::GBufferMaterial, RenderGraphResourceNames::GBufferDepth};
        RGTextureHandle available;
        for (const Identity &name : names)
        {
            if (!builder.TryGetTexture(name, available))
            {
                return;
            }
        }
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RGTextureHandle handle;
            if (builder.TryUseAttachment(names[index], handle, RGAttachmentKind::Color, RGAttachmentMutability::Write,
                                         RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource))
            {
                m_ColorHandles[index] = handle.ToResourceHandle();
            }
        }
        RGTextureHandle material;
        RGTextureHandle depth;
        if (builder.TryReadTexture(names[2], material, RHI::ResourceState::ShaderResource))
        {
            m_MaterialHandle = material.ToResourceHandle();
        }
        if (builder.TryReadTexture(names[3], depth, RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depth.ToResourceHandle();
        }
    }

    void DecalPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        const DecalScene *scene = context.SnapshotDecals;
        const CameraProxy *camera = context.GetActiveCamera();
        if (!scene || scene->IsEmpty() || !camera || !context.CommandList || !context.FrameResources ||
            !m_ColorHandles[0].IsValid() || !m_ColorHandles[1].IsValid() || !m_MaterialHandle.IsValid() ||
            !m_DepthHandle.IsValid())
        {
            return;
        }
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        RecordDecals(resources.GetTexture(m_ColorHandles[0]), resources.GetTexture(m_ColorHandles[1]),
                     resources.GetTexture(m_MaterialHandle), resources.GetTexture(m_DepthHandle), *context.CommandList,
                     *context.FrameResources, *scene, cameraConstants);
    }

    bool DecalPass::RecordDecals(const RHI::TexturePtr &albedoTarget, const RHI::TexturePtr &normalTarget,
                                 const RHI::TexturePtr &materialTexture, const RHI::TexturePtr &depthTexture,
                                 RHI::ICommandList &commandList, FrameResourceLane &frameResources,
                                 const DecalScene &scene, const CameraViewConstants &cameraConstants)
    {
        if (!m_bInitialized)
        {
            return false;
        }
        if (scene.IsEmpty())
        {
            if (m_LoggedDecalCount != 0)
            {
                m_LoggedDecalCount = 0;
                NORVES_LOG_INFO("DecalPass", "DECAL_DRAW instances=0");
            }
            return true;
        }
        if (!albedoTarget || !normalTarget || !materialTexture || !depthTexture ||
            scene.Decals->size() > (std::numeric_limits<uint32_t>::max)())
        {
            return false;
        }
        const uint32_t width = albedoTarget->GetWidth();
        const uint32_t height = albedoTarget->GetHeight();
        const RHI::TexturePtr textures[] = {albedoTarget, normalTarget, materialTexture, depthTexture};
        if (width == 0 || height == 0 || width > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
            height > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()))
        {
            return false;
        }
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (textures[i]->GetWidth() != width || textures[i]->GetHeight() != height)
            {
                return false;
            }
            for (uint32_t j = 0; j < i; ++j)
            {
                if (textures[i].get() == textures[j].get())
                {
                    return false;
                }
            }
        }
        const RHI::TexturePtr colorTargets[kColorTargetCount] = {albedoTarget, normalTarget};
        if (!EnsureTargets(colorTargets))
        {
            return false;
        }

        DecalUniform uniform{};
        cameraConstants.CopyShaderViewProjection(uniform.ViewProjection);
        cameraConstants.CopyShaderInverseViewProjection(uniform.InverseViewProjection);
        uniform.TargetSize[0] = static_cast<float>(width);
        uniform.TargetSize[1] = static_cast<float>(height);
        const FrameUploadAllocation constants = frameResources.AllocateConstants(uniform);
        const uint64_t bytes = static_cast<uint64_t>(scene.Decals->size()) * sizeof(DecalInstance);
        const FrameUploadAllocation instances = frameResources.AllocateUploadData(scene.Decals->data(), bytes, FrameUploadUsage::Vertex);
        const FrameDescriptorAllocation descriptor = frameResources.AllocateDescriptorSet(m_DescriptorSetLayout);
        if (!constants.IsValid() || !instances.IsValid() || !descriptor.IsValid())
        {
            return false;
        }
        descriptor.Set->BindConstantBuffer(0, constants.Buffer, constants.GetOffset32(), constants.GetSize32());
        descriptor.Set->BindTexture(1, materialTexture);
        descriptor.Set->BindSampler(1, m_PointSampler);
        descriptor.Set->BindTexture(2, depthTexture);
        descriptor.Set->BindSampler(2, m_PointSampler);
        descriptor.Set->Update();

        RHI::Viewport viewport;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(width);
        scissor.bottom = static_cast<int32_t>(height);
        commandList.BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList.SetViewport(viewport);
        commandList.SetScissor(scissor);
        commandList.SetPipeline(m_Pipeline);
        commandList.SetDescriptorSet(descriptor.Set, 0);
        commandList.SetVertexBuffer(instances.Buffer, instances.Offset, 0);
        const uint32_t count = static_cast<uint32_t>(scene.Decals->size());
        commandList.DrawInstanced(36, count);
        commandList.EndRenderPass();
        if (m_LoggedDecalCount != count)
        {
            m_LoggedDecalCount = count;
            NORVES_LOG_INFO("DecalPass", "DECAL_DRAW instances=%u", count);
        }
        return true;
    }

    bool DecalPass::EnsureTargets(const RHI::TexturePtr (&colorTargets)[kColorTargetCount])
    {
        bool sameTargets = m_RenderPass && m_Pipeline && m_Framebuffer;
        for (uint32_t index = 0; index < kColorTargetCount && sameTargets; ++index)
        {
            sameTargets = m_FramebufferTargets[index] == colorTargets[index].get();
        }
        if (sameTargets)
        {
            return true;
        }
        m_Framebuffer.reset();
        for (const RHI::ITexture *&target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        if (!CreatePipeline(colorTargets))
        {
            return false;
        }
        RHI::FramebufferDesc desc;
        desc.renderPass = m_RenderPass;
        for (const RHI::TexturePtr &target : colorTargets)
        {
            desc.colorTargets.push_back(target);
        }
        desc.width = colorTargets[0]->GetWidth();
        desc.height = colorTargets[0]->GetHeight();
        m_Framebuffer = m_Device->CreateFramebuffer(desc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("DecalPass", "DECAL_INIT_FAILED framebuffer を作れません");
            return false;
        }
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_FramebufferTargets[index] = colorTargets[index].get();
        }
        return true;
    }

    bool DecalPass::CreatePipeline(const RHI::TexturePtr (&colorTargets)[kColorTargetCount])
    {
        m_Pipeline.reset();
        m_RenderPass.reset();
        RHI::RenderPassDesc renderPassDesc;
        for (const RHI::TexturePtr &target : colorTargets)
        {
            RHI::AttachmentDesc attachment;
            attachment.format = target->GetFormat();
            attachment.isDepthStencil = false;
            attachment.clear = false;
            attachment.loadOp = RHI::AttachmentLoadOp::Load;
            attachment.storeOp = RHI::AttachmentStoreOp::Store;
            attachment.initialState = RHI::ResourceState::RenderTarget;
            attachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(attachment);
        }
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("DecalPass", "DECAL_INIT_FAILED render pass を作れません");
            return false;
        }
        RHI::GraphicsPipelineDesc desc;
        desc.vertexShader = m_VertexShader;
        desc.pixelShader = m_FragmentShader;
        desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        RHI::VertexBindingDesc binding;
        binding.binding = 0;
        binding.stride = sizeof(DecalInstance);
        binding.inputRate = RHI::VertexInputRate::Instance;
        desc.vertexBindings.push_back(binding);
        struct Attribute
        {
            RHI::Format Format;
            uint32_t Offset;
        };
        const Attribute attributes[] = {
            {RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, Position))},
            {RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, Rotation))},
            {RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, HalfExtents))},
            {RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, Color))},
            {RHI::Format::R32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, NormalOpacity))},
            {RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, LocalNormal))},
            {RHI::Format::R32_FLOAT, static_cast<uint32_t>(offsetof(DecalInstance, EdgeFade))},
            {RHI::Format::R32_UINT, static_cast<uint32_t>(offsetof(DecalInstance, Flags))},
        };
        for (uint32_t index = 0; index < sizeof(attributes) / sizeof(attributes[0]); ++index)
        {
            RHI::VertexAttributeDesc attribute;
            attribute.location = index;
            attribute.binding = 0;
            attribute.format = attributes[index].Format;
            attribute.offset = attributes[index].Offset;
            desc.vertexAttributes.push_back(attribute);
        }
        // この箱の頂点順は既存メッシュと逆なので、それに合わせて表面を外す
        desc.rasterState.cullMode = RHI::CullMode::Front;
        desc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        desc.depthStencilState.depthTestEnable = false;
        desc.depthStencilState.depthWriteEnable = false;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = true;
            blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
            blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
            blend.colorWriteMask = RHI::ColorWriteMask::R | RHI::ColorWriteMask::G | RHI::ColorWriteMask::B;
            desc.blendState.attachments.push_back(blend);
        }
        desc.renderPass = m_RenderPass;
        desc.descriptorSetLayouts.push_back(CreateDecalDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(desc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("DecalPass", "DECAL_INIT_FAILED パイプラインを作れません");
            m_RenderPass.reset();
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering
