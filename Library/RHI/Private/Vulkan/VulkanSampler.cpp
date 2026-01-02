#include "VulkanSampler.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

VulkanSampler::VulkanSampler(TSharedPtr<VulkanDevice> device, const SamplerDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    vk::SamplerCreateInfo samplerInfo;
    
    // フィルタリングの設定
    samplerInfo.magFilter = ToVkFilter(desc.filterMag);
    samplerInfo.minFilter = ToVkFilter(desc.filterMin);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.filterMip);
    
    // アドレッシングモードの設定
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressModeU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressModeV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressModeW);
    
    // アニソトロピック フィルタリングの設定
    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1 ? vk::True : vk::False;
    samplerInfo.maxAnisotropy = static_cast<float>(desc.maxAnisotropy);
    
    // ボーダーカラーの設定
    samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
    
    // 座標の正規化設定
    samplerInfo.unnormalizedCoordinates = vk::False;
    
    // 比較機能の設定
    samplerInfo.compareEnable = desc.compareFunc != CompareFunc::Never ? vk::True : vk::False;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareFunc);
    
    // MIPマップの設定
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    
    // サンプラーの作成
    auto result = m_device->GetVkDevice().createSampler(samplerInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("Vulkanサンプラーの作成に失敗しました");
    }
    m_sampler = result.value;
}

VulkanSampler::~VulkanSampler()
{
    if (m_sampler)
    {
        m_device->GetVkDevice().destroySampler(m_sampler);
        m_sampler = nullptr;
    }
}

vk::Filter VulkanSampler::ToVkFilter(FilterMode mode) const
{
    switch (mode)
    {
    case FilterMode::Point:
        return vk::Filter::eNearest;
    case FilterMode::Linear:
    case FilterMode::Anisotropic:
        return vk::Filter::eLinear;
    default:
        return vk::Filter::eLinear;
    }
}

vk::SamplerMipmapMode VulkanSampler::ToVkMipmapMode(FilterMode mode) const
{
    switch (mode)
    {
    case FilterMode::Point:
        return vk::SamplerMipmapMode::eNearest;
    case FilterMode::Linear:
    case FilterMode::Anisotropic:
        return vk::SamplerMipmapMode::eLinear;
    default:
        return vk::SamplerMipmapMode::eLinear;
    }
}

vk::SamplerAddressMode VulkanSampler::ToVkAddressMode(TextureAddressMode mode) const
{
    switch (mode)
    {
    case TextureAddressMode::Wrap:
        return vk::SamplerAddressMode::eRepeat;
    case TextureAddressMode::Mirror:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case TextureAddressMode::Clamp:
        return vk::SamplerAddressMode::eClampToEdge;
    case TextureAddressMode::Border:
        return vk::SamplerAddressMode::eClampToBorder;
    case TextureAddressMode::MirrorOnce:
        return vk::SamplerAddressMode::eMirrorClampToEdge;
    default:
        return vk::SamplerAddressMode::eRepeat;
    }
}

vk::CompareOp VulkanSampler::ToVkCompareOp(CompareFunc func) const
{
    switch (func)
    {
    case CompareFunc::Never:
        return vk::CompareOp::eNever;
    case CompareFunc::Less:
        return vk::CompareOp::eLess;
    case CompareFunc::Equal:
        return vk::CompareOp::eEqual;
    case CompareFunc::LessEqual:
        return vk::CompareOp::eLessOrEqual;
    case CompareFunc::Greater:
        return vk::CompareOp::eGreater;
    case CompareFunc::NotEqual:
        return vk::CompareOp::eNotEqual;
    case CompareFunc::GreaterEqual:
        return vk::CompareOp::eGreaterOrEqual;
    case CompareFunc::Always:
        return vk::CompareOp::eAlways;
    default:
        return vk::CompareOp::eNever;
    }
}

vk::BorderColor VulkanSampler::ToVkBorderColor(SamplerDesc::BorderColor color) const
{
    switch (color)
    {
    case SamplerDesc::BorderColor::TransparentBlack:
        return vk::BorderColor::eFloatTransparentBlack;
    case SamplerDesc::BorderColor::OpaqueBlack:
        return vk::BorderColor::eFloatOpaqueBlack;
    case SamplerDesc::BorderColor::OpaqueWhite:
        return vk::BorderColor::eFloatOpaqueWhite;
    default:
        return vk::BorderColor::eFloatTransparentBlack;
    }
}

} // namespace NorvesLib::RHI::Vulkan
