#pragma once

#include "VulkanDevice.h"
#include "VulkanPipeline.h"

namespace NorvesLib::RHI::Vulkan
{
    class VulkanBuffer;

    /**
     * @brief VulkanレイトレーシングpipelineとShader Binding Tableの実装
     */
    class VulkanRayTracingPipeline final : public VulkanPipeline
    {
    public:
        VulkanRayTracingPipeline(
            TSharedPtr<VulkanDevice> device,
            const RayTracingPipelineDesc& desc);
        ~VulkanRayTracingPipeline() override = default;

        BufferPtr GetShaderBindingTable() const;
        uint32_t GetShaderGroupCount() const { return m_shaderGroupCount; }
        uint32_t GetMaxRayDispatchInvocationCount() const
        {
            return m_rayTracingProperties.maxRayDispatchInvocationCount;
        }
        const vk::StridedDeviceAddressRegionKHR& GetRayGenerationRegion() const { return m_rayGenerationRegion; }
        const vk::StridedDeviceAddressRegionKHR& GetMissRegion() const { return m_missRegion; }
        const vk::StridedDeviceAddressRegionKHR& GetHitRegion() const { return m_hitRegion; }
        const vk::StridedDeviceAddressRegionKHR& GetCallableRegion() const { return m_callableRegion; }

    private:
        RayTracingPipelineDesc m_desc;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingProperties{};
        TSharedPtr<VulkanBuffer> m_shaderBindingTable;
        VariableArray<uint32_t> m_rayGenerationGroupIndices;
        VariableArray<uint32_t> m_missGroupIndices;
        VariableArray<uint32_t> m_hitGroupIndices;
        VariableArray<uint32_t> m_callableGroupIndices;
        vk::StridedDeviceAddressRegionKHR m_rayGenerationRegion{};
        vk::StridedDeviceAddressRegionKHR m_missRegion{};
        vk::StridedDeviceAddressRegionKHR m_hitRegion{};
        vk::StridedDeviceAddressRegionKHR m_callableRegion{};
        uint32_t m_shaderGroupCount = 0;

        void CreateRayTracingPipeline();
        void CreateShaderBindingTable();
    };
} // namespace NorvesLib::RHI::Vulkan
