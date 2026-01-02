#include "VulkanShader.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

    VulkanShader::VulkanShader(TSharedPtr<VulkanDevice> device, const ShaderDesc &desc)
        : m_device(device), m_desc(desc)
    {
        CreateShaderModule();
    }

    VulkanShader::~VulkanShader()
    {
        if (m_shaderModule)
        {
            m_device->GetVkDevice().destroyShaderModule(m_shaderModule);
            m_shaderModule = nullptr;
        }
    }

    vk::ShaderStageFlags VulkanShader::GetVkShaderStageFlags() const
    {
        return ToVkShaderStage(m_desc.stage);
    }

    void VulkanShader::CreateShaderModule()
    {
        if (m_desc.bytecode.empty())
        {
            throw std::runtime_error("シェーダーバイトコードが空です");
        }

        vk::ShaderModuleCreateInfo createInfo;
        createInfo.codeSize = m_desc.bytecode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t *>(m_desc.bytecode.data());

        auto result = m_device->GetVkDevice().createShaderModule(createInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkanシェーダーモジュールの作成に失敗しました");
        }
        m_shaderModule = result.value;
    }

    vk::ShaderStageFlagBits VulkanShader::ToVkShaderStage(ShaderStage stage) const
    {
        switch (stage)
        {
        case ShaderStage::Vertex:
            return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::Hull:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderStage::Domain:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case ShaderStage::Geometry:
            return vk::ShaderStageFlagBits::eGeometry;
        case ShaderStage::Pixel:
            return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::Compute:
            return vk::ShaderStageFlagBits::eCompute;
        default:
            throw std::runtime_error("未対応のシェーダーステージです");
        }
    }

} // namespace NorvesLib::RHI::Vulkan
