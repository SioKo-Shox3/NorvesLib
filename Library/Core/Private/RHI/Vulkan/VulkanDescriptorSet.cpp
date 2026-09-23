#include "VulkanDescriptorSet.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanAccelerationStructure.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

    namespace
    {
        vk::ImageLayout ResolveDescriptorImageLayout(
            vk::DescriptorType descriptorType,
            vk::ImageLayout trackedLayout)
        {
            switch (descriptorType)
            {
            case vk::DescriptorType::eStorageImage:
                return vk::ImageLayout::eGeneral;
            case vk::DescriptorType::eSampledImage:
            case vk::DescriptorType::eCombinedImageSampler:
                return trackedLayout == vk::ImageLayout::eGeneral
                           ? vk::ImageLayout::eGeneral
                           : vk::ImageLayout::eShaderReadOnlyOptimal;
            case vk::DescriptorType::eSampler:
                return vk::ImageLayout::eUndefined;
            default:
                return trackedLayout;
            }
        }
    } // namespace

    //===========================================================================================
    // VulkanDescriptorSetLayoutの実装
    //===========================================================================================

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
        TSharedPtr<VulkanDevice> device,
        const NorvesLib::Core::Container::VariableArray<DescriptorBindingDesc> &bindings)
        : m_device(device), m_bindings(bindings)
    {
        m_vkBindings.reserve(bindings.size());

        for (const auto &binding : bindings)
        {
            vk::DescriptorSetLayoutBinding layoutBinding;
            layoutBinding.binding = binding.binding;
            layoutBinding.descriptorType = ToVkDescriptorType(binding.type);
            layoutBinding.descriptorCount = binding.count;
            layoutBinding.stageFlags = ToVkShaderStageFlags(binding.stages);
            layoutBinding.pImmutableSamplers = nullptr;

            m_vkBindings.push_back(layoutBinding);
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.bindingCount = static_cast<uint32_t>(m_vkBindings.size());
        layoutInfo.pBindings = m_vkBindings.data();

        auto result = m_device->GetVkDevice().createDescriptorSetLayout(layoutInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("ディスクリプタセットレイアウトの作成に失敗しました");
        }
        m_layout = result.value;
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
    {
        if (m_layout)
        {
            m_device->GetVkDevice().destroyDescriptorSetLayout(m_layout);
            m_layout = nullptr;
        }
    }

    vk::DescriptorType VulkanDescriptorSetLayout::ToVkDescriptorType(DescriptorType type) const
    {
        switch (type)
        {
        case DescriptorType::UniformBuffer:
            return vk::DescriptorType::eUniformBuffer;
        case DescriptorType::SampledImage:
            return vk::DescriptorType::eSampledImage;
        case DescriptorType::Sampler:
            return vk::DescriptorType::eSampler;
        case DescriptorType::StorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case DescriptorType::StorageImage:
            return vk::DescriptorType::eStorageImage;
        case DescriptorType::UniformTexelBuffer:
            return vk::DescriptorType::eUniformTexelBuffer;
        case DescriptorType::StorageTexelBuffer:
            return vk::DescriptorType::eStorageTexelBuffer;
        case DescriptorType::CombinedImageSampler:
            return vk::DescriptorType::eCombinedImageSampler;
        case DescriptorType::AccelerationStructure:
            return vk::DescriptorType::eAccelerationStructureKHR;
        default:
            throw std::runtime_error("未サポートのディスクリプタタイプです");
        }
    }

    vk::ShaderStageFlags VulkanDescriptorSetLayout::ToVkShaderStageFlags(ShaderStage stage) const
    {
        vk::ShaderStageFlags flags{};

        if ((stage & ShaderStage::Vertex) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eVertex;
        }

        if ((stage & ShaderStage::Pixel) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eFragment;
        }

        if ((stage & ShaderStage::Compute) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eCompute;
        }

        if ((stage & ShaderStage::Geometry) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eGeometry;
        }

        if ((stage & ShaderStage::Hull) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eTessellationControl;
        }

        if ((stage & ShaderStage::Domain) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
        }

        if ((stage & ShaderStage::RayGen) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eRaygenKHR;
        }

        if ((stage & ShaderStage::Miss) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eMissKHR;
        }

        if ((stage & ShaderStage::ClosestHit) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eClosestHitKHR;
        }

        if ((stage & ShaderStage::AnyHit) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eAnyHitKHR;
        }

        if ((stage & ShaderStage::Intersection) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eIntersectionKHR;
        }

        if ((stage & ShaderStage::Callable) != ShaderStage::None)
        {
            flags |= vk::ShaderStageFlagBits::eCallableKHR;
        }

        return flags;
    }

    //===========================================================================================
    // VulkanDescriptorPoolの実装
    //===========================================================================================

    VulkanDescriptorPool::VulkanDescriptorPool(TSharedPtr<VulkanDevice> device, uint32_t maxSets)
        : m_device(device)
    {
        CreatePool(maxSets, nullptr);
    }

    VulkanDescriptorPool::VulkanDescriptorPool(
        TSharedPtr<VulkanDevice> device,
        uint32_t maxSets,
        const VariableArray<DescriptorBindingDesc> &bindings)
        : m_device(device)
    {
        CreatePool(maxSets, &bindings);
    }

    void VulkanDescriptorPool::CreatePool(
        uint32_t maxSets,
        const VariableArray<DescriptorBindingDesc> *bindings)
    {
        Core::Container::FixedArray<vk::DescriptorPoolSize, 7> poolSizes = {{{vk::DescriptorType::eUniformBuffer, maxSets * 4},
                                                                             {vk::DescriptorType::eSampledImage, maxSets * 8},
                                                                             {vk::DescriptorType::eSampler, maxSets * 4},
                                                                             {vk::DescriptorType::eStorageBuffer, maxSets * 2},
                                                                             {vk::DescriptorType::eStorageImage, maxSets * 2},
                                                                             {vk::DescriptorType::eCombinedImageSampler, maxSets * 4},
                                                                             {vk::DescriptorType::eAccelerationStructureKHR, maxSets * 2}}};

        if (bindings != nullptr)
        {
            // 型ごとの要素数合計がmaxSets分に収まるよう、既定容量を下回らない範囲で広げる。
            for (vk::DescriptorPoolSize &poolSize : poolSizes)
            {
                uint64_t required = 0u;
                for (const DescriptorBindingDesc &binding : *bindings)
                {
                    vk::DescriptorType bindingType;
                    switch (binding.type)
                    {
                    case DescriptorType::UniformBuffer: bindingType = vk::DescriptorType::eUniformBuffer; break;
                    case DescriptorType::SampledImage: bindingType = vk::DescriptorType::eSampledImage; break;
                    case DescriptorType::Sampler: bindingType = vk::DescriptorType::eSampler; break;
                    case DescriptorType::StorageBuffer: bindingType = vk::DescriptorType::eStorageBuffer; break;
                    case DescriptorType::StorageImage: bindingType = vk::DescriptorType::eStorageImage; break;
                    case DescriptorType::CombinedImageSampler: bindingType = vk::DescriptorType::eCombinedImageSampler; break;
                    case DescriptorType::AccelerationStructure: bindingType = vk::DescriptorType::eAccelerationStructureKHR; break;
                    default: continue;
                    }
                    if (bindingType == poolSize.type)
                    {
                        required += static_cast<uint64_t>(binding.count) * maxSets;
                    }
                }
                if (required > poolSize.descriptorCount && required <= UINT32_MAX)
                {
                    poolSize.descriptorCount = static_cast<uint32_t>(required);
                }
            }
        }

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        if (!m_device->GetCapabilities().RayTracing.bAccelerationStructure)
        {
            --poolInfo.poolSizeCount;
        }
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        auto result = m_device->GetVkDevice().createDescriptorPool(poolInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("ディスクリプタプールの作成に失敗しました");
        }
        m_pool = result.value;
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
        if (m_pool)
        {
            m_device->GetVkDevice().destroyDescriptorPool(m_pool);
            m_pool = nullptr;
        }
    }

    void VulkanDescriptorPool::Reset()
    {
        if (m_pool)
        {
            m_device->GetVkDevice().resetDescriptorPool(m_pool, {});
        }
    }

    //===========================================================================================
    // VulkanDescriptorSetの実装
    //===========================================================================================

    VulkanDescriptorSet::VulkanDescriptorSet(
        TSharedPtr<VulkanDevice> device,
        const DescriptorSetDesc &desc,
        TSharedPtr<VulkanDescriptorSetLayout> layout,
        TSharedPtr<VulkanDescriptorPool> pool)
        : m_device(device), m_desc(desc), m_layout(layout), m_pool(pool)
    {
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = pool->GetVkDescriptorPool();
        allocInfo.descriptorSetCount = 1;

        vk::DescriptorSetLayout layoutHandle = layout->GetVkDescriptorSetLayout();
        allocInfo.pSetLayouts = &layoutHandle;

        auto result = device->GetVkDevice().allocateDescriptorSets(allocInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("ディスクリプタセットの割り当てに失敗しました");
        }
        m_descriptorSet = result.value[0];

        CreatePipelineLayout();
    }

    VulkanDescriptorSet::~VulkanDescriptorSet()
    {
        if (m_pipelineLayout)
        {
            m_device->GetVkDevice().destroyPipelineLayout(m_pipelineLayout);
            m_pipelineLayout = nullptr;
        }
    }

    void VulkanDescriptorSet::BindConstantBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size)
    {
        BindingInfo info;
        info.type = BindingInfo::ResourceType::Buffer;
        info.buffer = buffer;
        info.offset = offset;
        info.range = size;

        m_bindings[binding] = info;
        m_bNeedsUpdate = true;
    }

    void VulkanDescriptorSet::BindStorageBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size)
    {
        BindingInfo info;
        info.type = BindingInfo::ResourceType::Buffer;
        info.buffer = buffer;
        info.offset = offset;
        info.range = size;

        m_bindings[binding] = info;
        m_bNeedsUpdate = true;
    }

    bool VulkanDescriptorSet::BindAccelerationStructure(
        uint32_t binding,
        AccelerationStructurePtr accelerationStructure)
    {
        bool expectsAccelerationStructure = false;
        for (const DescriptorBinding& bindingDesc : m_desc.bindings)
        {
            if (bindingDesc.binding == binding)
            {
                expectsAccelerationStructure = bindingDesc.type == ResourceBindType::AccelerationStructure;
                break;
            }
        }

        auto vkAccelerationStructure = DynamicPointerCast<VulkanAccelerationStructure>(accelerationStructure);
        if (!expectsAccelerationStructure || !m_device || !vkAccelerationStructure ||
            vkAccelerationStructure->m_device.get() != m_device.get() ||
            vkAccelerationStructure->GetDesc().type != AccelerationStructureType::TopLevel ||
            !vkAccelerationStructure->GetVkAccelerationStructure())
        {
            return false;
        }

        BindingInfo info;
        info.type = BindingInfo::ResourceType::AccelerationStructure;
        info.accelerationStructure = accelerationStructure;

        m_bindings[binding] = info;
        m_bNeedsUpdate = true;
        return true;
    }

    void VulkanDescriptorSet::BindTexture(uint32_t binding, TexturePtr texture)
    {
        // CombinedImageSamplerの場合、既存のサンプラーを保持してマージする
        auto it = m_bindings.find(binding);
        if (it != m_bindings.end() && it->second.sampler)
        {
            it->second.type = BindingInfo::ResourceType::Texture;
            it->second.texture = texture;
        }
        else
        {
            BindingInfo info;
            info.type = BindingInfo::ResourceType::Texture;
            info.texture = texture;
            m_bindings[binding] = info;
        }
        m_bNeedsUpdate = true;
    }

    bool VulkanDescriptorSet::BindTextureArrayElement(uint32_t binding,
                                                      uint32_t arrayElement,
                                                      TexturePtr texture,
                                                      SamplerPtr sampler)
    {
        const uint32_t count = GetDescriptorCount(binding);
        if (count < 2u || arrayElement >= count ||
            GetVkDescriptorType(binding) != vk::DescriptorType::eCombinedImageSampler ||
            !DynamicPointerCast<VulkanTexture>(texture) ||
            !DynamicPointerCast<VulkanSampler>(sampler))
        {
            return false;
        }

        BindingInfo info;
        info.type = BindingInfo::ResourceType::Texture;
        info.texture = texture;
        info.sampler = sampler;
        m_arrayElementBindings[(static_cast<uint64_t>(binding) << 32u) | arrayElement] = info;
        m_bNeedsUpdate = true;
        return true;
    }

    void VulkanDescriptorSet::BindStorageTexture(uint32_t binding, TexturePtr texture)
    {
        BindingInfo info;
        info.type = BindingInfo::ResourceType::Texture;
        info.texture = texture;

        m_bindings[binding] = info;
        m_bNeedsUpdate = true;
    }

    void VulkanDescriptorSet::BindStorageTexture(uint32_t binding, TexturePtr texture, uint32_t mipLevel)
    {
        BindingInfo info;
        info.type = BindingInfo::ResourceType::Texture;
        info.texture = texture;
        info.mipLevel = static_cast<int32_t>(mipLevel);

        m_bindings[binding] = info;
        m_bNeedsUpdate = true;
    }

    void VulkanDescriptorSet::BindSampler(uint32_t binding, SamplerPtr sampler)
    {
        // CombinedImageSamplerの場合、既存のテクスチャを保持してマージする
        auto it = m_bindings.find(binding);
        if (it != m_bindings.end() && it->second.texture)
        {
            it->second.sampler = sampler;
        }
        else
        {
            BindingInfo info;
            info.type = BindingInfo::ResourceType::Sampler;
            info.sampler = sampler;
            m_bindings[binding] = info;
        }
        m_bNeedsUpdate = true;
    }

    void VulkanDescriptorSet::Update()
    {
        if (!m_bNeedsUpdate)
        {
            return;
        }

        NorvesLib::Core::Container::VariableArray<vk::WriteDescriptorSet> descriptorWrites;
        NorvesLib::Core::Container::VariableArray<vk::DescriptorBufferInfo> bufferInfos;
        NorvesLib::Core::Container::VariableArray<vk::DescriptorImageInfo> imageInfos;
        NorvesLib::Core::Container::VariableArray<vk::WriteDescriptorSetAccelerationStructureKHR>
            accelerationStructureInfos;
        NorvesLib::Core::Container::VariableArray<vk::AccelerationStructureKHR> accelerationStructureHandles;

        // pImageInfo等は要素アドレスを保持するため、配列要素分も含めて先に容量を確保する。
        bufferInfos.reserve(m_bindings.size());
        imageInfos.reserve(m_bindings.size() + m_arrayElementBindings.size());
        accelerationStructureInfos.reserve(m_bindings.size());
        accelerationStructureHandles.reserve(m_bindings.size());

        for (const auto &[binding, info] : m_bindings)
        {
            vk::WriteDescriptorSet writeDesc;
            writeDesc.dstSet = m_descriptorSet;
            writeDesc.dstBinding = binding;
            writeDesc.dstArrayElement = 0;
            writeDesc.descriptorCount = 1;
            writeDesc.descriptorType = GetVkDescriptorType(binding);

            if (info.type == BindingInfo::ResourceType::Buffer)
            {
                auto vkBuffer = DynamicPointerCast<VulkanBuffer>(info.buffer);
                if (!vkBuffer)
                {
                    throw std::runtime_error("無効なバッファです");
                }

                vk::DescriptorBufferInfo bufferInfo;
                bufferInfo.buffer = vkBuffer->GetVkBuffer();
                bufferInfo.offset = info.offset;
                bufferInfo.range = info.range == 0 ? VK_WHOLE_SIZE : info.range;

                bufferInfos.push_back(bufferInfo);
                writeDesc.pBufferInfo = &bufferInfos.back();
            }
            else if (info.type == BindingInfo::ResourceType::Texture)
            {
                auto vkTexture = DynamicPointerCast<VulkanTexture>(info.texture);
                if (!vkTexture)
                {
                    throw std::runtime_error("無効なテクスチャです");
                }

                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = ResolveDescriptorImageLayout(
                    writeDesc.descriptorType,
                    vkTexture->GetVkImageLayout());

                // per-mip ImageView指定がある場合はそちらを使用
                if (info.mipLevel >= 0)
                {
                    imageInfo.imageView = vkTexture->GetMipImageView(static_cast<uint32_t>(info.mipLevel));
                }
                else
                {
                    imageInfo.imageView = vkTexture->GetVkImageView();
                }

                // CombinedImageSampler: テクスチャと同じバインドにサンプラーがある場合
                if (info.sampler)
                {
                    auto vkSampler = DynamicPointerCast<VulkanSampler>(info.sampler);
                    if (vkSampler)
                    {
                        imageInfo.sampler = vkSampler->GetVkSampler();
                    }
                    else
                    {
                        imageInfo.sampler = nullptr;
                    }
                }
                else
                {
                    imageInfo.sampler = nullptr;
                }

                imageInfos.push_back(imageInfo);
                writeDesc.pImageInfo = &imageInfos.back();
            }
            else if (info.type == BindingInfo::ResourceType::Sampler)
            {
                auto vkSampler = DynamicPointerCast<VulkanSampler>(info.sampler);
                if (!vkSampler)
                {
                    throw std::runtime_error("無効なサンプラーです");
                }

                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = vk::ImageLayout::eUndefined;
                imageInfo.imageView = nullptr;
                imageInfo.sampler = vkSampler->GetVkSampler();

                imageInfos.push_back(imageInfo);
                writeDesc.pImageInfo = &imageInfos.back();
            }
            else if (info.type == BindingInfo::ResourceType::AccelerationStructure)
            {
                auto vkAccelerationStructure =
                    DynamicPointerCast<VulkanAccelerationStructure>(info.accelerationStructure);
                if (!vkAccelerationStructure || !vkAccelerationStructure->GetVkAccelerationStructure())
                {
                    throw std::runtime_error("無効な加速構造です");
                }

                accelerationStructureHandles.push_back(
                    vkAccelerationStructure->GetVkAccelerationStructure());
                vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo{};
                accelerationStructureInfo.accelerationStructureCount = 1;
                accelerationStructureInfo.pAccelerationStructures = &accelerationStructureHandles.back();
                accelerationStructureInfos.push_back(accelerationStructureInfo);
                writeDesc.pNext = &accelerationStructureInfos.back();
            }

            descriptorWrites.push_back(writeDesc);
        }

        for (const auto &[key, info] : m_arrayElementBindings)
        {
            const uint32_t binding = static_cast<uint32_t>(key >> 32u);
            const uint32_t arrayElement = static_cast<uint32_t>(key & 0xFFFFFFFFu);
            auto vkTexture = DynamicPointerCast<VulkanTexture>(info.texture);
            auto vkSampler = DynamicPointerCast<VulkanSampler>(info.sampler);
            if (!vkTexture || !vkSampler)
            {
                throw std::runtime_error("無効な配列テクスチャ要素です");
            }

            vk::WriteDescriptorSet writeDesc;
            writeDesc.dstSet = m_descriptorSet;
            writeDesc.dstBinding = binding;
            writeDesc.dstArrayElement = arrayElement;
            writeDesc.descriptorCount = 1;
            writeDesc.descriptorType = GetVkDescriptorType(binding);

            vk::DescriptorImageInfo imageInfo;
            imageInfo.imageLayout = ResolveDescriptorImageLayout(
                writeDesc.descriptorType,
                vkTexture->GetVkImageLayout());
            imageInfo.imageView = vkTexture->GetVkImageView();
            imageInfo.sampler = vkSampler->GetVkSampler();
            imageInfos.push_back(imageInfo);
            writeDesc.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(writeDesc);
        }

        if (!descriptorWrites.empty())
        {
            m_device->GetVkDevice().updateDescriptorSets(
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr);
        }

        m_bNeedsUpdate = false;
    }

    vk::DescriptorSetLayout VulkanDescriptorSet::GetVkDescriptorSetLayout() const
    {
        return m_layout->GetVkDescriptorSetLayout();
    }

    vk::PipelineLayout VulkanDescriptorSet::GetVkPipelineLayout() const
    {
        return m_pipelineLayout;
    }

    void VulkanDescriptorSet::CreatePipelineLayout()
    {
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;

        vk::DescriptorSetLayout layout = m_layout->GetVkDescriptorSetLayout();
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &layout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        auto result = m_device->GetVkDevice().createPipelineLayout(pipelineLayoutInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("パイプラインレイアウトの作成に失敗しました");
        }
        m_pipelineLayout = result.value;
    }

    vk::DescriptorType VulkanDescriptorSet::GetVkDescriptorType(uint32_t binding) const
    {
        for (const auto &bindingDesc : m_layout->GetBindings())
        {
            if (bindingDesc.binding == binding)
            {
                return m_layout->ToVkDescriptorType(bindingDesc.type);
            }
        }

        throw std::runtime_error("指定されたバインディングに対するディスクリプタタイプが見つかりません");
    }

    uint32_t VulkanDescriptorSet::GetDescriptorCount(uint32_t binding) const
    {
        for (const auto &bindingDesc : m_layout->GetBindings())
        {
            if (bindingDesc.binding == binding)
            {
                return bindingDesc.count;
            }
        }
        return 0u;
    }

} // namespace NorvesLib::RHI::Vulkan
