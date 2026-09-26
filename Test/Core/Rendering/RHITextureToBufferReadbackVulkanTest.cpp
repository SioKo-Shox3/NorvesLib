#include "CoreTypes.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"

#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace NorvesLib
{
namespace
{

constexpr uint32_t TextureWidth = 3;
constexpr uint32_t TextureHeight = 2;
constexpr uint32_t BytesPerPixel = 4;
constexpr uint64_t ReadbackBytes = TextureWidth * TextureHeight * BytesPerPixel;
constexpr int GpuTestSkipReturnCode = 125;

bool IsGpuTestSkipForced()
{
    char* forceSkip = nullptr;
    size_t forceSkipLength = 0;
    if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
    {
        return false;
    }

    const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
    free(forceSkip);
    return bForceSkip;
}

int SkipGpuTest(const char* reason)
{
    std::cout << "RHITextureToBufferReadbackVulkanTest skipped: " << reason << std::endl;
    return GpuTestSkipReturnCode;
}

RHI::TextureDesc MakeColorTextureDesc()
{
    RHI::TextureDesc desc;
    desc.Width = TextureWidth;
    desc.Height = TextureHeight;
    desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
    desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::TransferSrc;
    desc.DebugName = "RHITextureToBufferReadbackColor";
    return desc;
}

RHI::BufferDesc MakeReadbackBufferDesc()
{
    RHI::BufferDesc desc;
    desc.Size = ReadbackBytes;
    desc.Usage = RHI::ResourceUsage::TransferDst;
    desc.CPUAccessible = true;
    desc.DebugName = "RHITextureToBufferReadbackBuffer";
    return desc;
}

RHI::RenderPassDesc MakeRenderPassDesc()
{
    RHI::AttachmentDesc attachment;
    attachment.format = RHI::Format::R8G8B8A8_UNORM;
    attachment.clearColor[0] = 1.0f;
    attachment.clearColor[1] = 0.0f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 1.0f;
    attachment.loadOp = RHI::AttachmentLoadOp::Clear;
    attachment.storeOp = RHI::AttachmentStoreOp::Store;
    attachment.initialState = RHI::ResourceState::Undefined;
    attachment.finalState = RHI::ResourceState::RenderTarget;

    RHI::RenderPassDesc desc;
    desc.colorAttachments.push_back(attachment);
    desc.hasDepthStencil = false;
    return desc;
}

RHI::FramebufferDesc MakeFramebufferDesc(RHI::RenderPassPtr renderPass, RHI::TexturePtr colorTexture)
{
    RHI::FramebufferDesc desc;
    desc.renderPass = renderPass;
    desc.colorTargets.push_back(colorTexture);
    desc.width = TextureWidth;
    desc.height = TextureHeight;
    return desc;
}

bool VerifyPixels(const uint8_t* pixels)
{
    for (uint32_t pixelIndex = 0; pixelIndex < TextureWidth * TextureHeight; ++pixelIndex)
    {
        const uint32_t byteIndex = pixelIndex * BytesPerPixel;
        if (pixels[byteIndex + 0] != 255 ||
            pixels[byteIndex + 1] != 0 ||
            pixels[byteIndex + 2] != 0 ||
            pixels[byteIndex + 3] != 255)
        {
            std::cerr << "Unexpected pixel at index " << pixelIndex
                      << ": {"
                      << static_cast<uint32_t>(pixels[byteIndex + 0]) << ", "
                      << static_cast<uint32_t>(pixels[byteIndex + 1]) << ", "
                      << static_cast<uint32_t>(pixels[byteIndex + 2]) << ", "
                      << static_cast<uint32_t>(pixels[byteIndex + 3]) << "}"
                      << std::endl;
            return false;
        }
    }

    return true;
}

int RunTest()
{
    if (IsGpuTestSkipForced())
    {
        return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 was set.");
    }

    RHI::RHIDeviceDesc desc;
    desc.Api = RHI::GraphicsAPI::Vulkan;
    desc.bEnableValidation = false;

    RHI::DevicePtr device = RHI::CreateRHIDevice(desc);
    if (!IsValid(device))
    {
        return SkipGpuTest("no Vulkan device is available for the texture readback test.");
    }

    assert(device->GetAPI() == RHI::API::Vulkan);

    RHI::TexturePtr colorTexture = device->CreateTexture(MakeColorTextureDesc());
    assert(IsValid(colorTexture));

    RHI::BufferPtr readbackBuffer = device->CreateBuffer(MakeReadbackBufferDesc());
    assert(IsValid(readbackBuffer));
    assert(readbackBuffer->GetSize() == ReadbackBytes);

    void* prefillData = readbackBuffer->Map(0, ReadbackBytes);
    assert(prefillData != nullptr);
    std::memset(prefillData, 0xCD, static_cast<size_t>(ReadbackBytes));
    readbackBuffer->Unmap();

    RHI::RenderPassPtr renderPass = device->CreateRenderPass(MakeRenderPassDesc());
    assert(IsValid(renderPass));

    RHI::FramebufferPtr framebuffer =
        device->CreateFramebuffer(MakeFramebufferDesc(renderPass, colorTexture));
    assert(IsValid(framebuffer));

    RHI::CommandListPtr commandList = device->CreateCommandList();
    assert(IsValid(commandList));

    commandList->Begin();
    commandList->BeginRenderPass(renderPass, framebuffer);
    commandList->EndRenderPass();
    commandList->TextureBarrier(
        colorTexture,
        RHI::ResourceState::RenderTarget,
        RHI::ResourceState::CopySource);
    commandList->CopyTextureToBuffer(colorTexture, readbackBuffer, TextureWidth, TextureHeight, 0);
    commandList->End();
    commandList->Submit(true);
    device->WaitIdle();

    void* mappedData = readbackBuffer->Map(0, ReadbackBytes);
    assert(mappedData != nullptr);
    const bool bPixelsMatch = VerifyPixels(static_cast<const uint8_t*>(mappedData));
    readbackBuffer->Unmap();

    if (!bPixelsMatch)
    {
        return 1;
    }

    std::cout << "RHITextureToBufferReadbackVulkanTest passed" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
