#include "VulkanDescriptorSet.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include <stdexcept>
#include <array>

namespace NorvesLib::RHI::Vulkan
{

//------------------------------------------------------------------------------
// VulkanDescriptorSetLayout
//------------------------------------------------------------------------------

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
    std::shared_ptr<VulkanDevice> device,
    const std::vector<DescriptorBindingDesc>& bindings)
    : m_device(device)
    , m_bindings(bindings)
{
    // バインディング情報の構築
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(bindings.size());
    
    for (const auto& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = ToVkDescriptorType(binding.type);
        layoutBinding.descriptorCount = binding.count;
        layoutBinding.stageFlags = ToVkShaderStageFlags(binding.stage);
        layoutBinding.pImmutableSamplers = nullptr;
        
        layoutBindings.push_back(layoutBinding);
    }
    
    // レイアウト作成情報の設定
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();
    
    // ディスクリプタセットレイアウトの作成
    if (vkCreateDescriptorSetLayout(m_device->GetVkDevice(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanディスクリプタセットレイアウトの作成に失敗しました");
    }
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->GetVkDevice(), m_layout, nullptr);
    }
}

// バインディングタイプをVulkanディスクリプタタイプに変換
VkDescriptorType VulkanDescriptorSetLayout::ToVkDescriptorType(DescriptorType type) const
{
    switch (type)
    {
    case DescriptorType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorType::ShaderResource:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case DescriptorType::UnorderedAccess:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case DescriptorType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    default:
        throw std::runtime_error("未対応のディスクリプタタイプです");
    }
}

// シェーダーステージをVulkanシェーダーステージフラグに変換
VkShaderStageFlags VulkanDescriptorSetLayout::ToVkShaderStageFlags(ShaderStage stage) const
{
    switch (stage)
    {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Hull:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderStage::Domain:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case ShaderStage::Geometry:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderStage::Pixel:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    case ShaderStage::All:
        return VK_SHADER_STAGE_ALL;
    default:
        throw std::runtime_error("未対応のシェーダーステージです");
    }
}

//------------------------------------------------------------------------------
// VulkanDescriptorPool
//------------------------------------------------------------------------------

VulkanDescriptorPool::VulkanDescriptorPool(std::shared_ptr<VulkanDevice> device, uint32_t maxSets)
    : m_device(device)
{
    // プールサイズを設定（各タイプに対して最大数を設定）
    std::array<VkDescriptorPoolSize, 5> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxSets * 8;  // 各セットに8つのUBOまで
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[1].descriptorCount = maxSets * 16; // 各セットに16のテクスチャまで
    
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = maxSets * 8;  // 各セットに8つのSSBOまで
    
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[3].descriptorCount = maxSets * 8;  // 各セットに8つのサンプラーまで
    
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[4].descriptorCount = maxSets * 8;  // 各セットに8つのストレージイメージまで
    
    // プール作成情報の設定
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // 個別に解放可能
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    // ディスクリプタプールの作成
    if (vkCreateDescriptorPool(m_device->GetVkDevice(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanディスクリプタプールの作成に失敗しました");
    }
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->GetVkDevice(), m_pool, nullptr);
    }
}

// プールのリセット
void VulkanDescriptorPool::Reset()
{
    vkResetDescriptorPool(m_device->GetVkDevice(), m_pool, 0);
}

//------------------------------------------------------------------------------
// VulkanDescriptorSet
//------------------------------------------------------------------------------

VulkanDescriptorSet::VulkanDescriptorSet(
    std::shared_ptr<VulkanDevice> device,
    const DescriptorSetDesc& desc,
    std::shared_ptr<VulkanDescriptorSetLayout> layout,
    std::shared_ptr<VulkanDescriptorPool> pool)
    : m_device(device)
    , m_desc(desc)
    , m_layout(layout)
    , m_pool(pool)
{
    // ディスクリプタセット割り当て情報の設定
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool->GetVkDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    
    // レイアウトを設定
    VkDescriptorSetLayout layout_handle = m_layout->GetVkDescriptorSetLayout();
    allocInfo.pSetLayouts = &layout_handle;
    
    // ディスクリプタセットの割り当て
    if (vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanディスクリプタセットの割り当てに失敗しました");
    }
    
    // パイプラインレイアウトの作成
    CreatePipelineLayout();
}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
    // ディスクリプタセットはプールと共に破棄されるので個別解放は不要
    
    // パイプラインレイアウトの破棄
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->GetVkDevice(), m_pipelineLayout, nullptr);
    }
}

void VulkanDescriptorSet::SetConstantBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetShaderResourceBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetUnorderedAccessBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetTexture(uint32_t binding, TexturePtr texture)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Texture;
    info.texture = texture;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetStorageTexture(uint32_t binding, TexturePtr texture)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Texture;
    info.texture = texture;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetSampler(uint32_t binding, SamplerPtr sampler)
{
    // バインディング情報を更新
    BindingInfo info;
    info.type = BindingInfo::ResourceType::Sampler;
    info.sampler = sampler;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::Update()
{
    // 更新が不要な場合は早期リターン
    if (!m_needsUpdate) {
        return;
    }
    
    // 書き込み情報のリスト
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    
    // バッファ記述子用の情報
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    
    // イメージ記述子用の情報
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    // 各バインディングの処理
    for (const auto& [binding, info] : m_bindings) {
        VkWriteDescriptorSet writeDesc{};
        writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDesc.dstSet = m_descriptorSet;
        writeDesc.dstBinding = binding;
        writeDesc.dstArrayElement = 0; // 配列の最初の要素
        writeDesc.descriptorCount = 1; // 1つの記述子
        writeDesc.descriptorType = GetVkDescriptorType(binding);
        
        // リソースタイプに応じた処理
        switch (info.type) {
            case BindingInfo::ResourceType::Buffer: {
                // バッファの場合
                auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(info.bufferInfo.buffer);
                if (!vulkanBuffer) {
                    throw std::runtime_error("無効なバッファが指定されました");
                }
                
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = vulkanBuffer->GetVkBuffer();
                bufferInfo.offset = info.bufferInfo.offset;
                bufferInfo.range = (info.bufferInfo.range == 0) ? 
                    VK_WHOLE_SIZE : info.bufferInfo.range;
                
                bufferInfos.push_back(bufferInfo);
                writeDesc.pBufferInfo = &bufferInfos.back();
                break;
            }
            
            case BindingInfo::ResourceType::Texture: {
                // テクスチャの場合
                auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(info.texture);
                if (!vulkanTexture) {
                    throw std::runtime_error("無効なテクスチャが指定されました");
                }
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = (writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ?
                    VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = vulkanTexture->GetVkImageView();
                imageInfo.sampler = VK_NULL_HANDLE; // サンプラーは別にバインド
                
                imageInfos.push_back(imageInfo);
                writeDesc.pImageInfo = &imageInfos.back();
                break;
            }
            
            case BindingInfo::ResourceType::Sampler: {
                // サンプラーの場合
                auto vulkanSampler = std::static_pointer_cast<VulkanSampler>(info.sampler);
                if (!vulkanSampler) {
                    throw std::runtime_error("無効なサンプラーが指定されました");
                }
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.sampler = vulkanSampler->GetVkSampler();
                imageInfo.imageView = VK_NULL_HANDLE; // イメージビューは別にバインド
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                
                imageInfos.push_back(imageInfo);
                writeDesc.pImageInfo = &imageInfos.back();
                break;
            }
        }
        
        descriptorWrites.push_back(writeDesc);
    }
    
    // ディスクリプタの更新
    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(
            m_device->GetVkDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr);
    }
    
    m_needsUpdate = false;
}

// レイアウトの取得
VkDescriptorSetLayout VulkanDescriptorSet::GetVkDescriptorSetLayout() const
{
    return m_layout->GetVkDescriptorSetLayout();
}

// パイプラインレイアウトの取得
VkPipelineLayout VulkanDescriptorSet::GetVkPipelineLayout() const
{
    return m_pipelineLayout;
}

// パイプラインレイアウト作成
void VulkanDescriptorSet::CreatePipelineLayout()
{
    // レイアウト作成情報の設定
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    // ディスクリプタセットレイアウト
    VkDescriptorSetLayout setLayout = m_layout->GetVkDescriptorSetLayout();
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;
    
    // プッシュコンスタント範囲（使用しない場合は0）
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    
    // パイプラインレイアウトの作成
    if (vkCreatePipelineLayout(m_device->GetVkDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanパイプラインレイアウトの作成に失敗しました");
    }
}

// バインディングに対応するVkDescriptorTypeを取得
VkDescriptorType VulkanDescriptorSet::GetVkDescriptorType(uint32_t binding) const
{
    // レイアウトからバインディング情報を探す
    for (const auto& bindingDesc : m_layout->GetBindings()) {
        if (bindingDesc.binding == binding) {
            // 対応するVkDescriptorTypeを返す
            switch (bindingDesc.type) {
                case DescriptorType::ConstantBuffer:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case DescriptorType::ShaderResource:
                    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case DescriptorType::UnorderedAccess:
                    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                case DescriptorType::Sampler:
                    return VK_DESCRIPTOR_TYPE_SAMPLER;
                case DescriptorType::StorageTexture:
                    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                default:
                    throw std::runtime_error("未対応のディスクリプタタイプです");
            }
        }
    }
    
    throw std::runtime_error("指定されたバインディングが見つかりません");
}

} // namespace NorvesLib::RHI::Vulkan