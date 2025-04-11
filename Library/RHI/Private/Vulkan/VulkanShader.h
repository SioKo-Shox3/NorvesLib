#pragma once

#include "RHI/Public/IShader.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanシェーダー実装クラス
 */
class VulkanShader : public IShader
{
public:
    /**
     * @brief VulkanShaderのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc シェーダー記述子
     */
    VulkanShader(std::shared_ptr<VulkanDevice> device, const ShaderDesc& desc);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanShader() override;

    // IShaderインターフェース実装
    ShaderStage GetStage() const override { return m_desc.stage; }
    const std::string& GetEntryPoint() const override { return m_desc.entryPoint; }
    const std::vector<uint8_t>& GetBytecode() const override { return m_desc.bytecode; }
    const std::string& GetSourceCode() const override { return m_desc.sourceCode; }
    void* GetNativeHandle() const override { return reinterpret_cast<void*>(m_shaderModule); }

    // Vulkan固有のメソッド
    VkShaderModule GetVkShaderModule() const { return m_shaderModule; }
    VkShaderStageFlags GetVkShaderStageFlags() const;

private:
    std::shared_ptr<VulkanDevice> m_device;
    ShaderDesc m_desc;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;
    
    // シェーダーモジュールの作成
    void CreateShaderModule();
    
    // ShaderStage → VkShaderStageFlagBits変換
    VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage) const;
};

} // namespace NorvesLib::RHI::Vulkan