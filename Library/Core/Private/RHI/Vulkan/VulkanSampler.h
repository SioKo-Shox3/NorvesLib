#pragma once

#include "RHI/ISampler.h"
#include "RHI/IDevice.h" // SamplerDesc用
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::RHI::SamplerDesc;

    class VulkanDevice;

    /**
     * @brief Vulkanサンプラー実装クラス (vulkan.hpp使用)
     */
    class VulkanSampler : public ISampler
    {
    public:
        /**
         * @brief VulkanSamplerのコンストラクタ
         * @param device Vulkanデバイス
         * @param desc サンプラー記述子
         */
        VulkanSampler(TSharedPtr<VulkanDevice> device, const SamplerDesc &desc);

        /**
         * @brief デストラクタ
         */
        virtual ~VulkanSampler();

        // ISamplerインターフェース実装
        FilterMode GetFilterMin() const override { return m_desc.filterMin; }
        FilterMode GetFilterMag() const override { return m_desc.filterMag; }
        FilterMode GetFilterMip() const override { return m_desc.filterMip; }
        TextureAddressMode GetAddressModeU() const override { return m_desc.addressU; }
        TextureAddressMode GetAddressModeV() const override { return m_desc.addressV; }
        TextureAddressMode GetAddressModeW() const override { return m_desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return m_desc.maxAnisotropy; }
        CompareFunc GetCompareFunc() const override { return m_desc.compareFunc; }

        // Vulkan固有の拡張メソッド
        float GetMipLodBias() const { return m_desc.mipLODBias; }
        float GetMinLod() const { return m_desc.minLOD; }
        float GetMaxLod() const { return m_desc.maxLOD; }
        const float *GetBorderColor() const { return m_desc.borderColor; }

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Sampler GetVkSampler() const { return m_sampler; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        SamplerDesc m_desc;
        vk::Sampler m_sampler;

        vk::Filter ToVkFilter(FilterMode mode) const;
        vk::SamplerMipmapMode ToVkMipmapMode(FilterMode mode) const;
        vk::SamplerAddressMode ToVkAddressMode(TextureAddressMode mode) const;
        vk::CompareOp ToVkCompareOp(CompareFunc func) const;
        vk::BorderColor ToVkBorderColor(const float borderColor[4]) const;
    };

} // namespace NorvesLib::RHI::Vulkan
