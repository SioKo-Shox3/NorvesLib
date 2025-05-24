// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\RHI\Private\Vulkan\VulkanRHI.h
#pragma once

#include "RHI/Public/RHITypes.h"
#include "VulkanDevice.h"

namespace NorvesLib::RHI::Vulkan 
{

/**
 * @brief Vulkan実装の初期化パラメータ
 */
struct VulkanInitParams 
{
    bool enableValidation = true;  // バリデーションレイヤーを有効にするかどうか
    bool preferIntegratedGPU = false; // 統合GPUを優先するかどうか
};

// VulkanDeviceの拡張: RHIContextで使用するためのファクトリメソッドを追加
inline DevicePtr VulkanDevice::Create(const VulkanInitParams& params = {}) 
{
    // VulkanDeviceのインスタンスを作成
    return std::make_shared<VulkanDevice>(params.enableValidation);
}

} // namespace NorvesLib::RHI::Vulkan