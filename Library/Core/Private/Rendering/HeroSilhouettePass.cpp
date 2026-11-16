#include "Rendering/HeroSilhouettePass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinnedLodSelection.h"
#include "Rendering/VertexAnimation.h"
#include "Rendering/ViewRenderContext.h"
#include "Resource/SkeletalDrawRange.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr const char* ExtensionName = "Ext.HeroSilhouette";
        // 深度形式の精度の 4 単位と、画素当たりの最大勾配の 1/8 を手前へ寄せる。
        // 計算経路の丸め差に余裕を持たせつつ、近い遮蔽物を見落とす幅を小さくする。
        constexpr float HeroSilhouetteDepthBiasConstant = -4.0f;
        constexpr float HeroSilhouetteDepthBiasSlope = -0.125f;

        struct alignas(16) SilhouetteUniform
        {
            float View[16] = {};
            float Projection[16] = {};
            float VertexAnimInstanceParams[4] = {};
            float VertexAnimTime[4] = {};
            float Color[4] = {};
        };
        static_assert(sizeof(SilhouetteUniform) == 176);

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding uniform;
            uniform.binding = 0;
            uniform.type = RHI::ResourceBindType::ConstantBuffer;
            uniform.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            desc.bindings.push_back(uniform);
            for (uint32_t binding = 8; binding <= 9; ++binding)
            {
                RHI::DescriptorBinding storage;
                storage.binding = binding;
                storage.type = RHI::ResourceBindType::StructuredBuffer;
                storage.stages = RHI::ShaderStage::Vertex;
                desc.bindings.push_back(storage);
            }
            return desc;
        }

        bool IsEnabledForCamera(const CameraProxy* camera)
        {
            return camera && camera->ViewClearance.bEnabled && camera->ViewClearance.bHeroSilhouette &&
                   camera->ViewClearance.HeroObjectId != 0;
        }

        Container::TUniquePtr<IViewPass> CreateHeroSilhouettePass(void*)
        {
            return Container::MakeUnique<HeroSilhouettePass>();
        }

        struct PreparedSilhouetteDraw
        {
            DrawCommand Command;
            Skeletal::SkeletalDrawRange Range;
            RHI::DescriptorSetPtr DescriptorSet;
        };
    }

    HeroSilhouettePass::HeroSilhouettePass(const HeroSilhouetteSettings& settings)
        : m_Settings(settings)
    {
    }

    HeroSilhouettePass::~HeroSilhouettePass()
    {
        Shutdown();
    }

    const char* HeroSilhouettePass::GetName() const
    {
        return ExtensionName;
    }

    bool HeroSilhouettePass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("HeroSilhouettePass", "描画デバイスまたはシェーダー管理がありません");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("hero_silhouette.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("hero_silhouette.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader ||
            !m_UniformAllocator.Initialize(m_Device, sizeof(SilhouetteUniform), MakeDescriptorSetDesc()))
        {
            NORVES_LOG_ERROR("HeroSilhouettePass", "シルエットの描画資源を初期化できませんでした");
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void HeroSilhouettePass::Shutdown()
    {
        m_UniformAllocator.Shutdown();
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_FramebufferColor = nullptr;
        m_FramebufferDepth = nullptr;
        m_ColorFormat = RHI::Format::UNKNOWN;
        m_DepthFormat = RHI::Format::UNKNOWN;
        m_ColorHandle = {};
        m_DepthHandle = {};
        m_bDeclared = false;
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void HeroSilhouettePass::Setup(ViewRenderContext&)
    {
    }

    void HeroSilhouettePass::Execute(ViewRenderContext&)
    {
        // RenderGraph 経由でのみ描画する。
    }

    void HeroSilhouettePass::Declare(RenderGraphBuilder& builder)
    {
        m_bDeclared = false;
        m_ColorHandle = {};
        m_DepthHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        const CameraProxy* camera = context ? context->GetActiveCamera() : nullptr;
        if (!IsEnabledForCamera(camera) || !context->SkinnedMeshes || !context->SnapshotSkinnedMeshFrameLeases)
        {
            return;
        }

        bool bHasHero = false;
        for (const DrawCommand& command : context->GetActiveOpaqueCommands())
        {
            if (command.Draw.PayloadKind == DrawPayloadKind::Skinned &&
                command.Draw.ObjectId == camera->ViewClearance.HeroObjectId)
            {
                bHasHero = true;
                break;
            }
        }
        RGTextureHandle availableColor;
        RGTextureHandle availableDepth;
        if (!bHasHero || !builder.TryGetTexture(RenderGraphResourceNames::SceneColor, availableColor) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, availableDepth))
        {
            return;
        }

        // 失敗や描画対象なしでも最終状態が一致するよう、色は添付の状態のまま次のパスへ渡す。
        if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor, m_ColorHandle,
                                                  RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                                                  RHI::ResourceState::RenderTarget, RHI::ResourceState::RenderTarget) ||
            !builder.TryUseAttachment(RenderGraphResourceNames::SceneDepth, m_DepthHandle,
                                      RGAttachmentKind::DepthStencil, RGAttachmentMutability::ReadOnly,
                                      RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                                      RHI::ResourceState::DepthRead, RHI::ResourceState::DepthRead))
        {
            return;
        }
        builder.PreserveInsertionOrder();
        m_bDeclared = true;
    }

    bool HeroSilhouettePass::PrepareTargets(RenderGraphResources& resources)
    {
        const RHI::TexturePtr color = resources.GetTexture(m_ColorHandle);
        const RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);
        if (!m_Device || !color || !depth || color->GetWidth() == 0 || color->GetHeight() == 0 ||
            color->GetWidth() != depth->GetWidth() || color->GetHeight() != depth->GetHeight())
        {
            return false;
        }

        if (!m_RenderPass || !m_Pipeline || m_ColorFormat != color->GetFormat() ||
            m_DepthFormat != depth->GetFormat())
        {
            m_Pipeline.reset();
            m_Framebuffer.reset();
            m_RenderPass.reset();
            m_FramebufferColor = nullptr;
            m_FramebufferDepth = nullptr;

            RHI::RenderPassDesc renderPassDesc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = color->GetFormat();
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::RenderTarget;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
            renderPassDesc.hasDepthStencil = true;
            auto& depthAttachment = renderPassDesc.depthStencilAttachment;
            depthAttachment.format = depth->GetFormat();
            depthAttachment.isDepthStencil = true;
            depthAttachment.clear = false;
            depthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            depthAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            depthAttachment.initialState = RHI::ResourceState::DepthRead;
            depthAttachment.finalState = RHI::ResourceState::DepthRead;
            m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
            if (!m_RenderPass)
            {
                return false;
            }

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = m_VertexShader;
            pipelineDesc.pixelShader = m_FragmentShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            RHI::VertexBindingDesc vertexBinding;
            vertexBinding.binding = 0;
            vertexBinding.stride = sizeof(SkinnedMeshVertex);
            vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
            pipelineDesc.vertexBindings.push_back(vertexBinding);
            for (uint32_t location = 0; location < 2; ++location)
            {
                RHI::VertexAttributeDesc attribute;
                attribute.location = location;
                attribute.binding = 0;
                attribute.format = RHI::Format::R32G32B32_FLOAT;
                attribute.offset = location * 3 * sizeof(float);
                pipelineDesc.vertexAttributes.push_back(attribute);
            }
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
            pipelineDesc.rasterState.depthBiasEnable = true;
            pipelineDesc.rasterState.depthBiasConstantFactor = HeroSilhouetteDepthBiasConstant;
            pipelineDesc.rasterState.depthBiasSlopeFactor = HeroSilhouetteDepthBiasSlope;
            pipelineDesc.rasterState.lineWidth = 1.0f;
            pipelineDesc.depthStencilState.depthTestEnable = true;
            pipelineDesc.depthStencilState.depthWriteEnable = false;
            // 透視・正射影とも近いほど深度が小さいため、手前の面に隠れた画素だけ Greater で通す。
            pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Greater;
            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = true;
            blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
            blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
            blend.colorBlendOp = RHI::BlendOp::Add;
            blend.srcAlphaBlendFactor = RHI::BlendFactor::One;
            blend.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
            blend.alphaBlendOp = RHI::BlendOp::Add;
            blend.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blend);
            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
            m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            if (!m_Pipeline)
            {
                return false;
            }
            m_ColorFormat = color->GetFormat();
            m_DepthFormat = depth->GetFormat();
        }

        if (!m_Framebuffer || m_FramebufferColor != color.get() || m_FramebufferDepth != depth.get())
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            framebufferDesc.colorTargets.push_back(color);
            framebufferDesc.depthStencilTarget = depth;
            framebufferDesc.width = color->GetWidth();
            framebufferDesc.height = color->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferColor = m_Framebuffer ? color.get() : nullptr;
            m_FramebufferDepth = m_Framebuffer ? depth.get() : nullptr;
        }
        return m_Framebuffer != nullptr;
    }

    void HeroSilhouettePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        const CameraProxy* camera = context.GetActiveCamera();
        if (!m_bDeclared || !m_bInitialized || !context.CommandList || !context.SkinnedMeshes ||
            !context.SnapshotSkinnedMeshFrameLeases || !IsEnabledForCamera(camera) || !PrepareTargets(resources))
        {
            return;
        }

        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        SkinnedViewFrustum frustum;
        if (context.SnapshotScene && context.SnapshotScene->bSkinnedViewCulling)
        {
            frustum = MakeSkinnedViewFrustum(cameraConstants.ViewProjectionMatrix);
        }
        SilhouetteUniform frameUniform;
        cameraConstants.CopyShaderView(frameUniform.View);
        cameraConstants.CopyShaderProjection(frameUniform.Projection);
        MakeVertexAnimationTimeConstants(context.ExtensionFrame, context.bVertexAnimationTestDisplacement,
                                         frameUniform.VertexAnimTime);
        const float preExposure = ResolveSceneColorPreExposure(camera);
        for (uint32_t index = 0; index < 3; ++index)
        {
            frameUniform.Color[index] = m_Settings.Color[index] * preExposure;
        }
        frameUniform.Color[3] = m_Settings.Opacity;

        Container::VariableArray<PreparedSilhouetteDraw> draws;
        for (const DrawCommand& source : context.GetActiveOpaqueCommands())
        {
            if (source.Draw.PayloadKind != DrawPayloadKind::Skinned ||
                source.Draw.ObjectId != camera->ViewClearance.HeroObjectId || source.Draw.bInstanced ||
                source.Draw.InstanceCount != 1 || !IsSkinnedDrawInView(source, frustum) ||
                source.Skinned.FrameLeaseIndex >= context.SnapshotSkinnedMeshFrameLeases->size())
            {
                continue;
            }
            const auto& lease = (*context.SnapshotSkinnedMeshFrameLeases)[source.Skinned.FrameLeaseIndex];
            if (!lease || !lease->IsValid() ||
                (lease->ComponentId != 0 && lease->ComponentId != source.Draw.SourceMeshComponentId))
            {
                continue;
            }

            PreparedSilhouetteDraw draw;
            draw.Command = source;
            draw.Command.Skinned.PassKind = SkinnedMeshPassKind::Overlay;
            draw.Command.Skinned.FrameLease = lease;
            // 同じコンポーネントの GBuffer と現在のパレットを共有し、前のパレットは要求しない。
            if (!context.SkinnedMeshes->PrepareDraw(lease, source.Skinned.BonePalette, source.Draw.WorldMatrix,
                                                     draw.Command.Skinned.Prepared))
            {
                continue;
            }
            const auto& prepared = draw.Command.Skinned.Prepared;
            const auto& asset = lease->AssetLease;
            if (prepared.MeshHandle != asset->GetHandle() || prepared.IndexCount != asset->GetIndices().size() ||
                !Skeletal::ResolveSkeletalDrawRange({asset->GetSubMeshes().data(), asset->GetSubMeshes().size()},
                                                    asset->GetIndices().size(), source.Draw.SubMeshIndex,
                                                    source.Draw.MaterialIndex, source.Draw.IndexOffset,
                                                    source.Draw.IndexCount, source.Draw.VertexOffset,
                                                    false, source.Draw.bCastShadow, draw.Range))
            {
                continue;
            }
            auto allocation = m_UniformAllocator.Allocate(context.FrameResources);
            if (!allocation.IsValid())
            {
                NORVES_LOG_WARNING("HeroSilhouettePass", "定数を確保できないため残りの描画を飛ばします");
                break;
            }
            SilhouetteUniform uniform = frameUniform;
            std::memcpy(uniform.VertexAnimInstanceParams, source.Draw.ShadingParams,
                        sizeof(uniform.VertexAnimInstanceParams));
            allocation.Write(&uniform, sizeof(uniform));
            allocation.DescriptorSet->BindStorageBuffer(8, prepared.PaletteBuffer, 0,
                                                        static_cast<uint32_t>(prepared.PaletteBuffer->GetSize()));
            allocation.DescriptorSet->BindStorageBuffer(9, prepared.VertexBuffer, 0,
                                                        static_cast<uint32_t>(prepared.VertexBuffer->GetSize()));
            allocation.DescriptorSet->Update();
            if (!context.SkinnedMeshes->MarkLastUse(prepared, lease))
            {
                continue;
            }
            draw.DescriptorSet = allocation.DescriptorSet;
            draws.push_back(std::move(draw));
        }
        if (draws.empty())
        {
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        commandList->SetPipeline(m_Pipeline);
        for (const PreparedSilhouetteDraw& draw : draws)
        {
            const auto& prepared = draw.Command.Skinned.Prepared;
            commandList->SetDescriptorSet(draw.DescriptorSet, 0);
            commandList->SetVertexBuffer(prepared.VertexBuffer, 0, 0);
            commandList->SetIndexBuffer(prepared.IndexBuffer, 0, RHI::IndexType::Uint32);
            commandList->DrawIndexed(draw.Range.IndexCount, draw.Range.IndexOffset, 0);
        }
        commandList->EndRenderPass();
    }

    bool RegisterHeroSilhouetteExtension(RenderWorld& world)
    {
        RenderExtensionDesc desc;
        desc.Name = ExtensionName;
        desc.Stage = RenderExtensionStage::AfterLighting;
        return world.RegisterRenderExtension(desc, {CreateHeroSilhouettePass, nullptr});
    }

    bool UnregisterHeroSilhouetteExtension(RenderWorld& world)
    {
        return world.UnregisterRenderExtension(ExtensionName);
    }
}
