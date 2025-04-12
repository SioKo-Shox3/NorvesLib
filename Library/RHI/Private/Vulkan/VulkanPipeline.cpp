#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanShader.h"
#include "VulkanDescriptorSet.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// VulkanPipelineLayoutの実装
VulkanPipelineLayout::VulkanPipelineLayout(
    std::shared_ptr<VulkanDevice> device,
    const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& layouts)
    : m_device(device), m_descriptorSetLayouts(layouts)
{
    // Vulkanレイアウト作成用のディスクリプタセットレイアウトハンドル配列
    std::vector<VkDescriptorSetLayout> vkLayouts;
    for (const auto& layout : layouts) {
        if (layout) {
            vkLayouts.push_back(layout->GetVkDescriptorSetLayout());
        }
    }
    
    // パイプラインレイアウト作成情報
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(vkLayouts.size());
    pipelineLayoutInfo.pSetLayouts = vkLayouts.empty() ? nullptr : vkLayouts.data();
    
    // プッシュ定数ブロックの設定（ここでは簡略化のために未実装）
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    
    // パイプラインレイアウト作成
    if (vkCreatePipelineLayout(m_device->GetVkDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("パイプラインレイアウトの作成に失敗しました");
    }
}

VulkanPipelineLayout::~VulkanPipelineLayout()
{
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->GetVkDevice(), m_pipelineLayout, nullptr);
    }
}

// VulkanPipelineの基底クラス実装
VulkanPipeline::VulkanPipeline(std::shared_ptr<VulkanDevice> device)
    : m_device(device)
{
}

VulkanPipeline::~VulkanPipeline()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->GetVkDevice(), m_pipeline, nullptr);
    }
}

// VulkanGraphicsPipelineの実装
VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    std::shared_ptr<VulkanDevice> device,
    const GraphicsPipelineDesc& desc)
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
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    
    // 頂点シェーダー
    if (m_desc.vertexShader) {
        auto vulkanShader = static_cast<VulkanShader*>(m_desc.vertexShader.get());
        VkPipelineShaderStageCreateInfo vertStageInfo{};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vulkanShader->GetVkShaderModule();
        vertStageInfo.pName = "main"; // エントリポイント
        shaderStages.push_back(vertStageInfo);
    }
    
    // フラグメントシェーダー
    if (m_desc.fragmentShader) {
        auto vulkanShader = static_cast<VulkanShader*>(m_desc.fragmentShader.get());
        VkPipelineShaderStageCreateInfo fragStageInfo{};
        fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module = vulkanShader->GetVkShaderModule();
        fragStageInfo.pName = "main"; // エントリポイント
        shaderStages.push_back(fragStageInfo);
    }
    
    // オプショナルなシェーダー
    // ジオメトリシェーダー
    if (m_desc.geometryShader) {
        auto vulkanShader = static_cast<VulkanShader*>(m_desc.geometryShader.get());
        VkPipelineShaderStageCreateInfo geomStageInfo{};
        geomStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        geomStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        geomStageInfo.module = vulkanShader->GetVkShaderModule();
        geomStageInfo.pName = "main"; // エントリポイント
        shaderStages.push_back(geomStageInfo);
    }
    
    // テッセレーションシェーダー
    if (m_desc.tessControlShader && m_desc.tessEvalShader) {
        auto vulkanTessControlShader = static_cast<VulkanShader*>(m_desc.tessControlShader.get());
        VkPipelineShaderStageCreateInfo tessControlStageInfo{};
        tessControlStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tessControlStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        tessControlStageInfo.module = vulkanTessControlShader->GetVkShaderModule();
        tessControlStageInfo.pName = "main"; // エントリポイント
        shaderStages.push_back(tessControlStageInfo);
        
        auto vulkanTessEvalShader = static_cast<VulkanShader*>(m_desc.tessEvalShader.get());
        VkPipelineShaderStageCreateInfo tessEvalStageInfo{};
        tessEvalStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tessEvalStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        tessEvalStageInfo.module = vulkanTessEvalShader->GetVkShaderModule();
        tessEvalStageInfo.pName = "main"; // エントリポイント
        shaderStages.push_back(tessEvalStageInfo);
    }
    
    // 頂点入力の設定
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    // 頂点バインディングの設定
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    for (const auto& binding : m_desc.vertexBindings) {
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = binding.binding;
        bindingDesc.stride = binding.stride;
        bindingDesc.inputRate = binding.inputRate == VertexInputRate::Vertex ? 
                                VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
        bindingDescriptions.push_back(bindingDesc);
    }
    
    // 頂点属性の設定
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    for (const auto& attribute : m_desc.vertexAttributes) {
        VkVertexInputAttributeDescription attributeDesc{};
        attributeDesc.binding = attribute.binding;
        attributeDesc.location = attribute.location;
        attributeDesc.format = m_device->ToVkFormat(attribute.format);
        attributeDesc.offset = attribute.offset;
        attributeDescriptions.push_back(attributeDesc);
    }
    
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.empty() ? nullptr : bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.empty() ? nullptr : attributeDescriptions.data();
    
    // 入力アセンブリの設定
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ConvertPrimitiveTopology(m_desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // テッセレーション設定（テッセレーションシェーダーを使用する場合）
    VkPipelineTessellationStateCreateInfo tessellationState{};
    if (m_desc.tessControlShader && m_desc.tessEvalShader) {
        tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellationState.patchControlPoints = m_desc.patchControlPoints;
    }
    
    // ビューポートの設定（動的なビューポート使用）
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; // 動的に設定するため、最低1つは必要
    viewportState.scissorCount = 1;  // 同上
    
    // ラスタライザ設定
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = m_desc.rasterState.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = ConvertPolygonMode(m_desc.rasterState.polygonMode);
    rasterizer.cullMode = ConvertCullMode(m_desc.rasterState.cullMode);
    rasterizer.frontFace = ConvertFrontFace(m_desc.rasterState.frontFace);
    rasterizer.depthBiasEnable = m_desc.rasterState.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = m_desc.rasterState.depthBiasConstantFactor;
    rasterizer.depthBiasClamp = m_desc.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = m_desc.rasterState.depthBiasSlopeFactor;
    rasterizer.lineWidth = m_desc.rasterState.lineWidth;
    
    // マルチサンプリング設定
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // 現在はMSAAなしで固定
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;
    
    // 深度ステンシル設定
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_desc.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ConvertCompareOp(m_desc.depthStencilState.depthCompareOp);
    depthStencil.depthBoundsTestEnable = m_desc.depthStencilState.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    
    // ステンシルテスト設定
    depthStencil.stencilTestEnable = m_desc.depthStencilState.stencilTestEnable ? VK_TRUE : VK_FALSE;
    
    // 表面ステンシル設定
    depthStencil.front.failOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.failOp);
    depthStencil.front.passOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.passOp);
    depthStencil.front.depthFailOp = ConvertStencilOp(m_desc.depthStencilState.frontFace.depthFailOp);
    depthStencil.front.compareOp = ConvertCompareOp(m_desc.depthStencilState.frontFace.compareOp);
    depthStencil.front.compareMask = m_desc.depthStencilState.frontFace.compareMask;
    depthStencil.front.writeMask = m_desc.depthStencilState.frontFace.writeMask;
    depthStencil.front.reference = m_desc.depthStencilState.frontFace.reference;
    
    // 裏面ステンシル設定
    depthStencil.back.failOp = ConvertStencilOp(m_desc.depthStencilState.backFace.failOp);
    depthStencil.back.passOp = ConvertStencilOp(m_desc.depthStencilState.backFace.passOp);
    depthStencil.back.depthFailOp = ConvertStencilOp(m_desc.depthStencilState.backFace.depthFailOp);
    depthStencil.back.compareOp = ConvertCompareOp(m_desc.depthStencilState.backFace.compareOp);
    depthStencil.back.compareMask = m_desc.depthStencilState.backFace.compareMask;
    depthStencil.back.writeMask = m_desc.depthStencilState.backFace.writeMask;
    depthStencil.back.reference = m_desc.depthStencilState.backFace.reference;
    
    // カラーブレンド設定
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& attachment : m_desc.blendState.attachments) {
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = attachment.blendEnable ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = ConvertBlendFactor(attachment.srcColorBlendFactor);
        colorBlendAttachment.dstColorBlendFactor = ConvertBlendFactor(attachment.dstColorBlendFactor);
        colorBlendAttachment.colorBlendOp = ConvertBlendOp(attachment.colorBlendOp);
        colorBlendAttachment.srcAlphaBlendFactor = ConvertBlendFactor(attachment.srcAlphaBlendFactor);
        colorBlendAttachment.dstAlphaBlendFactor = ConvertBlendFactor(attachment.dstAlphaBlendFactor);
        colorBlendAttachment.alphaBlendOp = ConvertBlendOp(attachment.alphaBlendOp);
        colorBlendAttachment.colorWriteMask = ConvertColorWriteMask(attachment.colorWriteMask);
        colorBlendAttachments.push_back(colorBlendAttachment);
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = m_desc.blendState.blendConstants[0];
    colorBlending.blendConstants[1] = m_desc.blendState.blendConstants[1];
    colorBlending.blendConstants[2] = m_desc.blendState.blendConstants[2];
    colorBlending.blendConstants[3] = m_desc.blendState.blendConstants[3];
    
    // 動的ステート設定
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // パイプラインレイアウト
    // ディスクリプタセットレイアウトの収集
    std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> descriptorSetLayouts;
    for (const auto& setDesc : m_desc.descriptorSetLayouts) {
        auto vulkanLayout = std::dynamic_pointer_cast<VulkanDescriptorSetLayout>(setDesc);
        if (vulkanLayout) {
            descriptorSetLayouts.push_back(vulkanLayout);
        }
    }
    
    // パイプラインレイアウトの作成
    m_pipelineLayout = std::make_shared<VulkanPipelineLayout>(m_device, descriptorSetLayouts);
    
    // レンダーパスとサブパス設定
    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (m_desc.renderPass) {
        auto vulkanRenderPass = static_cast<VulkanRenderPass*>(m_desc.renderPass.get());
        renderPass = vulkanRenderPass->GetVkRenderPass();
    }
    
    // グラフィックスパイプライン作成情報
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = m_desc.tessControlShader && m_desc.tessEvalShader ? &tessellationState : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout->GetVkPipelineLayout();
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = m_desc.subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    
    // パイプライン作成
    if (vkCreateGraphicsPipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("グラフィックスパイプラインの作成に失敗しました");
    }
}

// VulkanComputePipelineの実装
VulkanComputePipeline::VulkanComputePipeline(
    std::shared_ptr<VulkanDevice> device,
    const ComputePipelineDesc& desc)
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
    // コンピュートシェーダーステージの設定
    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // コンピュートシェーダーは必須
    if (!m_desc.computeShader) {
        throw std::runtime_error("コンピュートシェーダーが指定されていません");
    }
    
    auto vulkanShader = static_cast<VulkanShader*>(m_desc.computeShader.get());
    computeShaderStageInfo.module = vulkanShader->GetVkShaderModule();
    computeShaderStageInfo.pName = "main"; // エントリポイント
    
    // ディスクリプタセットレイアウト
    std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> descriptorSetLayouts;
    for (const auto& setDesc : m_desc.descriptorSetLayouts) {
        auto vulkanLayout = std::dynamic_pointer_cast<VulkanDescriptorSetLayout>(setDesc);
        if (vulkanLayout) {
            descriptorSetLayouts.push_back(vulkanLayout);
        }
    }
    
    // パイプラインレイアウトの作成
    m_pipelineLayout = std::make_shared<VulkanPipelineLayout>(m_device, descriptorSetLayouts);
    
    // コンピュートパイプライン作成情報
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = m_pipelineLayout->GetVkPipelineLayout();
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    
    // パイプライン作成
    if (vkCreateComputePipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("コンピュートパイプラインの作成に失敗しました");
    }
}

// 列挙型変換ヘルパーメソッド
VkPrimitiveTopology VulkanGraphicsPipeline::ConvertPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology) {
        case PrimitiveTopology::PointList:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PrimitiveTopology::PatchList:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkPolygonMode VulkanGraphicsPipeline::ConvertPolygonMode(PolygonMode mode)
{
    switch (mode) {
        case PolygonMode::Fill:
            return VK_POLYGON_MODE_FILL;
        case PolygonMode::Line:
            return VK_POLYGON_MODE_LINE;
        case PolygonMode::Point:
            return VK_POLYGON_MODE_POINT;
        default:
            return VK_POLYGON_MODE_FILL;
    }
}

VkCullModeFlags VulkanGraphicsPipeline::ConvertCullMode(CullMode mode)
{
    switch (mode) {
        case CullMode::None:
            return VK_CULL_MODE_NONE;
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack:
            return VK_CULL_MODE_FRONT_AND_BACK;
        default:
            return VK_CULL_MODE_BACK_BIT;
    }
}

VkFrontFace VulkanGraphicsPipeline::ConvertFrontFace(FrontFace frontFace)
{
    switch (frontFace) {
        case FrontFace::CounterClockwise:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case FrontFace::Clockwise:
            return VK_FRONT_FACE_CLOCKWISE;
        default:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

VkCompareOp VulkanGraphicsPipeline::ConvertCompareOp(CompareOp op)
{
    switch (op) {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_LESS;
    }
}

VkStencilOp VulkanGraphicsPipeline::ConvertStencilOp(StencilOp op)
{
    switch (op) {
        case StencilOp::Keep:
            return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrementAndClamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementAndClamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:
            return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrementAndWrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementAndWrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            return VK_STENCIL_OP_KEEP;
    }
}

VkBlendFactor VulkanGraphicsPipeline::ConvertBlendFactor(BlendFactor factor)
{
    switch (factor) {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default:
            return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp VulkanGraphicsPipeline::ConvertBlendOp(BlendOp op)
{
    switch (op) {
        case BlendOp::Add:
            return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return VK_BLEND_OP_MIN;
        case BlendOp::Max:
            return VK_BLEND_OP_MAX;
        default:
            return VK_BLEND_OP_ADD;
    }
}

VkColorComponentFlags VulkanGraphicsPipeline::ConvertColorWriteMask(ColorWriteMask mask)
{
    VkColorComponentFlags result = 0;
    if (mask & ColorWriteMask::R) result |= VK_COLOR_COMPONENT_R_BIT;
    if (mask & ColorWriteMask::G) result |= VK_COLOR_COMPONENT_G_BIT;
    if (mask & ColorWriteMask::B) result |= VK_COLOR_COMPONENT_B_BIT;
    if (mask & ColorWriteMask::A) result |= VK_COLOR_COMPONENT_A_BIT;
    return result;
}

} // namespace NorvesLib::RHI::Vulkan