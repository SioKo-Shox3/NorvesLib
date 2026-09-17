#include "RHI/IFramebuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace RHI = NorvesLib::RHI;

namespace
{
    int g_FailureCount = 0;

    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    bool ContainsText(const std::string& source, const std::string& text)
    {
        return source.find(text) != std::string::npos;
    }

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++g_FailureCount;
            std::cout << "FAILED: " << message << "\n";
        }
    }

    void ExpectContains(const std::string& source, const char* text, const char* message)
    {
        Expect(ContainsText(source, text), message);
    }

    void TestDescriptorDefaultsAndLayerRange()
    {
        RHI::TextureDesc shadowDesc = RHI::TextureDesc::DepthStencil(
            128,
            128,
            RHI::Format::D32_FLOAT,
            "ShadowMap");
        shadowDesc.ArraySize = 4;
        shadowDesc.Dimension = RHI::TextureDimension::Texture2D;

        Expect(shadowDesc.ArraySize == 4, "shadow texture descriptor has four layers");
        Expect(shadowDesc.TextureFormat == RHI::Format::D32_FLOAT,
               "shadow texture descriptor uses D32_FLOAT");
        Expect((shadowDesc.Usage & RHI::ResourceUsage::DepthStencil) != RHI::ResourceUsage::None,
               "shadow texture descriptor has depth-stencil usage");
        Expect((shadowDesc.Usage & RHI::ResourceUsage::ShaderResource) != RHI::ResourceUsage::None,
               "shadow texture descriptor remains shader-readable");

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.depthStencilArrayLayer = 3;
        Expect(framebufferDesc.depthStencilArrayLayer == 3,
               "framebuffer descriptor stores a zero-based depth layer");
    }

    void TestSourceContracts(const std::string& sourceDir)
    {
        const std::string publicRhiDir = sourceDir + "/Library/Core/Public/RHI";
        const std::string privateVulkanDir = sourceDir + "/Library/Core/Private/RHI/Vulkan";
        const std::string privateRenderingDir = sourceDir + "/Library/Core/Private/Rendering";

        const std::string framebufferInterface = ReadTextFile(publicRhiDir + "/IFramebuffer.h");
        const std::string deviceInterface = ReadTextFile(publicRhiDir + "/IDevice.h");
        const std::string vulkanTexture = ReadTextFile(privateVulkanDir + "/VulkanTexture.cpp");
        const std::string vulkanFramebuffer = ReadTextFile(privateVulkanDir + "/VulkanFramebuffer.cpp");
        const std::string shadowMapPass = ReadTextFile(privateRenderingDir + "/ShadowMapPass.cpp");

        ExpectContains(framebufferInterface,
                       "GetDepthStencilArrayLayer",
                       "IFramebuffer exposes the selected depth array layer");
        ExpectContains(deviceInterface,
                       "depthStencilArrayLayer = 0",
                       "FramebufferDesc defaults the selected depth layer to zero");
        ExpectContains(vulkanTexture,
                       "GetArrayLayerImageView",
                       "VulkanTexture provides per-array-layer views");
        ExpectContains(vulkanTexture,
                       "baseArrayLayer = arrayLayer",
                       "per-layer views select the requested array layer");
        ExpectContains(vulkanTexture,
                       "layerCount = 1",
                       "per-layer views contain exactly one array layer");
        ExpectContains(vulkanFramebuffer,
                       "depthStencilArrayLayer >= m_desc.depthStencilTarget->GetArraySize()",
                       "VulkanFramebuffer rejects out-of-range depth layers");
        ExpectContains(vulkanFramebuffer,
                       "GetArrayLayerImageView(arrayLayer)",
                       "VulkanFramebuffer attaches the selected layer view");
        ExpectContains(vulkanFramebuffer,
                       "framebufferInfo.layers = 1",
                       "VulkanFramebuffer remains a one-layer framebuffer");
        ExpectContains(shadowMapPass,
                       "shadowMapDesc.ArraySize = CSM_CASCADE_COUNT",
                       "ShadowMapPass creates a four-layer depth array");
        ExpectContains(shadowMapPass,
                       "m_ShadowFramebuffers.reserve(CSM_CASCADE_COUNT)",
                       "ShadowMapPass allocates one framebuffer per cascade");
        ExpectContains(shadowMapPass,
                       "fbDesc.depthStencilArrayLayer = cascadeIndex",
                       "ShadowMapPass assigns each framebuffer its layer");
        ExpectContains(shadowMapPass,
                       "m_bInitialized &&",
                       "ShadowMapPass does not publish partially initialized resources");
        ExpectContains(shadowMapPass,
                       "context.PhysicalLighting.PublishCascadedShadow",
                       "ShadowMapPass records all cascaded matrices");
        ExpectContains(shadowMapPass,
                       "m_ShadowFramebuffers[cascadeIndex]",
                       "ShadowMapPass draws into the matching cascade framebuffer");
        Expect(!ContainsText(shadowMapPass, "m_ShadowFramebuffer,") &&
                   !ContainsText(shadowMapPass, "m_ShadowFramebuffer)") &&
                   !ContainsText(shadowMapPass, "m_ShadowFramebuffer;") ,
               "ShadowMapPass has no stale single-framebuffer ownership path");
    }
} // namespace

int main()
{
    std::cout << "ShadowMapArrayLayerContractTest start\n";

#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for ShadowMapArrayLayerContractTest.
#endif

    const std::string sourceDir = NORVES_SOURCE_DIR;
    TestDescriptorDefaultsAndLayerRange();
    TestSourceContracts(sourceDir);

    if (g_FailureCount != 0)
    {
        std::cout << "ShadowMapArrayLayerContractTest failed with "
                  << g_FailureCount << " failure(s)\n";
        return 1;
    }

    std::cout << "ShadowMapArrayLayerContractTest passed\n";
    return 0;
}
