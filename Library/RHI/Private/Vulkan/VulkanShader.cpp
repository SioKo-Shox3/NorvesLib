#include "VulkanShader.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanShader::VulkanShader(std::shared_ptr<VulkanDevice> device, const ShaderDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    // シェーダーモジュールを作成
    CreateShaderModule();
}

// デストラクタ
VulkanShader::~VulkanShader()
{
    if (m_shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device->GetVkDevice(), m_shaderModule, nullptr);
    }
}

// シェーダーステージフラグを取得
VkShaderStageFlags VulkanShader::GetVkShaderStageFlags() const
{
    return static_cast<VkShaderStageFlags>(ToVkShaderStage(m_desc.stage));
}

// シェーダーモジュールの作成
void VulkanShader::CreateShaderModule()
{
    // バイトコードが空の場合はエラー
    if (m_desc.bytecode.empty()) {
        throw std::runtime_error("シェーダーバイトコードが空です");
    }

    // シェーダーモジュール作成情報
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = m_desc.bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(m_desc.bytecode.data());

    // シェーダーモジュールの作成
    if (vkCreateShaderModule(m_device->GetVkDevice(), &createInfo, nullptr, &m_shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanシェーダーモジュールの作成に失敗しました");
    }
}

// シェーダーステージの変換
VkShaderStageFlagBits VulkanShader::ToVkShaderStage(ShaderStage stage) const
{
    switch (stage)
    {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Hull:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderStage::Domain:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case ShaderStage::Geometry:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderStage::Pixel:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    default:
        throw std::runtime_error("未対応のシェーダーステージです");
    }
}

} // namespace NorvesLib::RHI::Vulkan