#include "Rendering/RibbonTrailRenderer.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ShaderManager.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IRenderPass.h"
#include "Logging/LogMacros.h"
#include <cstddef>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct RibbonTrailUBO
        {
            float view[16];
            float projection[16];
            // x: シーンの事前露出、y: 加算なら 1（RGB に不透明度を掛ける）。
            float sceneColorParams[4];
        };

        static_assert(offsetof(RibbonTrailUBO, projection) == 64);
        static_assert(offsetof(RibbonTrailUBO, sceneColorParams) == 128);
        static_assert(sizeof(RibbonTrailUBO) == 144);

        RHI::DescriptorSetDesc CreateDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding binding;
            binding.binding = 0;
            binding.type = RHI::ResourceBindType::ConstantBuffer;
            binding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            desc.bindings.push_back(binding);
            return desc;
        }
    } // 無名名前空間

    bool RibbonTrailRenderer::Initialize(RHI::IDevice* device, ShaderManager* shaderManager,
                                         const RHI::RenderPassPtr& renderPass)
    {
        Shutdown();
        if (!device || !shaderManager || !renderPass ||
            renderPass->GetColorAttachmentCount() != 1 || !renderPass->HasDepthStencilAttachment())
        {
            NORVES_LOG_ERROR("RibbonTrailRenderer", "帯の描画に必要なデバイス・シェーダー管理・色と深度の描画先がありません");
            return false;
        }

        m_VertexShader = shaderManager->LoadShader("ribbon_trail.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = shaderManager->LoadShader("ribbon_trail.frag", RHI::ShaderStage::Pixel);
        const RHI::DescriptorSetDesc descriptorDesc = CreateDescriptorSetDesc();
        m_DescriptorSetLayout = device->GetOrCreateDescriptorSetLayout(descriptorDesc);
        if (!m_VertexShader || !m_FragmentShader || !m_DescriptorSetLayout)
        {
            NORVES_LOG_ERROR("RibbonTrailRenderer", "帯のシェーダーまたは記述子のレイアウトを作れませんでした");
            Shutdown();
            return false;
        }

        RHI::GraphicsPipelineDesc desc;
        desc.vertexShader = m_VertexShader;
        desc.pixelShader = m_FragmentShader;
        desc.primitiveTopology = RHI::PrimitiveTopology::TriangleStrip;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(RibbonTrailVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        desc.vertexBindings.push_back(vertexBinding);

        RHI::VertexAttributeDesc attribute;
        attribute.binding = 0;
        attribute.location = 0;
        attribute.format = RHI::Format::R32G32B32_FLOAT;
        attribute.offset = offsetof(RibbonTrailVertex, Position);
        desc.vertexAttributes.push_back(attribute);
        attribute.location = 1;
        attribute.format = RHI::Format::R32G32_FLOAT;
        attribute.offset = offsetof(RibbonTrailVertex, UV);
        desc.vertexAttributes.push_back(attribute);
        attribute.location = 2;
        attribute.format = RHI::Format::R32G32B32A32_FLOAT;
        attribute.offset = offsetof(RibbonTrailVertex, Color);
        desc.vertexAttributes.push_back(attribute);

        desc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        desc.rasterState.cullMode = RHI::CullMode::None;
        desc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        desc.rasterState.lineWidth = 1.0f;
        desc.depthStencilState.depthTestEnable = true;
        desc.depthStencilState.depthWriteEnable = false;
        desc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;
        desc.renderPass = renderPass;
        desc.descriptorSetLayouts.push_back(descriptorDesc);

        // 加算の RGB は断片シェーダーで A を掛けるので、ここでは One + One で混ぜる。
        RHI::BlendAttachmentDesc blend;
        blend.blendEnable = true;
        blend.srcColorBlendFactor = RHI::BlendFactor::One;
        blend.dstColorBlendFactor = RHI::BlendFactor::One;
        blend.colorBlendOp = RHI::BlendOp::Add;
        blend.srcAlphaBlendFactor = RHI::BlendFactor::One;
        blend.dstAlphaBlendFactor = RHI::BlendFactor::One;
        blend.alphaBlendOp = RHI::BlendOp::Add;
        blend.colorWriteMask = RHI::ColorWriteMask::All;
        desc.blendState.attachments.push_back(blend);
        m_AdditivePipeline = device->CreateGraphicsPipeline(desc);

        // アルファの RGB は事前乗算せず、混合で SrcAlpha を掛ける。
        blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blend.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        desc.blendState.attachments[0] = blend;
        m_AlphaPipeline = device->CreateGraphicsPipeline(desc);
        if (!IsInitialized())
        {
            NORVES_LOG_ERROR("RibbonTrailRenderer", "帯の描画パイプラインを作れませんでした");
            Shutdown();
            return false;
        }
        return true;
    }

    void RibbonTrailRenderer::Shutdown()
    {
        m_AdditivePipeline.reset();
        m_AlphaPipeline.reset();
        m_DescriptorSetLayout.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
    }

    bool RibbonTrailRenderer::IsInitialized() const
    {
        return m_AdditivePipeline && m_AlphaPipeline && m_DescriptorSetLayout;
    }

    bool RibbonTrailRenderer::Record(RHI::ICommandList* commandList, FrameResourceLane* frameResources,
                                     const RibbonTrailScene& scene, const float (&view)[16],
                                     const float (&projection)[16], float preExposure) const
    {
        if (scene.IsEmpty())
        {
            return true;
        }
        if (!commandList || !frameResources || !IsInitialized() || scene.Vertices.empty() ||
            scene.Vertices.size() > UINT32_MAX)
        {
            return false;
        }

        bool bUsedBlend[2] = {};
        for (const RibbonTrailDraw& draw : scene.Draws)
        {
            if (draw.VertexCount < 3 || draw.FirstVertex > scene.Vertices.size() ||
                draw.VertexCount > scene.Vertices.size() - draw.FirstVertex ||
                (draw.Blend != RibbonTrailBlend::Additive && draw.Blend != RibbonTrailBlend::Alpha))
            {
                NORVES_LOG_ERROR("RibbonTrailRenderer", "帯の頂点範囲または混合方式が不正です");
                return false;
            }
            bUsedBlend[draw.Blend == RibbonTrailBlend::Additive ? 0 : 1] = true;
        }

        const FrameUploadAllocation vertices = frameResources->AllocateUploadData(
            scene.Vertices.data(), static_cast<uint64_t>(scene.Vertices.size()) * sizeof(RibbonTrailVertex),
            FrameUploadUsage::Vertex);
        if (!vertices.IsValid())
        {
            return false;
        }

        RibbonTrailUBO ubo{};
        std::memcpy(ubo.view, view, sizeof(view));
        std::memcpy(ubo.projection, projection, sizeof(projection));
        ubo.sceneColorParams[0] = preExposure;
        RHI::DescriptorSetPtr descriptorSets[2];
        for (uint32_t blendIndex = 0; blendIndex < 2; ++blendIndex)
        {
            if (!bUsedBlend[blendIndex])
            {
                continue;
            }
            ubo.sceneColorParams[1] = blendIndex == 0 ? 1.0f : 0.0f;
            const FrameUploadAllocation constants = frameResources->AllocateConstants(ubo);
            const FrameDescriptorAllocation descriptor = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
            if (!constants.IsValid() || !descriptor.IsValid())
            {
                return false;
            }
            // 定数の切れ端は持ち主が記述子の 32 ビットの範囲に収める。
            descriptor.Set->BindConstantBuffer(0, constants.Buffer,
                                               static_cast<uint32_t>(constants.Offset),
                                               static_cast<uint32_t>(constants.Size));
            descriptor.Set->Update();
            descriptorSets[blendIndex] = descriptor.Set;
        }

        commandList->SetVertexBuffer(vertices.Buffer, vertices.Offset, 0);
        for (const RibbonTrailDraw& draw : scene.Draws)
        {
            const bool bAdditive = draw.Blend == RibbonTrailBlend::Additive;
            commandList->SetPipeline(bAdditive ? m_AdditivePipeline : m_AlphaPipeline);
            commandList->SetDescriptorSet(descriptorSets[bAdditive ? 0 : 1], 0);
            commandList->Draw(draw.VertexCount, draw.FirstVertex);
        }
        return true;
    }
} // 名前空間 NorvesLib::Core::Rendering
