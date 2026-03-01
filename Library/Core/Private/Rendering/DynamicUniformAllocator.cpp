#include "Rendering/DynamicUniformAllocator.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"

#include <cstdio>

namespace NorvesLib::Core::Rendering
{

    bool DynamicUniformAllocator::Initialize(RHI::IDevice* device, uint32_t uboSize, uint32_t maxSlots,
                                              const RHI::DescriptorSetDesc& descriptorSetDesc)
    {
        if (m_bInitialized)
        {
            NORVES_LOG_WARNING("DynamicUniformAllocator", "Already initialized");
            return true;
        }

        if (!device || uboSize == 0 || maxSlots == 0)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Invalid parameters for DynamicUniformAllocator::Initialize");
            return false;
        }

        m_UBOSize = uboSize;
        m_MaxSlots = maxSlots;
        m_CurrentIndex = 0;

        m_Slots.resize(maxSlots);

        char debugName[128];
        for (uint32_t i = 0; i < maxSlots; ++i)
        {
            // UBOバッファ作成
            std::snprintf(debugName, sizeof(debugName), "DynUBO_Slot%u", i);
            RHI::BufferDesc bufDesc(uboSize, RHI::ResourceUsage::ConstantBuffer, true, debugName);
            m_Slots[i].UniformBuffer = device->CreateBuffer(bufDesc);
            if (!m_Slots[i].UniformBuffer)
            {
                NORVES_LOG_ERROR("DynamicUniformAllocator", "Failed to create UBO for slot %u", i);
                Shutdown();
                return false;
            }

            // DescriptorSet作成
            m_Slots[i].DescriptorSet = device->CreateDescriptorSet(descriptorSetDesc);
            if (!m_Slots[i].DescriptorSet)
            {
                NORVES_LOG_ERROR("DynamicUniformAllocator", "Failed to create DescriptorSet for slot %u", i);
                Shutdown();
                return false;
            }

            // UBOをDescriptorSetのbinding 0にバインド
            m_Slots[i].DescriptorSet->BindConstantBuffer(0, m_Slots[i].UniformBuffer, 0, uboSize);
            m_Slots[i].DescriptorSet->Update();
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("DynamicUniformAllocator", "Initialized: %u slots, %u bytes/slot", maxSlots, uboSize);
        return true;
    }

    void DynamicUniformAllocator::Shutdown()
    {
        for (auto& slot : m_Slots)
        {
            slot.DescriptorSet.reset();
            slot.UniformBuffer.reset();
        }
        m_Slots.clear();
        m_MaxSlots = 0;
        m_UBOSize = 0;
        m_CurrentIndex = 0;
        m_bInitialized = false;
    }

    void DynamicUniformAllocator::Reset()
    {
        m_CurrentIndex = 0;
    }

    DynamicUniformAllocator::Allocation DynamicUniformAllocator::Allocate()
    {
        Allocation result{};

        if (!m_bInitialized)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Not initialized");
            return result;
        }

        if (m_CurrentIndex >= m_MaxSlots)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Out of slots (%u/%u)", m_CurrentIndex, m_MaxSlots);
            return result;
        }

        auto& slot = m_Slots[m_CurrentIndex];
        result.UniformBuffer = slot.UniformBuffer;
        result.DescriptorSet = slot.DescriptorSet;
        result.SlotIndex = m_CurrentIndex;

        ++m_CurrentIndex;
        return result;
    }

} // namespace NorvesLib::Core::Rendering
