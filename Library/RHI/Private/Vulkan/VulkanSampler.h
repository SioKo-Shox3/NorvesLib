#pragma once

#include "RHI/Public/ISampler.h"
#include "RHI/Public/RHITypes.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

    class VulkanDevice;

    /**
     * @brief サンプラー記述子
     */
    struct SamplerDesc
    {
        FilterMode filterMin = FilterMode::Linear;
        FilterMode filterMag = FilterMode::Linear;
        FilterMode filterMip = FilterMode::Linear;
        TextureAddressMode addressModeU = TextureAddressMode::Wrap;
        TextureAddressMode addressModeV = TextureAddressMode::Wrap;
        TextureAddressMode addressModeW = TextureAddressMode::Wrap;
        uint32_t maxAnisotropy = 1;
        CompareFunc compareFunc = CompareFunc::Never;
        float mipLodBias = 0.0f;
        float minLod = 0.0f;
        float maxLod = 1000.0f;
        enum class BorderColor
        {
            TransparentBlack,
            OpaqueBlack,
            OpaqueWhite
        } borderColor = BorderColor::TransparentBlack;
    };

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
        TextureAddressMode GetAddressModeU() const override { return m_desc.addressModeU; }
        TextureAddressMode GetAddressModeV() const override { return m_desc.addressModeV; }
        TextureAddressMode GetAddressModeW() const override { return m_desc.addressModeW; }
        uint32_t GetMaxAnisotropy() const override { return m_desc.maxAnisotropy; }
        CompareFunc GetCompareFunc() const override { return m_desc.compareFunc; }

        // Vulkan固有の拡張メソッド
        float GetMipLodBias() const { return m_desc.mipLodBias; }
        float GetMinLod() const { return m_desc.minLod; }
        float GetMaxLod() const { return m_desc.maxLod; }
        SamplerDesc::BorderColor GetBorderColor() const { return m_desc.borderColor; }

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
        vk::BorderColor ToVkBorderColor(SamplerDesc::BorderColor color) const;
    };

} // namespace NorvesLib::RHI::Vulkan