#include "VulkanDescriptorSet.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanAccelerationStructure.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

    namespace
    {
        /** @brief 記述子の型をフレームごとの pool が数える型へ写す（数えない型は Count） */
        FrameDescriptorPoolType ToFrameDescriptorPoolType(DescriptorType type)
        {
            switch (type)
            {
            case DescriptorType::UniformBuffer:
                return FrameDescriptorPoolType::UniformBuffer;
            case DescriptorType::SampledImage:
                return FrameDescriptorPoolType::SampledImage;
            case DescriptorType::Sampler:
                return FrameDescriptorPoolType::Sampler;
            case DescriptorType::StorageBuffer:
                return FrameDescriptorPoolType::StorageBuffer;
            case DescriptorType::StorageImage:
                return FrameDescriptorPoolType::StorageImage;
            case DescriptorType::CombinedImageSampler:
                return FrameDescriptorPoolType::CombinedImageSampler;
            case DescriptorType::AccelerationStructure:
                return FrameDescriptorPoolType::AccelerationStructure;
            default:
                return FrameDescriptorPoolType::Count;
            }
        }

        vk::DescriptorType ToVkDescriptorType(FrameDescriptorPoolType type)
        {
            switch (type)
            {
            case FrameDescriptorPoolType::UniformBuffer:
                return vk::DescriptorType::eUniformBuffer;
            case FrameDescriptorPoolType::SampledImage:
                return vk::DescriptorType::eSampledImage;
            case FrameDescriptorPoolType::Sampler:
                return vk::DescriptorType::eSampler;
            case FrameDescriptorPoolType::StorageBuffer:
                return vk::DescriptorType::eStorageBuffer;
            case FrameDescriptorPoolType::StorageImage:
                return vk::DescriptorType::eStorageImage;
            case FrameDescriptorPoolType::CombinedImageSampler:
                return vk::DescriptorType::eCombinedImageSampler;
            case FrameDescriptorPoolType::AccelerationStructure:
            default:
                return vk::DescriptorType::eAccelerationStructureKHR;
            }
        }

        uint32_t GetCapacityCount(const FrameDescriptorPoolCapacity &capacity, FrameDescriptorPoolType type)
        {
            switch (type)
            {
            case FrameDescriptorPoolType::UniformBuffer:
                return capacity.UniformBuffers;
            case FrameDescriptorPoolType::SampledImage:
                return capacity.SampledImages;
            case FrameDescriptorPoolType::Sampler:
                return capacity.Samplers;
            case FrameDescriptorPoolType::StorageBuffer:
                return capacity.StorageBuffers;
            case FrameDescriptorPoolType::StorageImage:
                return capacity.StorageImages;
            case FrameDescriptorPoolType::CombinedImageSampler:
                return capacity.CombinedImageSamplers;
            case FrameDescriptorPoolType::AccelerationStructure:
                return capacity.AccelerationStructures;
            default:
                return 0u;
            }
        }

#if NORVES_BUILD_DEVELOPMENT
        uint64_t ReadDescriptorTimerNanoseconds()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count());
        }
#endif

        // 同じ誤りが毎フレーム続くとログが埋まるので、最初の 8 回と、その後は 2 の冪の回だけ出す
        bool ShouldLogRepeated(uint64_t count)
        {
            return count <= 8 || (count & (count - 1)) == 0;
        }

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
            // 前の提出が使っているかもしれないので、提出の完了まで破棄を遅らせる
            m_device->DeferDestroy(m_layout);
            m_layout = nullptr;
        }
    }

    void VulkanDescriptorSetLayout::ReleaseHandle()
    {
        if (m_layout)
        {
            m_device->DeferDestroy(m_layout);
            m_layout = nullptr;
        }
    }

    //===========================================================================================
    // VulkanSharedDescriptorSetLayoutの実装
    //===========================================================================================

    VulkanSharedDescriptorSetLayout::VulkanSharedDescriptorSetLayout(
        const DescriptorSetDesc &desc,
        TSharedPtr<VulkanDescriptorSetLayout> layout,
        VariableArray<uint32_t> signature)
        : m_desc(desc), m_layout(layout), m_signature(signature)
    {
        for (const DescriptorBindingDesc &binding : m_layout->GetBindings())
        {
            const FrameDescriptorPoolType type = ToFrameDescriptorPoolType(binding.type);
            if (type != FrameDescriptorPoolType::Count)
            {
                m_requiredCounts[static_cast<uint32_t>(type)] += binding.count;
            }
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

        bool bHasArrayBinding = false;
        if (bindings != nullptr)
        {
            for (const DescriptorBindingDesc &binding : *bindings)
            {
                bHasArrayBinding = bHasArrayBinding || binding.count > 1u;
            }
        }

        if (bHasArrayBinding)
        {
            // 配列を含むlayoutだけ、型ごとの要素数合計がmaxSets分に収まるよう既定容量から広げる。
            // 配列を含まないlayoutは従来の既定容量のままにする。
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
            // 前の提出が使っているかもしれないので、提出の完了まで破棄を遅らせる
            // （このプールから確保した記述子セットもプールと一緒に解放される）
            m_device->DeferDestroy(m_pool);
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

    VulkanDescriptorSet::VulkanDescriptorSet(
        TSharedPtr<VulkanDevice> device,
        TSharedPtr<VulkanDescriptorSetLayout> layout,
        vk::DescriptorSet descriptorSet,
        TSharedPtr<VulkanFrameDescriptorPoolState> borrowState,
        uint64_t borrowEpoch)
        : m_device(device),
          m_layout(layout),
          m_borrowState(borrowState),
          m_borrowEpoch(borrowEpoch),
          m_descriptorSet(descriptorSet)
    {
        // 借りたセットはパイプラインレイアウトを持たない（束縛はパイプラインのレイアウトで行う）
    }

    VulkanDescriptorSet::~VulkanDescriptorSet()
    {
        if (m_pipelineLayout)
        {
            // 前の提出が使っているかもしれないので、提出の完了まで破棄を遅らせる。
            // 記述子セットそのものは専用のプール（m_pool）と一緒に、プールの破棄の時に解放される
            m_device->DeferDestroy(m_pipelineLayout);
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
        // 借りたセットは作成情報を持たないので、レイアウトの束縛で型を確かめる
        bool expectsAccelerationStructure = false;
        for (const DescriptorBindingDesc& bindingDesc : m_layout->GetBindings())
        {
            if (bindingDesc.binding == binding)
            {
                expectsAccelerationStructure = bindingDesc.type == DescriptorType::AccelerationStructure;
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

        if (!m_borrowState)
        {
            (void)WriteBindings();
            m_bNeedsUpdate = false;
            return;
        }

        // 借りたセット: pool の回収・確保と同じ排他の下で、回収されていなければ書き込む
#if NORVES_BUILD_DEVELOPMENT
        const uint64_t startNanoseconds = ReadDescriptorTimerNanoseconds();
#endif
        Thread::ScopedLock lock(m_borrowState->Mutex);
        if (m_borrowState->Epoch.Load() != m_borrowEpoch)
        {
            static Thread::Atomic<uint64_t> s_expiredWriteRejectCount{0};
            ++m_borrowState->Stats.ExpiredRejects;
            const uint64_t rejectCount = s_expiredWriteRejectCount.FetchAdd(1) + 1;
            if (ShouldLogRepeated(rejectCount))
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "FRAME_RESOURCE_DESCRIPTOR_EXPIRED op=update borrowed_epoch=%llu current_epoch=%llu rejects=%llu",
                                 static_cast<unsigned long long>(m_borrowEpoch),
                                 static_cast<unsigned long long>(m_borrowState->Epoch.Load()),
                                 static_cast<unsigned long long>(rejectCount));
            }
            return;
        }
        const uint32_t writeCount = WriteBindings();
        m_bNeedsUpdate = false;
        m_borrowState->Stats.Writes += writeCount;
#if NORVES_BUILD_DEVELOPMENT
        m_borrowState->Stats.WriteNanoseconds += ReadDescriptorTimerNanoseconds() - startNanoseconds;
#endif
    }

    bool VulkanDescriptorSet::IsExpired() const
    {
        return m_borrowState && m_borrowState->Epoch.Load() != m_borrowEpoch;
    }

    uint32_t VulkanDescriptorSet::WriteBindings()
    {
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

        return static_cast<uint32_t>(descriptorWrites.size());
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

    //===========================================================================================
    // VulkanFrameDescriptorAllocatorの実装
    //===========================================================================================

    VulkanFrameDescriptorAllocator::VulkanFrameDescriptorAllocator(
        TSharedPtr<VulkanDevice> device,
        const FrameDescriptorPoolCapacity &capacity)
        : m_device(device), m_capacity(capacity), m_state(MakeShared<VulkanFrameDescriptorPoolState>())
    {
        m_capacity.MaxSets = std::max(m_capacity.MaxSets, 1u);
    }

    VulkanFrameDescriptorAllocator::~VulkanFrameDescriptorAllocator()
    {
        Release();
    }

    bool VulkanFrameDescriptorAllocator::Fits(const PoolEntry &pool, const VulkanSharedDescriptorSetLayout &layout)
    {
        if (pool.RemainingSets == 0)
        {
            return false;
        }
        for (uint32_t typeIndex = 0; typeIndex < FrameDescriptorPoolTypeCount; ++typeIndex)
        {
            if (layout.GetRequiredCount(static_cast<FrameDescriptorPoolType>(typeIndex)) > pool.Remaining[typeIndex])
            {
                return false;
            }
        }
        return true;
    }

    bool VulkanFrameDescriptorAllocator::AddPoolLocked(const VulkanSharedDescriptorSetLayout &layout)
    {
        const bool bAccelerationStructure = m_device->GetCapabilities().RayTracing.bAccelerationStructure;
        PoolEntry entry;
        entry.MaxSets = m_capacity.MaxSets;
        entry.RemainingSets = entry.MaxSets;

        VariableArray<vk::DescriptorPoolSize> poolSizes;
        poolSizes.reserve(FrameDescriptorPoolTypeCount);
        for (uint32_t typeIndex = 0; typeIndex < FrameDescriptorPoolTypeCount; ++typeIndex)
        {
            const FrameDescriptorPoolType type = static_cast<FrameDescriptorPoolType>(typeIndex);
            // 容量を超える型を持つレイアウトは、そのレイアウトが入る大きさの pool にする
            const uint32_t count = std::max(GetCapacityCount(m_capacity, type), layout.GetRequiredCount(type));
            if (count == 0 || (type == FrameDescriptorPoolType::AccelerationStructure && !bAccelerationStructure))
            {
                continue;
            }
            entry.Capacity[typeIndex] = count;
            entry.Remaining[typeIndex] = count;
            vk::DescriptorPoolSize poolSize;
            poolSize.type = ToVkDescriptorType(type);
            poolSize.descriptorCount = count;
            poolSizes.push_back(poolSize);
        }
        if (poolSizes.empty())
        {
            return false;
        }

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.maxSets = entry.MaxSets;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        // セットを 1 つずつ解放しない（回収で pool ごと戻す）ので eFreeDescriptorSet は付けない
        auto result = m_device->GetVkDevice().createDescriptorPool(poolInfo);
        if (result.result != vk::Result::eSuccess)
        {
            NORVES_LOG_ERROR("FrameResource",
                             "FRAME_RESOURCE_DESCRIPTOR_POOL_FAILED result=%d max_sets=%u",
                             static_cast<int32_t>(result.result),
                             entry.MaxSets);
            return false;
        }
        entry.Pool = result.value;
        m_pools.push_back(entry);
        ++m_totalPoolsAdded;
        ++m_state->Stats.PoolsAdded;
        return true;
    }

    DescriptorSetPtr VulkanFrameDescriptorAllocator::Allocate(const DescriptorSetLayoutPtr &layout)
    {
#if NORVES_BUILD_DEVELOPMENT
        const uint64_t startNanoseconds = ReadDescriptorTimerNanoseconds();
#endif
        auto sharedLayout = DynamicPointerCast<VulkanSharedDescriptorSetLayout>(layout);
        Thread::ScopedLock lock(m_state->Mutex);
        if (!m_device || !sharedLayout || !sharedLayout->GetLayout() ||
            !sharedLayout->GetLayout()->GetVkDescriptorSetLayout())
        {
            ++m_state->Stats.Failures;
            NORVES_LOG_ERROR("FrameResource", "FRAME_RESOURCE_DESCRIPTOR_INVALID_LAYOUT");
            return nullptr;
        }

        // 今の pool から前へ進み、入る pool を探す。どれにも入らなければ pool を足す
        while (m_currentPool < m_pools.size() && !Fits(m_pools[m_currentPool], *sharedLayout))
        {
            ++m_currentPool;
        }
        if (m_currentPool >= m_pools.size())
        {
            if (!AddPoolLocked(*sharedLayout))
            {
                ++m_state->Stats.Failures;
                return nullptr;
            }
            m_currentPool = m_pools.size() - 1;
            if (!Fits(m_pools[m_currentPool], *sharedLayout))
            {
                // デバイスに無い型（加速構造など）を求めるレイアウトは、足した pool にも入らない
                ++m_state->Stats.Failures;
                NORVES_LOG_ERROR("FrameResource", "FRAME_RESOURCE_DESCRIPTOR_UNSUPPORTED_LAYOUT");
                return nullptr;
            }
        }

        PoolEntry &pool = m_pools[m_currentPool];
        vk::DescriptorSetLayout layoutHandle = sharedLayout->GetLayout()->GetVkDescriptorSetLayout();
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = pool.Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layoutHandle;
        vk::DescriptorSet descriptorSet;
        const vk::Result result = m_device->GetVkDevice().allocateDescriptorSets(&allocInfo, &descriptorSet);
        if (result != vk::Result::eSuccess)
        {
            // 残りは数えているので本来は起きない。この pool は使い切ったとして次へ進む
            pool.RemainingSets = 0;
            ++m_state->Stats.Failures;
            NORVES_LOG_ERROR("FrameResource",
                             "FRAME_RESOURCE_DESCRIPTOR_ALLOCATE_FAILED result=%d pool=%llu",
                             static_cast<int32_t>(result),
                             static_cast<unsigned long long>(m_currentPool));
            return nullptr;
        }
        --pool.RemainingSets;
        for (uint32_t typeIndex = 0; typeIndex < FrameDescriptorPoolTypeCount; ++typeIndex)
        {
            pool.Remaining[typeIndex] -= sharedLayout->GetRequiredCount(static_cast<FrameDescriptorPoolType>(typeIndex));
        }

        auto descriptorSetObject = MakeShared<VulkanDescriptorSet>(
            m_device, sharedLayout->GetLayout(), descriptorSet, m_state, m_state->Epoch.Load());
        ++m_state->Stats.Allocations;
#if NORVES_BUILD_DEVELOPMENT
        m_state->Stats.AllocateNanoseconds += ReadDescriptorTimerNanoseconds() - startNanoseconds;
#endif
        return StaticPointerCast<IDescriptorSet>(descriptorSetObject);
    }

    void VulkanFrameDescriptorAllocator::Reset()
    {
        Thread::ScopedLock lock(m_state->Mutex);
        // 前に借りたセットを使えなくしてから、pool をまとめて戻す
        m_state->Epoch.FetchAdd(1);
        for (PoolEntry &pool : m_pools)
        {
            m_device->GetVkDevice().resetDescriptorPool(pool.Pool, {});
            pool.RemainingSets = pool.MaxSets;
            for (uint32_t typeIndex = 0; typeIndex < FrameDescriptorPoolTypeCount; ++typeIndex)
            {
                pool.Remaining[typeIndex] = pool.Capacity[typeIndex];
            }
        }
        m_currentPool = 0;
        m_state->Stats = {};
    }

    void VulkanFrameDescriptorAllocator::ReleasePoolsLocked()
    {
        m_state->Epoch.FetchAdd(1);
        for (PoolEntry &pool : m_pools)
        {
            // 前の提出が使っているかもしれないので、提出の完了まで破棄を遅らせる（確保したセットも一緒に戻る）
            m_device->DeferDestroy(pool.Pool);
        }
        m_pools.clear();
        m_currentPool = 0;
        m_state->Stats = {};
    }

    void VulkanFrameDescriptorAllocator::Release()
    {
        if (!m_device || !m_state)
        {
            return;
        }
        Thread::ScopedLock lock(m_state->Mutex);
        ReleasePoolsLocked();
    }

    FrameDescriptorStats VulkanFrameDescriptorAllocator::GetFrameStats() const
    {
        Thread::ScopedLock lock(m_state->Mutex);
        return m_state->Stats;
    }

    uint32_t VulkanFrameDescriptorAllocator::GetPoolCount() const
    {
        Thread::ScopedLock lock(m_state->Mutex);
        return static_cast<uint32_t>(m_pools.size());
    }

    uint64_t VulkanFrameDescriptorAllocator::GetTotalPoolsAdded() const
    {
        Thread::ScopedLock lock(m_state->Mutex);
        return m_totalPoolsAdded;
    }

} // namespace NorvesLib::RHI::Vulkan
