#include "VulkanSampler.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanSampler::VulkanSampler(std::shared_ptr<VulkanDevice> device, const SamplerDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    // サンプラー作成情報の設定
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
    // フィルタリングの設定
    samplerInfo.magFilter = ToVkFilter(desc.filterMag);
    samplerInfo.minFilter = ToVkFilter(desc.filterMin);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.filterMip);
    
    // アドレッシングモードの設定
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressModeU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressModeV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressModeW);
    
    // アニソトロピック フィルタリングの設定
    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1 ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = static_cast<float>(desc.maxAnisotropy);
    
    // ボーダーカラーの設定
    samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
    
    // 座標の正規化設定
    samplerInfo.unnormalizedCoordinates = VK_FALSE; // 常に正規化
    
    // 比較機能の設定
    samplerInfo.compareEnable = desc.compareFunc != CompareFunc::Never ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareFunc);
    
    // MIPマップの設定
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    
    // サンプラーの作成
    if (vkCreateSampler(m_device->GetVkDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanサンプラーの作成に失敗しました");
    }
}

// デストラクタ
VulkanSampler::~VulkanSampler()
{
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device->GetVkDevice(), m_sampler, nullptr);
    }
}

// フィルターモードをVulkanフィルターに変換
VkFilter VulkanSampler::ToVkFilter(FilterMode mode) const
{
    switch (mode)
    {
    case FilterMode::Point:
        return VK_FILTER_NEAREST;
    case FilterMode::Linear:
    case FilterMode::Anisotropic:
        return VK_FILTER_LINEAR;
    default:
        return VK_FILTER_LINEAR;
    }
}

// ミップマップフィルターモードをVulkanミップマップモードに変換
VkSamplerMipmapMode VulkanSampler::ToVkMipmapMode(FilterMode mode) const
{
    switch (mode)
    {
    case FilterMode::Point:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case FilterMode::Linear:
    case FilterMode::Anisotropic:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

// アドレッシングモードをVulkanアドレッシングモードに変換
VkSamplerAddressMode VulkanSampler::ToVkAddressMode(TextureAddressMode mode) const
{
    switch (mode)
    {
    case TextureAddressMode::Wrap:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureAddressMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureAddressMode::Clamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureAddressMode::Border:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case TextureAddressMode::MirrorOnce:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// 比較関数をVulkan比較関数に変換
VkCompareOp VulkanSampler::ToVkCompareOp(CompareFunc func) const
{
    switch (func)
    {
    case CompareFunc::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareFunc::Less:
        return VK_COMPARE_OP_LESS;
    case CompareFunc::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareFunc::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunc::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareFunc::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunc::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareFunc::Always:
        return VK_COMPARE_OP_ALWAYS;
    default:
        return VK_COMPARE_OP_NEVER;
    }
}

// ボーダーカラーをVulkanボーダーカラーに変換
VkBorderColor VulkanSampler::ToVkBorderColor(SamplerDesc::BorderColor color) const
{
    switch (color)
    {
    case SamplerDesc::BorderColor::TransparentBlack:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case SamplerDesc::BorderColor::OpaqueBlack:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case SamplerDesc::BorderColor::OpaqueWhite:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    default:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

} // namespace NorvesLib::RHI::Vulkan