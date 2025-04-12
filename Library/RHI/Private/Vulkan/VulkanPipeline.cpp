#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include "VulkanShader.h"
#include "VulkanRenderPass.h"
#include "VulkanDescriptorSet.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// グラフィックスパイプラインコンストラクタ
VulkanPipeline::VulkanPipeline(
    std::shared_ptr<VulkanDevice> device,
    const GraphicsPipelineDesc& desc,
    std::shared_ptr<VulkanRenderPass> renderPass,
    const std::vector<std::shared_ptr<VulkanShader>>& shaders,
    const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts)
    : m_device(device)
    , m_descriptorSetLayouts(descriptorSetLayouts)
    , m_isCompute(false)
{
    // パイプラインレイアウトの作成
    CreatePipelineLayout(descriptorSetLayouts);
    
    // グラフィックスパイプラインの作成
    CreateGraphicsPipeline(desc, renderPass, shaders);
}

// コンピュートパイプラインコンストラクタ
VulkanPipeline::VulkanPipeline(
    std::shared_ptr<VulkanDevice> device,
    const ComputePipelineDesc& desc,
    std::shared_ptr<VulkanShader> computeShader,
    const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts)
    : m_device(device)
    , m_descriptorSetLayouts(descriptorSetLayouts)
    , m_isCompute(true)
{
    // パイプラインレイアウトの作成
    CreatePipelineLayout(descriptorSetLayouts);
    
    // コンピュートパイプラインの作成
    CreateComputePipeline(desc, computeShader);
}

// デストラクタ
VulkanPipeline::~VulkanPipeline()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->GetVkDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->GetVkDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

// パイプラインレイアウトの作成
void VulkanPipeline::CreatePipelineLayout(const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts)
{
    // ディスクリプタセットレイアウトの配列を作成
    std::vector<VkDescriptorSetLayout> setLayouts;
    for (const auto& layout : descriptorSetLayouts) {
        setLayouts.push_back(layout->GetVkDescriptorSetLayout());
    }
    
    // プッシュコンスタント範囲（今回は使用しない）
    std::vector<VkPushConstantRange> pushConstantRanges;
    
    // パイプラインレイアウト作成情報を設定
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
    
    // パイプラインレイアウトを作成
    if (vkCreatePipelineLayout(m_device->GetVkDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("パイプラインレイアウトの作成に失敗しました");
    }
}

// グラフィックスパイプラインの作成
void VulkanPipeline::CreateGraphicsPipeline(
    const GraphicsPipelineDesc& desc,
    std::shared_ptr<VulkanRenderPass> renderPass,
    const std::vector<std::shared_ptr<VulkanShader>>& shaders)
{
    // シェーダーステージの設定
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    for (const auto& shader : shaders) {
        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = shader->GetVkShaderStageFlag();
        shaderStageInfo.module = shader->GetVkShaderModule();
        shaderStageInfo.pName = "main"; // エントリーポイント名
        
        shaderStages.push_back(shaderStageInfo);
    }
    
    // 頂点入力の設定
    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
    
    // 頂点バインディング記述子の設定
    for (const auto& binding : desc.vertexLayout.bindings) {
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = binding.binding;
        bindingDesc.stride = binding.stride;
        bindingDesc.inputRate = binding.instanceStepRate > 0 ? 
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        
        vertexBindingDescriptions.push_back(bindingDesc);
    }
    
    // 頂点属性記述子の設定
    for (const auto& attribute : desc.vertexLayout.attributes) {
        VkVertexInputAttributeDescription attribDesc{};
        attribDesc.binding = attribute.binding;
        attribDesc.location = attribute.location;
        attribDesc.format = ConvertToVkFormat(attribute.format);
        attribDesc.offset = attribute.offset;
        
        vertexAttributeDescriptions.push_back(attribDesc);
    }
    
    // 頂点入力ステージの設定
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();
    
    // 入力アセンブリステージ
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ConvertToVkPrimitiveTopology(desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // ビューポート状態
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // ラスタライザーステート
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = desc.rasterizerState.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = ConvertToVkPolygonMode(desc.rasterizerState.fillMode);
    rasterizer.cullMode = ConvertToVkCullMode(desc.rasterizerState.cullMode);
    rasterizer.frontFace = desc.rasterizerState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.rasterizerState.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = desc.rasterizerState.depthBias;
    rasterizer.depthBiasClamp = desc.rasterizerState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.rasterizerState.depthBiasSlope;
    rasterizer.lineWidth = 1.0f;
    
    // マルチサンプルステート
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  // 単一サンプリング
    
    // デプスステンシルステート
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencilState.depthEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencilState.depthWriteMask == DepthWriteMask::All ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ConvertToVkCompareOp(desc.depthStencilState.depthFunc);
    depthStencil.stencilTestEnable = desc.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
    
    // フロントフェイスのステンシル操作
    depthStencil.front.failOp = ConvertToVkStencilOp(desc.depthStencilState.frontFace.failOp);
    depthStencil.front.passOp = ConvertToVkStencilOp(desc.depthStencilState.frontFace.passOp);
    depthStencil.front.depthFailOp = ConvertToVkStencilOp(desc.depthStencilState.frontFace.depthFailOp);
    depthStencil.front.compareOp = ConvertToVkCompareOp(desc.depthStencilState.frontFace.stencilFunc);
    depthStencil.front.compareMask = desc.depthStencilState.stencilReadMask;
    depthStencil.front.writeMask = desc.depthStencilState.stencilWriteMask;
    depthStencil.front.reference = 0;
    
    // バックフェイスのステンシル操作（フロントと同じに設定）
    depthStencil.back.failOp = ConvertToVkStencilOp(desc.depthStencilState.backFace.failOp);
    depthStencil.back.passOp = ConvertToVkStencilOp(desc.depthStencilState.backFace.passOp);
    depthStencil.back.depthFailOp = ConvertToVkStencilOp(desc.depthStencilState.backFace.depthFailOp);
    depthStencil.back.compareOp = ConvertToVkCompareOp(desc.depthStencilState.backFace.stencilFunc);
    depthStencil.back.compareMask = desc.depthStencilState.stencilReadMask;
    depthStencil.back.writeMask = desc.depthStencilState.stencilWriteMask;
    depthStencil.back.reference = 0;
    
    // カラーブレンドステート
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& target : desc.blendState.renderTargets) {
        VkPipelineColorBlendAttachmentState attachmentState{};
        attachmentState.blendEnable = target.blendEnable ? VK_TRUE : VK_FALSE;
        attachmentState.srcColorBlendFactor = ConvertToVkBlendFactor(target.srcBlend);
        attachmentState.dstColorBlendFactor = ConvertToVkBlendFactor(target.destBlend);
        attachmentState.colorBlendOp = ConvertToVkBlendOp(target.blendOp);
        attachmentState.srcAlphaBlendFactor = ConvertToVkBlendFactor(target.srcBlendAlpha);
        attachmentState.dstAlphaBlendFactor = ConvertToVkBlendFactor(target.destBlendAlpha);
        attachmentState.alphaBlendOp = ConvertToVkBlendOp(target.blendOpAlpha);
        attachmentState.colorWriteMask = 
            (target.writeMask & ColorWriteMask::Red   ? VK_COLOR_COMPONENT_R_BIT : 0) |
            (target.writeMask & ColorWriteMask::Green ? VK_COLOR_COMPONENT_G_BIT : 0) |
            (target.writeMask & ColorWriteMask::Blue  ? VK_COLOR_COMPONENT_B_BIT : 0) |
            (target.writeMask & ColorWriteMask::Alpha ? VK_COLOR_COMPONENT_A_BIT : 0);
        
        colorBlendAttachments.push_back(attachmentState);
    }
    
    // レンダーパスのアタッチメント数が足りない場合、追加でブレンドステートを作成
    while (colorBlendAttachments.size() < renderPass->GetColorAttachmentCount()) {
        VkPipelineColorBlendAttachmentState attachmentState{};
        attachmentState.blendEnable = VK_FALSE;
        attachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments.push_back(attachmentState);
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;
    
    // 動的ステート
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();
    
    // グラフィックスパイプラインの作成情報
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass->GetVkRenderPass();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    // パイプラインの作成
    if (vkCreateGraphicsPipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("グラフィックスパイプラインの作成に失敗しました");
    }
}

// コンピュートパイプラインの作成
void VulkanPipeline::CreateComputePipeline(
    const ComputePipelineDesc& desc,
    std::shared_ptr<VulkanShader> computeShader)
{
    // シェーダーステージの設定
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShader->GetVkShaderModule();
    shaderStageInfo.pName = "main"; // エントリーポイント名
    
    // コンピュートパイプラインの作成情報
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    // パイプラインの作成
    if (vkCreateComputePipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("コンピュートパイプラインの作成に失敗しました");
    }
}

// フォーマット変換ヘルパー関数
VkFormat ConvertToVkFormat(Format format)
{
    switch (format)
    {
        case Format::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32G32B32_FLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32_FLOAT:       return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
        case Format::R8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::B8G8R8A8_UNORM:     return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
        default:                          return VK_FORMAT_UNDEFINED;
    }
}

// プリミティブトポロジー変換
VkPrimitiveTopology ConvertToVkPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default:                               return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// フィルモード変換
VkPolygonMode ConvertToVkPolygonMode(FillMode fillMode)
{
    switch (fillMode)
    {
        case FillMode::Wireframe: return VK_POLYGON_MODE_LINE;
        case FillMode::Solid:     return VK_POLYGON_MODE_FILL;
        default:                  return VK_POLYGON_MODE_FILL;
    }
}

// カリングモード変換
VkCullModeFlags ConvertToVkCullMode(CullMode cullMode)
{
    switch (cullMode)
    {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
        default:              return VK_CULL_MODE_BACK_BIT;
    }
}

// 比較演算子変換
VkCompareOp ConvertToVkCompareOp(ComparisonFunc func)
{
    switch (func)
    {
        case ComparisonFunc::Never:         return VK_COMPARE_OP_NEVER;
        case ComparisonFunc::Less:          return VK_COMPARE_OP_LESS;
        case ComparisonFunc::Equal:         return VK_COMPARE_OP_EQUAL;
        case ComparisonFunc::LessEqual:     return VK_COMPARE_OP_LESS_OR_EQUAL;
        case ComparisonFunc::Greater:       return VK_COMPARE_OP_GREATER;
        case ComparisonFunc::NotEqual:      return VK_COMPARE_OP_NOT_EQUAL;
        case ComparisonFunc::GreaterEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case ComparisonFunc::Always:        return VK_COMPARE_OP_ALWAYS;
        default:                            return VK_COMPARE_OP_LESS;
    }
}

// ステンシル操作変換
VkStencilOp ConvertToVkStencilOp(StencilOp op)
{
    switch (op)
    {
        case StencilOp::Keep:       return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:       return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:    return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrSat:    return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrSat:    return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:     return VK_STENCIL_OP_INVERT;
        case StencilOp::Incr:       return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::Decr:       return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:                    return VK_STENCIL_OP_KEEP;
    }
}

// ブレンドファクター変換
VkBlendFactor ConvertToVkBlendFactor(Blend blend)
{
    switch (blend)
    {
        case Blend::Zero:                 return VK_BLEND_FACTOR_ZERO;
        case Blend::One:                  return VK_BLEND_FACTOR_ONE;
        case Blend::SrcColor:             return VK_BLEND_FACTOR_SRC_COLOR;
        case Blend::InvSrcColor:          return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Blend::SrcAlpha:             return VK_BLEND_FACTOR_SRC_ALPHA;
        case Blend::InvSrcAlpha:          return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Blend::DestAlpha:            return VK_BLEND_FACTOR_DST_ALPHA;
        case Blend::InvDestAlpha:         return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case Blend::DestColor:            return VK_BLEND_FACTOR_DST_COLOR;
        case Blend::InvDestColor:         return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case Blend::SrcAlphaSat:          return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case Blend::BlendFactor:          return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case Blend::InvBlendFactor:       return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        default:                          return VK_BLEND_FACTOR_ONE;
    }
}

// ブレンド操作変換
VkBlendOp ConvertToVkBlendOp(BlendOperation blendOp)
{
    switch (blendOp)
    {
        case BlendOperation::Add:         return VK_BLEND_OP_ADD;
        case BlendOperation::Subtract:    return VK_BLEND_OP_SUBTRACT;
        case BlendOperation::RevSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOperation::Min:         return VK_BLEND_OP_MIN;
        case BlendOperation::Max:         return VK_BLEND_OP_MAX;
        default:                          return VK_BLEND_OP_ADD;
    }
}

} // namespace NorvesLib::RHI::Vulkan