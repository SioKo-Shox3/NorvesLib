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

//===========================================================================================
// VulkanDescriptorSetLayoutの実装
//===========================================================================================

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
    std::shared_ptr<VulkanDevice> device,
    const NorvesLib::Core::Container::VariableArray<DescriptorBindingDesc>& bindings)
    : m_device(device)
    , m_bindings(bindings)
{
    NorvesLib::Core::Container::VariableArray<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(bindings.size());

    for (const auto& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = ToVkDescriptorType(binding.type);
        layoutBinding.descriptorCount = binding.count;
        layoutBinding.stageFlags = ToVkShaderStageFlags(binding.stages);
        layoutBinding.pImmutableSamplers = nullptr; // イミュータブルサンプラーは現在サポートしない

        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(m_device->GetVkDevice(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("ディスクリプタセットレイアウトの作成に失敗しました");
    }
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->GetVkDevice(), m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

VkDescriptorType VulkanDescriptorSetLayout::ToVkDescriptorType(DescriptorType type) const
{
    switch (type) {
        case DescriptorType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::StorageImage:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformTexelBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case DescriptorType::StorageTexelBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case DescriptorType::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:
            throw std::runtime_error("未サポートのディスクリプタタイプです");
    }
}

VkShaderStageFlags VulkanDescriptorSetLayout::ToVkShaderStageFlags(ShaderStage stage) const
{
    VkShaderStageFlags flags = 0;
    
    if ((stage & ShaderStage::Vertex) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    
    if ((stage & ShaderStage::Pixel) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    
    if ((stage & ShaderStage::Compute) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    
    if ((stage & ShaderStage::Geometry) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    
    if ((stage & ShaderStage::Hull) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    
    if ((stage & ShaderStage::Domain) != ShaderStage::None)
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    
    return flags;
}

//===========================================================================================
// VulkanDescriptorPoolの実装
//===========================================================================================

VulkanDescriptorPool::VulkanDescriptorPool(std::shared_ptr<VulkanDevice> device, uint32_t maxSets)
    : m_device(device)
{
    // よく使われる各種ディスクリプタタイプの数を設定
    // 実際のアプリケーションでは使用頻度に応じて調整する必要があります
    std::array<VkDescriptorPoolSize, 6> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 4 },         // 定数バッファ
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets * 8 },          // テクスチャ
        { VK_DESCRIPTOR_TYPE_SAMPLER, maxSets * 4 },                // サンプラー
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 2 },         // ストレージバッファ
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets * 2 },          // ストレージイメージ
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 4 }  // コンバインドイメージサンプラー
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // 個々のセットを解放可能にする

    if (vkCreateDescriptorPool(m_device->GetVkDevice(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("ディスクリプタプールの作成に失敗しました");
    }
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->GetVkDevice(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
}

void VulkanDescriptorPool::Reset()
{
    if (m_pool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_device->GetVkDevice(), m_pool, 0);
    }
}

//===========================================================================================
// VulkanDescriptorSetの実装
//===========================================================================================

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
    // ディスクリプタセットの割り当て
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool->GetVkDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    
    VkDescriptorSetLayout layout_handle = layout->GetVkDescriptorSetLayout();
    allocInfo.pSetLayouts = &layout_handle;

    if (vkAllocateDescriptorSets(device->GetVkDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("ディスクリプタセットの割り当てに失敗しました");
    }

    // パイプラインレイアウトの作成
    CreatePipelineLayout();
}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
    // ディスクリプタセットはプールを破棄するときに自動的に解放されるため、
    // ここでは特に何もしません。個別に解放が必要な場合は下記のようにします。
    /*
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_device->GetVkDevice(), m_pool->GetVkDescriptorPool(), 1, &m_descriptorSet);
        m_descriptorSet = VK_NULL_HANDLE;
    }
    */
    
    // パイプラインレイアウトの破棄
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->GetVkDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

void VulkanDescriptorSet::SetConstantBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetShaderResourceBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetUnorderedAccessBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset, uint64_t range)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Buffer;
    info.bufferInfo.buffer = buffer;
    info.bufferInfo.offset = offset;
    info.bufferInfo.range = range;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetTexture(uint32_t binding, TexturePtr texture)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Texture;
    info.texture = texture;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetStorageTexture(uint32_t binding, TexturePtr texture)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Texture;
    info.texture = texture;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::SetSampler(uint32_t binding, SamplerPtr sampler)
{
    BindingInfo info{};
    info.type = BindingInfo::ResourceType::Sampler;
    info.sampler = sampler;
    
    m_bindings[binding] = info;
    m_needsUpdate = true;
}

void VulkanDescriptorSet::Update()
{
    if (!m_needsUpdate) {
        return;
    }

    NorvesLib::Core::Container::VariableArray<VkWriteDescriptorSet> descriptorWrites;
    NorvesLib::Core::Container::VariableArray<VkDescriptorBufferInfo> bufferInfos;
    NorvesLib::Core::Container::VariableArray<VkDescriptorImageInfo> imageInfos;

    for (const auto& [binding, info] : m_bindings) {
        VkWriteDescriptorSet writeDesc{};
        writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDesc.dstSet = m_descriptorSet;
        writeDesc.dstBinding = binding;
        writeDesc.dstArrayElement = 0;
        writeDesc.descriptorCount = 1;
        writeDesc.descriptorType = GetVkDescriptorType(binding);

        if (info.type == BindingInfo::ResourceType::Buffer) {
            auto vkBuffer = std::dynamic_pointer_cast<VulkanBuffer>(info.bufferInfo.buffer);
            if (!vkBuffer) {
                throw std::runtime_error("無効なバッファです");
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = vkBuffer->GetVkBuffer();
            bufferInfo.offset = info.bufferInfo.offset;
            bufferInfo.range = info.bufferInfo.range == 0 ? VK_WHOLE_SIZE : info.bufferInfo.range;

            bufferInfos.push_back(bufferInfo);
            writeDesc.pBufferInfo = &bufferInfos.back();
        }
        else if (info.type == BindingInfo::ResourceType::Texture) {
            auto vkTexture = std::dynamic_pointer_cast<VulkanTexture>(info.texture);
            if (!vkTexture) {
                throw std::runtime_error("無効なテクスチャです");
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = vkTexture->IsStorage() ? 
                VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = vkTexture->GetVkImageView();
            imageInfo.sampler = VK_NULL_HANDLE; // テクスチャのみの場合はサンプラーは設定しない

            imageInfos.push_back(imageInfo);
            writeDesc.pImageInfo = &imageInfos.back();
        }
        else if (info.type == BindingInfo::ResourceType::Sampler) {
            auto vkSampler = std::dynamic_pointer_cast<VulkanSampler>(info.sampler);
            if (!vkSampler) {
                throw std::runtime_error("無効なサンプラーです");
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED; // サンプラーのみの場合はレイアウトは使用しない
            imageInfo.imageView = VK_NULL_HANDLE; // サンプラーのみの場合はイメージビューは設定しない
            imageInfo.sampler = vkSampler->GetVkSampler();

            imageInfos.push_back(imageInfo);
            writeDesc.pImageInfo = &imageInfos.back();
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
            nullptr
        );
    }

    m_needsUpdate = false;
}

VkDescriptorSetLayout VulkanDescriptorSet::GetVkDescriptorSetLayout() const
{
    return m_layout->GetVkDescriptorSetLayout();
}

VkPipelineLayout VulkanDescriptorSet::GetVkPipelineLayout() const
{
    return m_pipelineLayout;
}

void VulkanDescriptorSet::CreatePipelineLayout()
{
    // パイプラインレイアウトの作成
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    // ディスクリプタセットレイアウトの設定
    VkDescriptorSetLayout layout = m_layout->GetVkDescriptorSetLayout();
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    
    // プッシュコンスタントの設定
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    
    if (vkCreatePipelineLayout(m_device->GetVkDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("パイプラインレイアウトの作成に失敗しました");
    }
}

VkDescriptorType VulkanDescriptorSet::GetVkDescriptorType(uint32_t binding) const
{
    // バインディング情報からディスクリプタタイプを取得
    for (const auto& bindingDesc : m_layout->GetBindings()) {
        if (bindingDesc.binding == binding) {
            return m_layout->ToVkDescriptorType(bindingDesc.type);
        }
    }
    
    throw std::runtime_error("指定されたバインディングに対するディスクリプタタイプが見つかりません");
}

} // namespace NorvesLib::RHI::Vulkan