#include "GBufferDebugPass.h"

#if NORVES_ENABLE_STATS

#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#include <cstdlib>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t DebugParamsBytes = 16; // 表示の引数（vec4）

        void AddBinding(RHI::DescriptorSetDesc& desc,
                        uint32_t binding,
                        RHI::ResourceBindType type,
                        RHI::ShaderStage stages)
        {
            RHI::DescriptorBinding descriptorBinding;
            descriptorBinding.binding = binding;
            descriptorBinding.type = type;
            descriptorBinding.stages = stages;
            desc.bindings.push_back(descriptorBinding);
        }

        // gbuffer_debug.frag の binding 0〜3（法線・速度・深度・パラメータ）
        RHI::DescriptorSetDesc MakeGBufferDebugDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 1, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 2, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 3, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Pixel);
            return desc;
        }

        // 速度の表示の倍率。1 フレームの UV の動き（数千分の 1）を、灰色 0.5 からの差として見える大きさにする
        constexpr float GBufferDebugVelocityScale = 400.0f;
    } // namespace

    GBufferDebugPass::GBufferDebugPass(GBufferDebugView view)
        : m_View(view)
    {
    }

    GBufferDebugPass::~GBufferDebugPass()
    {
        Shutdown();
    }

    bool GBufferDebugPass::TryGetViewFromEnvironment(GBufferDebugView& outView)
    {
        char* value = nullptr;
        size_t length = 0;
        bool bMatched = false;
        if (_dupenv_s(&value, &length, "NORVES_GBUFFER_DEBUG") == 0 && value)
        {
            if (std::strcmp(value, "normal") == 0)
            {
                outView = GBufferDebugView::Normal;
                bMatched = true;
            }
            else if (std::strcmp(value, "velocity") == 0)
            {
                outView = GBufferDebugView::Velocity;
                bMatched = true;
            }
            else if (std::strcmp(value, "depth") == 0)
            {
                outView = GBufferDebugView::Depth;
                bMatched = true;
            }
        }
        std::free(value);
        return bMatched;
    }

    bool GBufferDebugPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("gbuffer_debug.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("GBufferDebugPass", "検証表示のシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        // 値をそのまま比べるのでフィルターしない（texelFetch で読む）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = m_Device->CreateSampler(samplerDesc);
        if (!m_Sampler)
        {
            NORVES_LOG_WARNING("GBufferDebugPass", "検証表示のサンプラーの作成に失敗。このパスは何もしません");
        }
        return true;
    }

    void GBufferDebugPass::Shutdown()
    {
        m_Pipeline.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_FramebufferColor = nullptr;
        m_ColorFormat = RHI::Format::UNKNOWN;
        m_Sampler.reset();
        m_Uses.Clear();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ColorHandle = {};
        m_NormalHandle = {};
        m_VelocityHandle = {};
        m_DepthHandle = {};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void GBufferDebugPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void GBufferDebugPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void GBufferDebugPass::Declare(RenderGraphBuilder& builder)
    {
        m_ColorHandle = {};
        m_NormalHandle = {};
        m_VelocityHandle = {};
        m_DepthHandle = {};

        // GBuffer の 3 枚はどれかが無ければ（描画のパスが何も宣言しなかった）色の添付も宣言しない
        RGTextureHandle normalHandle;
        RGTextureHandle velocityHandle;
        RGTextureHandle depthHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, normalHandle, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferVelocity, velocityHandle, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depthHandle, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        // 最後のシーンの色（SSR があればその出力）へ書く
        RGTextureHandle colorHandle;
        if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                 colorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource) &&
            !builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                 colorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource))
        {
            return;
        }

        m_ColorHandle = colorHandle.ToResourceHandle();
        m_NormalHandle = normalHandle;
        m_VelocityHandle = velocityHandle;
        m_DepthHandle = depthHandle;
        builder.PreserveInsertionOrder();
    }

    void GBufferDebugPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        if (!context.CommandList || !m_Device || !m_ColorHandle.IsValid() || !m_NormalHandle.IsValid() ||
            !m_VelocityHandle.IsValid() || !m_DepthHandle.IsValid())
        {
            return;
        }

        const RHI::TexturePtr colorTexture = resources.GetTexture(m_ColorHandle);
        const RHI::TexturePtr normalTexture = resources.GetTexture(m_NormalHandle);
        const RHI::TexturePtr velocityTexture = resources.GetTexture(m_VelocityHandle);
        const RHI::TexturePtr depthTexture = resources.GetTexture(m_DepthHandle);
        if (!colorTexture || !normalTexture || !velocityTexture || !depthTexture)
        {
            return;
        }

        // 色の添付の形式が変わったときだけ、レンダーパス・パイプラインを作り直す
        if (!m_RenderPass || m_ColorFormat != colorTexture->GetFormat())
        {
            m_Pipeline.reset();
            m_RenderPass.reset();
            m_Framebuffer.reset();
            m_FramebufferColor = nullptr;

            RHI::RenderPassDesc renderPassDesc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = colorTexture->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
            m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
            m_ColorFormat = colorTexture->GetFormat();

            if (m_RenderPass && m_VertexShader && m_FragmentShader)
            {
                RHI::GraphicsPipelineDesc pipelineDesc;
                pipelineDesc.vertexShader = m_VertexShader;
                pipelineDesc.pixelShader = m_FragmentShader;
                pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
                pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
                pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
                pipelineDesc.rasterState.lineWidth = 1.0f;
                pipelineDesc.depthStencilState.depthTestEnable = false;
                pipelineDesc.depthStencilState.depthWriteEnable = false;
                RHI::BlendAttachmentDesc blendAttachment;
                blendAttachment.blendEnable = false;
                blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
                pipelineDesc.blendState.attachments.push_back(blendAttachment);
                pipelineDesc.renderPass = m_RenderPass;
                pipelineDesc.descriptorSetLayouts.push_back(MakeGBufferDebugDescriptorSetDesc());
                m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            }
        }
        if (!m_RenderPass)
        {
            return;
        }

        if (!m_Framebuffer || m_FramebufferColor != colorTexture.get())
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            framebufferDesc.colorTargets.push_back(colorTexture);
            framebufferDesc.width = colorTexture->GetWidth();
            framebufferDesc.height = colorTexture->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferColor = m_Framebuffer ? colorTexture.get() : nullptr;
        }
        if (!m_Framebuffer)
        {
            return;
        }

        // 資源は Execute の回数ではなくフレームの枠で決める（同じフレームに何回 Execute されても提出前の資源を上書きしない）
        m_Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        Use* use = nullptr;
        if (m_Pipeline && m_Sampler)
        {
            use = &m_Uses.Acquire();
            if (!use->ParamsUniform)
            {
                use->ParamsUniform = m_Device->CreateBuffer(
                    RHI::BufferDesc(DebugParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "GBuffer_DebugParams"));
            }
            if (!use->DescriptorSet)
            {
                use->DescriptorSet = m_Device->CreateDescriptorSet(MakeGBufferDebugDescriptorSetDesc());
            }
            if (!use->ParamsUniform || !use->DescriptorSet)
            {
                NORVES_LOG_WARNING("GBufferDebugPass", "検証表示の資源の作成に失敗。この描画は何もしません");
                use = nullptr;
            }
        }
        const bool bCanDraw = use != nullptr;
        if (bCanDraw)
        {
            const float params[4] = {static_cast<float>(static_cast<uint32_t>(m_View)), GBufferDebugVelocityScale, 0.0f, 0.0f};
            use->ParamsUniform->Update(params, sizeof(params));
            RHI::DescriptorSetPtr& descriptorSet = use->DescriptorSet;
            descriptorSet->BindTexture(0, normalTexture);
            descriptorSet->BindSampler(0, m_Sampler);
            descriptorSet->BindTexture(1, velocityTexture);
            descriptorSet->BindSampler(1, m_Sampler);
            descriptorSet->BindTexture(2, depthTexture);
            descriptorSet->BindSampler(2, m_Sampler);
            descriptorSet->BindConstantBuffer(3, use->ParamsUniform, 0, DebugParamsBytes);
            descriptorSet->Update();
        }

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        if (bCanDraw)
        {
            commandList->SetPipeline(m_Pipeline);
            commandList->SetDescriptorSet(use->DescriptorSet, 0);
            commandList->Draw(3, 0);
        }
        commandList->EndRenderPass();
    }

} // namespace NorvesLib::Core::Rendering

#endif // NORVES_ENABLE_STATS
