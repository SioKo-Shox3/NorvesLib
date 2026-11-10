#pragma once

#include "RHI/FormatInfo.h"
#include <vulkan/vulkan.hpp>

namespace NorvesLib::RHI::Vulkan
{
    struct VulkanFormatMapping
    {
        Format RhiFormat;
        vk::Format VkFormat;
    };

    // UNKNOWN 以外の RHI 形式と Vulkan 形式の対応。
    inline constexpr VulkanFormatMapping kVulkanFormatTable[] =
    {
        {Format::R8_UNORM, vk::Format::eR8Unorm},
        {Format::R8G8_UNORM, vk::Format::eR8G8Unorm},
        {Format::R8G8B8A8_UNORM, vk::Format::eR8G8B8A8Unorm},
        {Format::R8G8B8A8_SRGB, vk::Format::eR8G8B8A8Srgb},
        {Format::B8G8R8A8_UNORM, vk::Format::eB8G8R8A8Unorm},
        {Format::B8G8R8A8_SRGB, vk::Format::eB8G8R8A8Srgb},
        {Format::R16_FLOAT, vk::Format::eR16Sfloat},
        {Format::R16G16_FLOAT, vk::Format::eR16G16Sfloat},
        {Format::R16G16B16A16_FLOAT, vk::Format::eR16G16B16A16Sfloat},
        {Format::R32_FLOAT, vk::Format::eR32Sfloat},
        {Format::R32G32_FLOAT, vk::Format::eR32G32Sfloat},
        {Format::R32G32B32_FLOAT, vk::Format::eR32G32B32Sfloat},
        {Format::R32G32B32A32_FLOAT, vk::Format::eR32G32B32A32Sfloat},
        {Format::D16_UNORM, vk::Format::eD16Unorm},
        {Format::D24_UNORM_S8_UINT, vk::Format::eD24UnormS8Uint},
        {Format::D32_FLOAT, vk::Format::eD32Sfloat},
        {Format::R16_UNORM, vk::Format::eR16Unorm},
        {Format::BC1_UNORM, vk::Format::eBc1RgbaUnormBlock},
        {Format::BC1_SRGB, vk::Format::eBc1RgbaSrgbBlock},
        {Format::BC4_UNORM, vk::Format::eBc4UnormBlock},
        {Format::BC5_UNORM, vk::Format::eBc5UnormBlock},
        {Format::BC7_UNORM, vk::Format::eBc7UnormBlock},
        {Format::BC7_SRGB, vk::Format::eBc7SrgbBlock},
        {Format::R32_UINT, vk::Format::eR32Uint},
        {Format::R32G32_UINT, vk::Format::eR32G32Uint},
        {Format::R16G16_UNORM, vk::Format::eR16G16Unorm},
        // パック形式の R は下位ビットに置く。
        {Format::R10G10B10A2_UNORM, vk::Format::eA2B10G10R10UnormPack32},
        {Format::R11G11B10_FLOAT, vk::Format::eB10G11R11UfloatPack32},
        {Format::BC3_UNORM, vk::Format::eBc3UnormBlock},
        {Format::BC3_SRGB, vk::Format::eBc3SrgbBlock},
        {Format::BC6H_UFLOAT, vk::Format::eBc6HUfloatBlock},
    };

    static_assert(sizeof(kVulkanFormatTable) / sizeof(kVulkanFormatTable[0]) == FormatCount - 1,
                  "Vulkan の形式表は UNKNOWN 以外の全形式を含める");

    inline constexpr vk::Format ToVkFormatFromTable(Format format)
    {
        for (const VulkanFormatMapping &mapping : kVulkanFormatTable)
        {
            if (mapping.RhiFormat == format)
            {
                return mapping.VkFormat;
            }
        }
        return vk::Format::eUndefined;
    }

    inline constexpr Format FromVkFormatFromTable(vk::Format format)
    {
        for (const VulkanFormatMapping &mapping : kVulkanFormatTable)
        {
            if (mapping.VkFormat == format)
            {
                return mapping.RhiFormat;
            }
        }
        return Format::UNKNOWN;
    }
} // namespace NorvesLib::RHI::Vulkan
