#include "GBufferDebugPass.h"

#if NORVES_ENABLE_STATS

#include "Logging/LogMacros.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RTGIContract.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VisibilityResolvePass.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#include <cmath>
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

        // gbuffer_aov.frag の AovParams（mat4 の逆ビュー投影・カメラの位置・前方向・照明の出力の有無）
        constexpr uint32_t AovParamsBytes = 64 + 16 + 16 + 16;
        constexpr RHI::Format AovFormat = RHI::Format::R32G32B32A32_FLOAT;
        // 書く枚数（表面・幾何・照明・印）
        constexpr uint32_t AovTargetCount = 4;

        // gbuffer_aov.frag の binding 0〜7（アルベド・法線・材質・深度・パラメータ・照明の鏡面の反射率・
        // RTGI の時間の蓄積の後・空間のフィルタの後）
        RHI::DescriptorSetDesc MakeGBufferAovDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t binding = 0; binding < 4u; ++binding)
            {
                AddBinding(desc, binding, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            }
            AddBinding(desc, 4, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Pixel);
            AddBinding(desc, 5, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 6, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 7, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 8, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            return desc;
        }
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
            else if (std::strcmp(value, "albedo") == 0)
            {
                outView = GBufferDebugView::Albedo;
                bMatched = true;
            }
            else if (std::strcmp(value, "material") == 0)
            {
                outView = GBufferDebugView::Material;
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
        // 1 つ目の束縛（シェーダーの normalTexture）は、法線・アルベド・材質の表示ではそのテクスチャを読む
        const auto firstSource = m_View == GBufferDebugView::Albedo     ? RenderGraphResourceNames::GBufferAlbedo
                                     : m_View == GBufferDebugView::Material ? RenderGraphResourceNames::GBufferMaterial
                                                                            : RenderGraphResourceNames::GBufferNormal;
        if (!builder.TryReadTexture(firstSource, normalHandle, RHI::ResourceState::ShaderResource) ||
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
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                use->ParamsUniform = m_Device->CreateBuffer(
                    RHI::BufferDesc(DebugParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "GBuffer_DebugParams"));
            }
            if (!use->DescriptorSet)
            {
                RHI::FrameResourceProtectedCreationScope protectedCreation;
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

    GBufferAovPass::~GBufferAovPass()
    {
        Shutdown();
    }

    bool GBufferAovPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("gbuffer_aov.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("GBufferAovPass", "AOV のシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = m_Device->CreateSampler(samplerDesc);

        RHI::RenderPassDesc renderPassDesc;
        for (uint32_t target = 0; target < AovTargetCount; ++target)
        {
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = AovFormat;
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
        }
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (m_RenderPass && m_Sampler)
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
            for (uint32_t target = 0; target < AovTargetCount; ++target)
            {
                RHI::BlendAttachmentDesc blendAttachment;
                blendAttachment.blendEnable = false;
                blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
                pipelineDesc.blendState.attachments.push_back(blendAttachment);
            }
            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(MakeGBufferAovDescriptorSetDesc());
            m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        }
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("GBufferAovPass", "AOV のパイプラインの作成に失敗。このパスは何もしません");
        }
        return true;
    }

    void GBufferAovPass::Shutdown()
    {
        m_Pipeline.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        for (RHI::ITexture*& target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        m_Sampler.reset();
        m_Uses.Clear();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_SurfaceHandle = {};
        m_GeometryHandle = {};
        m_LightingHandle = {};
        m_IdsHandle = {};
        m_ReflectanceHandle = {};
        m_RTGITemporalHandle = {};
        m_GeometricNormalHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_MaterialHandle = {};
        m_DepthHandle = {};
        m_LastCamera = FrameCaptureAovCamera{};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void GBufferAovPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void GBufferAovPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void GBufferAovPass::Declare(RenderGraphBuilder& builder)
    {
        m_SurfaceHandle = {};
        m_GeometryHandle = {};
        m_LightingHandle = {};
        m_IdsHandle = {};
        m_ReflectanceHandle = {};
        m_RTGITemporalHandle = {};
        m_GeometricNormalHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_MaterialHandle = {};
        m_DepthHandle = {};

        // 初期化を済ませたのにパイプラインが無いときは何も宣言しない
        if (m_bInitialized && !m_Pipeline)
        {
            return;
        }
        // GBuffer の 4 枚がそろわない構成では何も宣言しない（一部だけ読む宣言を残さない）
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle material;
        RGTextureHandle depth;
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedo) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferMaterial, material) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depth))
        {
            return;
        }
        if (!builder.TryReadTexture(RenderGraphResourceNames::GBufferAlbedo, albedo, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, normal, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferMaterial, material, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depth, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        const ViewRenderContext* context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 0;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 0;
        if (width == 0 || height == 0)
        {
            return;
        }
        // 照明の出力（a = 太陽の可視率）は、照明のパスがある構成だけ読む。無ければ可視率を -1 にする
        RGTextureHandle reflectance;
        if (builder.TryGetTexture(RenderGraphResourceNames::LightingSpecularReflectance, reflectance) &&
            !builder.TryReadTexture(RenderGraphResourceNames::LightingSpecularReflectance, reflectance,
                                    RHI::ResourceState::ShaderResource))
        {
            reflectance = {};
        }
        // RTGI の時間の蓄積の後の出力（a = 遮蔽の診断）は、RTGI が動く構成だけ読む
        RGTextureHandle rtgiTemporal;
        if (builder.TryGetTexture(RenderGraphResourceNames::RTGIDiffuseIndirect, rtgiTemporal) &&
            !builder.TryReadTexture(RenderGraphResourceNames::RTGIDiffuseIndirect, rtgiTemporal,
                                    RHI::ResourceState::ShaderResource))
        {
            rtgiTemporal = {};
        }
        // 可視バッファの解決が書いた三角形の面の幾何の法線は、解決が書く構成（--capture-aov と可視バッファ）だけ読む
        RGTextureHandle geometricNormal;
        if (builder.TryGetTexture(RenderGraphResourceNames::VisResolveGeometricNormal, geometricNormal) &&
            !builder.TryReadTexture(RenderGraphResourceNames::VisResolveGeometricNormal, geometricNormal,
                                    RHI::ResourceState::ShaderResource))
        {
            geometricNormal = {};
        }
        const Identity targetNames[AovTargetCount] = {RenderGraphResourceNames::CaptureAovSurface,
                                                      RenderGraphResourceNames::CaptureAovGeometry,
                                                      RenderGraphResourceNames::CaptureAovLighting,
                                                      RenderGraphResourceNames::CaptureAovIds};
        const char* targetDebugNames[AovTargetCount] = {"Capture_AovSurface", "Capture_AovGeometry", "Capture_AovLighting",
                                                        "Capture_AovIds"};
        RGResourceHandle targets[AovTargetCount];
        for (uint32_t target = 0; target < AovTargetCount; ++target)
        {
            RGTextureDesc desc = RGTextureDesc::RenderTarget(width, height, AovFormat, targetDebugNames[target]);
            desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
            const RGTextureHandle handle = builder.WriteTextureAttachment(targetNames[target],
                                                                          desc,
                                                                          RGAttachmentKind::Color,
                                                                          RHI::AttachmentLoadOp::DontCare,
                                                                          RHI::AttachmentStoreOp::Store,
                                                                          RHI::ResourceState::RenderTarget,
                                                                          RHI::ResourceState::ShaderResource);
            if (!handle.IsValid())
            {
                return;
            }
            // 撮影の読み出しはグラフの実行の後なので、外へ出して寿命を延ばす
            builder.ExportTexture(targetNames[target], handle);
            targets[target] = handle.ToResourceHandle();
        }

        m_SurfaceHandle = targets[0];
        m_GeometryHandle = targets[1];
        m_LightingHandle = targets[2];
        m_IdsHandle = targets[3];
        m_ReflectanceHandle = reflectance;
        m_RTGITemporalHandle = rtgiTemporal;
        m_GeometricNormalHandle = geometricNormal;
        m_AlbedoHandle = albedo;
        m_NormalHandle = normal;
        m_MaterialHandle = material;
        m_DepthHandle = depth;
        builder.PreserveInsertionOrder();
    }

    void GBufferAovPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_LastCamera = FrameCaptureAovCamera{};
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        const CameraProxy* camera = context.GetActiveCamera();
        if (!context.CommandList || !m_Device || !m_Pipeline || !camera || !m_SurfaceHandle.IsValid() ||
            !m_GeometryHandle.IsValid() || !m_LightingHandle.IsValid() || !m_IdsHandle.IsValid() ||
            !m_AlbedoHandle.IsValid() || !m_NormalHandle.IsValid() || !m_MaterialHandle.IsValid() ||
            !m_DepthHandle.IsValid())
        {
            return;
        }

        const RHI::TexturePtr targets[AovTargetCount] = {resources.GetTexture(m_SurfaceHandle),
                                                         resources.GetTexture(m_GeometryHandle),
                                                         resources.GetTexture(m_LightingHandle),
                                                         resources.GetTexture(m_IdsHandle)};
        const RHI::TexturePtr albedoTexture = resources.GetTexture(m_AlbedoHandle);
        const RHI::TexturePtr normalTexture = resources.GetTexture(m_NormalHandle);
        const RHI::TexturePtr materialTexture = resources.GetTexture(m_MaterialHandle);
        const RHI::TexturePtr depthTexture = resources.GetTexture(m_DepthHandle);
        const RHI::TexturePtr reflectanceTexture =
            m_ReflectanceHandle.IsValid() ? resources.GetTexture(m_ReflectanceHandle) : RHI::TexturePtr{};
        if (!targets[0] || !targets[1] || !targets[2] || !targets[3] || !albedoTexture || !normalTexture ||
            !materialTexture || !depthTexture)
        {
            return;
        }

        bool bFramebufferCurrent = m_Framebuffer != nullptr;
        for (uint32_t target = 0; target < AovTargetCount; ++target)
        {
            bFramebufferCurrent = bFramebufferCurrent && m_FramebufferTargets[target] == targets[target].get();
        }
        if (!bFramebufferCurrent)
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            for (uint32_t target = 0; target < AovTargetCount; ++target)
            {
                framebufferDesc.colorTargets.push_back(targets[target]);
            }
            framebufferDesc.width = targets[0]->GetWidth();
            framebufferDesc.height = targets[0]->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            for (uint32_t target = 0; target < AovTargetCount; ++target)
            {
                m_FramebufferTargets[target] = m_Framebuffer ? targets[target].get() : nullptr;
            }
        }
        if (!m_Framebuffer)
        {
            return;
        }

        m_Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        Use& use = m_Uses.Acquire();
        if (!use.ParamsUniform)
        {
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            use.ParamsUniform = m_Device->CreateBuffer(
                RHI::BufferDesc(AovParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "GBuffer_AovParams"));
        }
        if (!use.DescriptorSet)
        {
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeGBufferAovDescriptorSetDesc());
        }
        if (!use.ParamsUniform || !use.DescriptorSet)
        {
            NORVES_LOG_WARNING("GBufferAovPass", "AOV の資源の作成に失敗。この描画は何もしません");
            return;
        }

        // 照明の深度からの位置の復元（lighting.frag の ReconstructWorldPosition）と同じ行列
        const float aspectRatio = context.GetActiveAspectRatio();
        const CameraViewConstants cameraConstants = CameraViewConstants::BuildForDevice(*camera, aspectRatio, context.Device);
        float params[AovParamsBytes / sizeof(float)] = {};
        cameraConstants.CopyShaderInverseViewProjection(params);
        cameraConstants.CopyCameraPosition(params + 16);
        float forward[3] = {camera->ForwardX, camera->ForwardY, camera->ForwardZ};
        const float forwardLength = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
        if (!(forwardLength > 1.0e-6f) || !std::isfinite(forwardLength))
        {
            return;
        }
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            forward[axis] /= forwardLength;
            params[20 + axis] = forward[axis];
        }
        // x: 1 なら binding 5 が照明の出力（a = 太陽の可視率）。0 なら可視率を -1 にする
        params[24] = reflectanceTexture ? 1.0f : 0.0f;
        // y: 1 なら binding 6・7 が同じフレームの RTGI の時間の蓄積の後・空間のフィルタの後（a = 遮蔽の診断）。0 なら -1 にする
        const RHI::TexturePtr rtgiTemporalTexture =
            m_RTGITemporalHandle.IsValid() ? resources.GetTexture(m_RTGITemporalHandle) : RHI::TexturePtr{};
        const RTGIResult& rtgiResult = context.PhysicalLighting.RTGI.Result;
        const bool bRTGIAvailable = rtgiTemporalTexture && rtgiResult.IsComplete() &&
                                    rtgiResult.FrameNumber == context.FrameNumber &&
                                    rtgiResult.Width == rtgiTemporalTexture->GetWidth() &&
                                    rtgiResult.Height == rtgiTemporalTexture->GetHeight();
        params[25] = bRTGIAvailable ? 1.0f : 0.0f;
        // z: binding 8 の幾何の法線のうち、w の絶対値がこの値の画素だけがこのフレームに解決が書いた面の法線（w が負なら地形の面。
        // 0 なら幾何の法線と印は無い）
        const RHI::TexturePtr geometricNormalTexture =
            m_GeometricNormalHandle.IsValid() ? resources.GetTexture(m_GeometricNormalHandle) : RHI::TexturePtr{};
        params[26] = geometricNormalTexture ? VisibilityResolvePass::GetGeometricNormalTag(context.FrameNumber) : 0.0f;
        use.ParamsUniform->Update(params, sizeof(params));
        RHI::DescriptorSetPtr& descriptorSet = use.DescriptorSet;
        const RHI::TexturePtr sources[4] = {albedoTexture, normalTexture, materialTexture, depthTexture};
        for (uint32_t binding = 0; binding < 4u; ++binding)
        {
            descriptorSet->BindTexture(binding, sources[binding]);
            descriptorSet->BindSampler(binding, m_Sampler);
        }
        descriptorSet->BindConstantBuffer(4, use.ParamsUniform, 0, AovParamsBytes);
        // 照明の出力が無い構成は、読まない代わりの画像（アルベド）を結ぶ
        descriptorSet->BindTexture(5, reflectanceTexture ? reflectanceTexture : albedoTexture);
        descriptorSet->BindSampler(5, m_Sampler);
        // RTGI が無い構成も同じく、読まない代わりの画像を結ぶ
        descriptorSet->BindTexture(6, bRTGIAvailable ? rtgiTemporalTexture : albedoTexture);
        descriptorSet->BindSampler(6, m_Sampler);
        descriptorSet->BindTexture(7, bRTGIAvailable ? rtgiResult.DiffuseIndirectRadiance : albedoTexture);
        descriptorSet->BindSampler(7, m_Sampler);
        descriptorSet->BindTexture(8, geometricNormalTexture ? geometricNormalTexture : albedoTexture);
        descriptorSet->BindSampler(8, m_Sampler);
        descriptorSet->Update();

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();

        m_LastCamera.bValid = true;
        m_LastCamera.Position[0] = camera->PositionX;
        m_LastCamera.Position[1] = camera->PositionY;
        m_LastCamera.Position[2] = camera->PositionZ;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            m_LastCamera.Forward[axis] = forward[axis];
        }
        m_LastCamera.Up[0] = camera->UpX;
        m_LastCamera.Up[1] = camera->UpY;
        m_LastCamera.Up[2] = camera->UpZ;
        m_LastCamera.Right[0] = camera->RightX;
        m_LastCamera.Right[1] = camera->RightY;
        m_LastCamera.Right[2] = camera->RightZ;
        m_LastCamera.VerticalFieldOfViewDegrees = camera->FieldOfView;
        m_LastCamera.AspectRatio = aspectRatio;
        m_LastCamera.NearPlane = camera->NearPlane;
        m_LastCamera.FarPlane = camera->FarPlane;
        m_LastCamera.bOrthographic = camera->Projection != ProjectionType::Perspective;
    }

} // namespace NorvesLib::Core::Rendering

#endif // NORVES_ENABLE_STATS
