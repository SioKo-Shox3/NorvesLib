#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanShader.h"
#include "VulkanDescriptorSet.h"
#include <stdexcept>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    // VulkanPipelineLayoutの実装
    VulkanPipelineLayout::VulkanPipelineLayout(
        TSharedPtr<VulkanDevice> device,
        const VariableArray<TSharedPtr<VulkanDescriptorSetLayout>> &layouts)
        : m_device(device), m_descriptorSetLayouts(layouts)
    {
        // Vulkanレイアウト作成用のディスクリプタセットレイアウトハンドル配列
        VariableArray<vk::DescriptorSetLayout> vkLayouts;
        for (const auto &layout : layouts)
        {
            if (layout)
            {
                vkLayouts.push_back(layout->GetVkDescriptorSetLayout());
            }
        }

        // パイプラインレイアウト作成情報
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = static_cast<uint32_t>(vkLayouts.size()),
            .pSetLayouts = vkLayouts.empty() ? nullptr : vkLayouts.data(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr};

        // パイプラインレイアウト作成
        vk::Result result;
        std::tie(result, m_pipelineLayout) = m_device->GetVkDevice().createPipelineLayout(pipelineLayoutInfo);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("パイプラインレイアウトの作成に失敗しました");
        }
    }

    VulkanPipelineLayout::~VulkanPipelineLayout()
    {
        if (m_pipelineLayout)
        {
            m_device->GetVkDevice().destroyPipelineLayout(m_pipelineLayout);
        }
    }

    // VulkanPipelineの基底クラス実装
    VulkanPipeline::VulkanPipeline(TSharedPtr<VulkanDevice> device)
        : m_device(device)
    {
    }

    VulkanPipeline::~VulkanPipeline()
    {
        if (m_pipeline)
        {
            m_device->GetVkDevice().destroyPipeline(m_pipeline);
        }
    }

    // VulkanGraphicsPipelineの実装
    VulkanGraphicsPipeline::VulkanGraphicsPipeline(
        TSharedPtr<VulkanDevice> device,
        const GraphicsPipelineDesc &desc)
        : VulkanPipeline(device), m_desc(desc)
    {
        m_pipelineType = PipelineType::Graphics;
        CreateGraphicsPipeline();
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
    {
        // 基底クラスのデストラクタがパイプラインを破棄する
    }

    void VulkanGraphicsPipeline::CreateGraphicsPipeline()
    {
        // シェーダーステージ設定
        VariableArray<vk::PipelineShaderStageCreateInfo> shaderStages;

        // 頂点シェーダー
        if (m_desc.vertexShader)
        {
            auto vulkanShader = static_cast<VulkanShader *>(m_desc.vertexShader.get());
            vk::PipelineShaderStageCreateInfo vertStageInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = vulkanShader->GetVkShaderModule(),
                .pName = "main"};
            shaderStages.push_back(vertStageInfo);
        }

        // フラグメントシェーダー
        if (m_desc.fragmentShader)
        {
            auto vulkanShader = static_cast<VulkanShader *>(m_desc.fragmentShader.get());
            vk::PipelineShaderStageCreateInfo fragStageInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = vulkanShader->GetVkShaderModule(),
                .pName = "main"};
            shaderStages.push_back(fragStageInfo);
        }

        // オプショナルなシェーダー
        // ジオメトリシェーダー
        if (m_desc.geometryShader)
        {
            auto vulkanShader = static_cast<VulkanShader *>(m_desc.geometryShader.get());
            vk::PipelineShaderStageCreateInfo geomStageInfo{
                .stage = vk::ShaderStageFlagBits::eGeometry,
                .module = vulkanShader->GetVkShaderModule(),
                .pName = "main"};
            shaderStages.push_back(geomStageInfo);
        }

        // テッセレーションシェーダー
        if (m_desc.tessControlShader && m_desc.tessEvalShader)
        {
            auto vulkanTessControlShader = static_cast<VulkanShader *>(m_desc.tessControlShader.get());
            vk::PipelineShaderStageCreateInfo tessControlStageInfo{
                .stage = vk::ShaderStageFlagBits::eTessellationControl,
                .module = vulkanTessControlShader->GetVkShaderModule(),
                .pName = "main"};
            shaderStages.push_back(tessControlStageInfo);

            auto vulkanTessEvalShader = static_cast<VulkanShader *>(m_desc.tessEvalShader.get());
            vk::PipelineShaderStageCreateInfo tessEvalStageInfo{
                .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
                .module = vulkanTessEvalShader->GetVkShaderModule(),
                .pName = "main"};
            shaderStages.push_back(tessEvalStageInfo);
        }

        // 頂点バインディングの設定
        VariableArray<vk::VertexInputBindingDescription> bindingDescriptions;
        for (const auto &binding : m_desc.vertexBindings)
        {
            vk::VertexInputBindingDescription bindingDesc{
                .binding = binding.binding,
                .stride = binding.stride,
                .inputRate = binding.inputRate == VertexInputRate::Vertex ? vk::VertexInputRate::eVertex : vk::VertexInputRate::eInstance};
            bindingDescriptions.push_back(bindingDesc);
        }

        // 頂点属性の設定
        VariableArray<vk::VertexInputAttributeDescription> attributeDescriptions;
        for (const auto &attribute : m_desc.vertexAttributes)
        {
            vk::VertexInputAttributeDescription attributeDesc{
                .location = attribute.location,
                .binding = attribute.binding,
                .format = m_device->ToVkFormat(attribute.format),
                .offset = attribute.offset};
            attributeDescriptions.push_back(attributeDesc);
        }

        // 頂点入力の設定
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size()),
            .pVertexBindingDescriptions = bindingDescriptions.empty() ? nullptr : bindingDescriptions.data(),
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.empty() ? nullptr : attributeDescriptions.data()};

        // 入力アセンブリの設定
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .topology = ConvertPrimitiveTopology(m_desc.primitiveTopology),
            .primitiveRestartEnable = vk::False};

        // テッセレーション設定（テッセレーションシェーダーを使用する場合）
        vk::PipelineTessellationStateCreateInfo tessellationState{
            .patchControlPoints = m_desc.patchControlPoints};

        // ビューポートの設定（動的なビューポート使用）
        vk::PipelineViewportStateCreateInfo viewportState{
            .viewportCount = 1,
            .scissorCount = 1};

        // ラスタライザ設定
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable = m_desc.rasterState.depthClampEnable ? vk::True : vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = ConvertPolygonMode(m_desc.rasterState.polygonMode),
            .cullMode = ConvertCullMode(m_desc.rasterState.cullMode),
            .frontFace = ConvertFrontFace(m_desc.rasterState.frontFace),
            .depthBiasEnable = m_desc.rasterState.depthBiasEnable ? vk::True : vk::False,
            .depthBiasConstantFactor = m_desc.rasterState.depthBiasConstantFactor,
            .depthBiasClamp = m_desc.rasterState.depthBiasClamp,
            .depthBiasSlopeFactor = m_desc.rasterState.depthBiasSlopeFactor,
            .lineWidth = m_desc.rasterState.lineWidth};

        // マルチサンプリング設定
        vk::PipelineMultisampleStateCreateInfo multisampling{
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = vk::False,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = vk::False,
            .alphaToOneEnable = vk::False};

        // 深度ステンシル設定
        vk::StencilOpState frontStencil{
            .failOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.failOp),
            .passOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.passOp),
            .depthFailOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.depthFailOp),
            .compareOp = ConvertCompareOp(m_desc.depthStencilState.frontFace.compareOp),
            .compareMask = m_desc.depthStencilState.frontFace.compareMask,
            .writeMask = m_desc.depthStencilState.frontFace.writeMask,
            .reference = m_desc.depthStencilState.frontFace.reference};

        vk::StencilOpState backStencil{
            .failOp = ConvertStencilOp(m_desc.depthStencilState.backFace.failOp),
            .passOp = ConvertStencilOp(m_desc.depthStencilState.backFace.passOp),
            .depthFailOp = ConvertStencilOp(m_desc.depthStencilState.backFace.depthFailOp),
            .compareOp = ConvertCompareOp(m_desc.depthStencilState.backFace.compareOp),
            .compareMask = m_desc.depthStencilState.backFace.compareMask,
            .writeMask = m_desc.depthStencilState.backFace.writeMask,
            .reference = m_desc.depthStencilState.backFace.reference};

        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable = m_desc.depthStencilState.depthTestEnable ? vk::True : vk::False,
            .depthWriteEnable = m_desc.depthStencilState.depthWriteEnable ? vk::True : vk::False,
            .depthCompareOp = ConvertCompareOp(m_desc.depthStencilState.depthCompareOp),
            .depthBoundsTestEnable = m_desc.depthStencilState.depthBoundsTestEnable ? vk::True : vk::False,
            .stencilTestEnable = m_desc.depthStencilState.stencilTestEnable ? vk::True : vk::False,
            .front = frontStencil,
            .back = backStencil,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f};

        // カラーブレンド設定
        VariableArray<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        for (const auto &attachment : m_desc.blendState.attachments)
        {
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{
                .blendEnable = attachment.blendEnable ? vk::True : vk::False,
                .srcColorBlendFactor = ConvertBlendFactor(attachment.srcColorBlendFactor),
                .dstColorBlendFactor = ConvertBlendFactor(attachment.dstColorBlendFactor),
                .colorBlendOp = ConvertBlendOp(attachment.colorBlendOp),
                .srcAlphaBlendFactor = ConvertBlendFactor(attachment.srcAlphaBlendFactor),
                .dstAlphaBlendFactor = ConvertBlendFactor(attachment.dstAlphaBlendFactor),
                .alphaBlendOp = ConvertBlendOp(attachment.alphaBlendOp),
                .colorWriteMask = ConvertColorWriteMask(attachment.colorWriteMask)};
            colorBlendAttachments.push_back(colorBlendAttachment);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
            .pAttachments = colorBlendAttachments.data(),
            .blendConstants = {{m_desc.blendState.blendConstants[0],
                                m_desc.blendState.blendConstants[1],
                                m_desc.blendState.blendConstants[2],
                                m_desc.blendState.blendConstants[3]}}};

        // 動的ステート設定
        VariableArray<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor};

        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()};

        // パイプラインレイアウト
        // ディスクリプタセットレイアウトの収集
        VariableArray<TSharedPtr<VulkanDescriptorSetLayout>> descriptorSetLayouts;
        for (const auto &setDesc : m_desc.descriptorSetLayouts)
        {
            auto vulkanLayout = DynamicPointerCast<VulkanDescriptorSetLayout>(setDesc);
            if (vulkanLayout)
            {
                descriptorSetLayouts.push_back(vulkanLayout);
            }
        }

        // パイプラインレイアウトの作成
        m_pipelineLayout = MakeShared<VulkanPipelineLayout>(m_device, descriptorSetLayouts);

        // レンダーパスとサブパス設定
        vk::RenderPass renderPass;
        if (m_desc.renderPass)
        {
            auto vulkanRenderPass = static_cast<VulkanRenderPass *>(m_desc.renderPass.get());
            renderPass = vulkanRenderPass->GetVkRenderPass();
        }

        // グラフィックスパイプライン作成情報
        vk::GraphicsPipelineCreateInfo pipelineInfo{
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pTessellationState = (m_desc.tessControlShader && m_desc.tessEvalShader) ? &tessellationState : nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = m_pipelineLayout->GetVkPipelineLayout(),
            .renderPass = renderPass,
            .subpass = m_desc.subpass,
            .basePipelineHandle = nullptr,
            .basePipelineIndex = -1};

        // パイプライン作成
        vk::Result result;
        std::tie(result, m_pipeline) = m_device->GetVkDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("グラフィックスパイプラインの作成に失敗しました");
        }
    }

    // VulkanComputePipelineの実装
    VulkanComputePipeline::VulkanComputePipeline(
        TSharedPtr<VulkanDevice> device,
        const ComputePipelineDesc &desc)
        : VulkanPipeline(device), m_desc(desc)
    {
        m_pipelineType = PipelineType::Compute;
        CreateComputePipeline();
    }

    VulkanComputePipeline::~VulkanComputePipeline()
    {
        // 基底クラスのデストラクタがパイプラインを破棄する
    }

    void VulkanComputePipeline::CreateComputePipeline()
    {
        // コンピュートシェーダーは必須
        if (!m_desc.computeShader)
        {
            throw std::runtime_error("コンピュートシェーダーが指定されていません");
        }

        auto vulkanShader = static_cast<VulkanShader *>(m_desc.computeShader.get());

        // コンピュートシェーダーステージの設定
        vk::PipelineShaderStageCreateInfo computeShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = vulkanShader->GetVkShaderModule(),
            .pName = "main"};

        // ディスクリプタセットレイアウト
        VariableArray<TSharedPtr<VulkanDescriptorSetLayout>> descriptorSetLayouts;
        for (const auto &setDesc : m_desc.descriptorSetLayouts)
        {
            auto vulkanLayout = DynamicPointerCast<VulkanDescriptorSetLayout>(setDesc);
            if (vulkanLayout)
            {
                descriptorSetLayouts.push_back(vulkanLayout);
            }
        }

        // パイプラインレイアウトの作成
        m_pipelineLayout = MakeShared<VulkanPipelineLayout>(m_device, descriptorSetLayouts);

        // コンピュートパイプライン作成情報
        vk::ComputePipelineCreateInfo pipelineInfo{
            .stage = computeShaderStageInfo,
            .layout = m_pipelineLayout->GetVkPipelineLayout(),
            .basePipelineHandle = nullptr,
            .basePipelineIndex = -1};

        // パイプライン作成
        vk::Result result;
        std::tie(result, m_pipeline) = m_device->GetVkDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コンピュートパイプラインの作成に失敗しました");
        }
    }

    // 列挙型変換ヘルパーメソッド
    vk::PrimitiveTopology VulkanGraphicsPipeline::ConvertPrimitiveTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList:
            return vk::PrimitiveTopology::ePointList;
        case PrimitiveTopology::LineList:
            return vk::PrimitiveTopology::eLineList;
        case PrimitiveTopology::LineStrip:
            return vk::PrimitiveTopology::eLineStrip;
        case PrimitiveTopology::TriangleList:
            return vk::PrimitiveTopology::eTriangleList;
        case PrimitiveTopology::TriangleStrip:
            return vk::PrimitiveTopology::eTriangleStrip;
        case PrimitiveTopology::TriangleFan:
            return vk::PrimitiveTopology::eTriangleFan;
        case PrimitiveTopology::PatchList:
            return vk::PrimitiveTopology::ePatchList;
        default:
            return vk::PrimitiveTopology::eTriangleList;
        }
    }

    vk::PolygonMode VulkanGraphicsPipeline::ConvertPolygonMode(PolygonMode mode)
    {
        switch (mode)
        {
        case PolygonMode::Fill:
            return vk::PolygonMode::eFill;
        case PolygonMode::Line:
            return vk::PolygonMode::eLine;
        case PolygonMode::Point:
            return vk::PolygonMode::ePoint;
        default:
            return vk::PolygonMode::eFill;
        }
    }

    vk::CullModeFlags VulkanGraphicsPipeline::ConvertCullMode(CullMode mode)
    {
        switch (mode)
        {
        case CullMode::None:
            return vk::CullModeFlagBits::eNone;
        case CullMode::Front:
            return vk::CullModeFlagBits::eFront;
        case CullMode::Back:
            return vk::CullModeFlagBits::eBack;
        case CullMode::FrontAndBack:
            return vk::CullModeFlagBits::eFrontAndBack;
        default:
            return vk::CullModeFlagBits::eBack;
        }
    }

    vk::FrontFace VulkanGraphicsPipeline::ConvertFrontFace(FrontFace frontFace)
    {
        switch (frontFace)
        {
        case FrontFace::CounterClockwise:
            return vk::FrontFace::eCounterClockwise;
        case FrontFace::Clockwise:
            return vk::FrontFace::eClockwise;
        default:
            return vk::FrontFace::eCounterClockwise;
        }
    }

    vk::CompareOp VulkanGraphicsPipeline::ConvertCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:
            return vk::CompareOp::eNever;
        case CompareOp::Less:
            return vk::CompareOp::eLess;
        case CompareOp::Equal:
            return vk::CompareOp::eEqual;
        case CompareOp::LessOrEqual:
            return vk::CompareOp::eLessOrEqual;
        case CompareOp::Greater:
            return vk::CompareOp::eGreater;
        case CompareOp::NotEqual:
            return vk::CompareOp::eNotEqual;
        case CompareOp::GreaterOrEqual:
            return vk::CompareOp::eGreaterOrEqual;
        case CompareOp::Always:
            return vk::CompareOp::eAlways;
        default:
            return vk::CompareOp::eLess;
        }
    }

    vk::StencilOp VulkanGraphicsPipeline::ConvertStencilOp(StencilOp op)
    {
        switch (op)
        {
        case StencilOp::Keep:
            return vk::StencilOp::eKeep;
        case StencilOp::Zero:
            return vk::StencilOp::eZero;
        case StencilOp::Replace:
            return vk::StencilOp::eReplace;
        case StencilOp::IncrementAndClamp:
            return vk::StencilOp::eIncrementAndClamp;
        case StencilOp::DecrementAndClamp:
            return vk::StencilOp::eDecrementAndClamp;
        case StencilOp::Invert:
            return vk::StencilOp::eInvert;
        case StencilOp::IncrementAndWrap:
            return vk::StencilOp::eIncrementAndWrap;
        case StencilOp::DecrementAndWrap:
            return vk::StencilOp::eDecrementAndWrap;
        default:
            return vk::StencilOp::eKeep;
        }
    }

    vk::BlendFactor VulkanGraphicsPipeline::ConvertBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero:
            return vk::BlendFactor::eZero;
        case BlendFactor::One:
            return vk::BlendFactor::eOne;
        case BlendFactor::SrcColor:
            return vk::BlendFactor::eSrcColor;
        case BlendFactor::OneMinusSrcColor:
            return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::DstColor:
            return vk::BlendFactor::eDstColor;
        case BlendFactor::OneMinusDstColor:
            return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::SrcAlpha:
            return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::OneMinusSrcAlpha:
            return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::DstAlpha:
            return vk::BlendFactor::eDstAlpha;
        case BlendFactor::OneMinusDstAlpha:
            return vk::BlendFactor::eOneMinusDstAlpha;
        case BlendFactor::ConstantColor:
            return vk::BlendFactor::eConstantColor;
        case BlendFactor::OneMinusConstantColor:
            return vk::BlendFactor::eOneMinusConstantColor;
        case BlendFactor::ConstantAlpha:
            return vk::BlendFactor::eConstantAlpha;
        case BlendFactor::OneMinusConstantAlpha:
            return vk::BlendFactor::eOneMinusConstantAlpha;
        case BlendFactor::SrcAlphaSaturate:
            return vk::BlendFactor::eSrcAlphaSaturate;
        default:
            return vk::BlendFactor::eOne;
        }
    }

    vk::BlendOp VulkanGraphicsPipeline::ConvertBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:
            return vk::BlendOp::eAdd;
        case BlendOp::Subtract:
            return vk::BlendOp::eSubtract;
        case BlendOp::ReverseSubtract:
            return vk::BlendOp::eReverseSubtract;
        case BlendOp::Min:
            return vk::BlendOp::eMin;
        case BlendOp::Max:
            return vk::BlendOp::eMax;
        default:
            return vk::BlendOp::eAdd;
        }
    }

    vk::ColorComponentFlags VulkanGraphicsPipeline::ConvertColorWriteMask(ColorWriteMask mask)
    {
        vk::ColorComponentFlags result;
        if (mask & ColorWriteMask::R)
        {
            result |= vk::ColorComponentFlagBits::eR;
        }
        if (mask & ColorWriteMask::G)
        {
            result |= vk::ColorComponentFlagBits::eG;
        }
        if (mask & ColorWriteMask::B)
        {
            result |= vk::ColorComponentFlagBits::eB;
        }
        if (mask & ColorWriteMask::A)
        {
            result |= vk::ColorComponentFlagBits::eA;
        }
        return result;
    }

} // namespace NorvesLib::RHI::Vulkan
