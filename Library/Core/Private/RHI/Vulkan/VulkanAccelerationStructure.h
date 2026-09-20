#pragma once

#include "VulkanDevice.h"
#include "VulkanBuffer.h"

namespace NorvesLib::RHI::Vulkan
{
    /**
     * @brief Vulkan加速構造とその構築領域を管理する
     */
    class VulkanAccelerationStructure final : public IAccelerationStructure
    {
    public:
        VulkanAccelerationStructure(
            TSharedPtr<VulkanDevice> device,
            const AccelerationStructureDesc& desc);
        ~VulkanAccelerationStructure() override;

        const AccelerationStructureDesc& GetDesc() const override { return m_desc; }
        uint64_t GetSize() const override { return m_size; }
        uint64_t GetDeviceAddress() const override { return m_deviceAddress; }
        bool Build(const AccelerationStructureBuildDesc& desc) override;

        vk::AccelerationStructureKHR GetVkAccelerationStructure() const { return m_accelerationStructure; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        AccelerationStructureDesc m_desc;
        TSharedPtr<VulkanBuffer> m_storageBuffer;
        vk::AccelerationStructureKHR m_accelerationStructure;
        uint64_t m_size = 0;
        uint64_t m_buildScratchSize = 0;
        uint64_t m_deviceAddress = 0;
    };
} // namespace NorvesLib::RHI::Vulkan
