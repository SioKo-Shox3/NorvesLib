#pragma once

#include "RHI/Public/IShader.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanシェーダー実装クラス (vulkan.hpp使用)
 */
class VulkanShader : public IShader
{
public:
    /**
     * @brief VulkanShaderのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc シェーダー記述子
     */
    VulkanShader(TSharedPtr<VulkanDevice> device, const ShaderDesc& desc);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanShader() override;

    // IShaderインターフェース実装
    ShaderStage GetStage() const override { return m_desc.stage; }
    const NorvesLib::Core::Container::String& GetEntryPoint() const override { return m_desc.entryPoint; }
    const NorvesLib::Core::Container::VariableArray<uint8_t>& GetBytecode() const override { return m_desc.bytecode; }
    const NorvesLib::Core::Container::String& GetSourceCode() const override { return m_desc.sourceCode; }
    void* GetNativeHandle() const override { return reinterpret_cast<void*>(static_cast<VkShaderModule>(m_shaderModule)); }

    // Vulkan固有のメソッド (vulkan.hpp型)
    vk::ShaderModule GetVkShaderModule() const { return m_shaderModule; }
    vk::ShaderStageFlags GetVkShaderStageFlags() const;

private:
    TSharedPtr<VulkanDevice> m_device;
    ShaderDesc m_desc;
    vk::ShaderModule m_shaderModule;
    
    void CreateShaderModule();
    vk::ShaderStageFlagBits ToVkShaderStage(ShaderStage stage) const;
};

} // namespace NorvesLib::RHI::Vulkan