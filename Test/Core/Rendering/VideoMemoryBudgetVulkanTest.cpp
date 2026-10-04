// Vulkan デバイスの VRAM 予算・使用量の取得（IDevice::GetVideoMemoryBudget）の契約テスト。
// VK_EXT_memory_budget のある GPU で budget>0・usage>0・usage<=budget を確かめる。
// Vulkan デバイスが無い、または拡張が無い環境では 125（スキップ）を返す。
#include "CoreTypes.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace NorvesLib
{
namespace
{

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
    std::cout << "VideoMemoryBudgetVulkanTest skipped: " << reason << std::endl;
    return GpuTestSkipReturnCode;
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
        return SkipGpuTest("no Vulkan device is available for the video memory budget test.");
    }

    // 使用量が 0 のままにならないよう、先に VRAM を使うテクスチャを1枚作っておく。
    RHI::TextureDesc textureDesc;
    textureDesc.Width = 1024;
    textureDesc.Height = 1024;
    textureDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
    textureDesc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::TransferSrc;
    textureDesc.DebugName = "VideoMemoryBudgetProbe";
    RHI::TexturePtr probeTexture = device->CreateTexture(textureDesc);
    if (!IsValid(probeTexture))
    {
        std::cerr << "VideoMemoryBudgetVulkanTest failed: probe texture creation failed" << std::endl;
        return 1;
    }

    const RHI::VideoMemoryBudget budget = device->GetVideoMemoryBudget();
    if (!budget.bValid)
    {
        return SkipGpuTest("VK_EXT_memory_budget is not available on this device.");
    }

    std::cout << "VideoMemoryBudgetVulkanTest budget_bytes=" << budget.BudgetBytes
              << " usage_bytes=" << budget.UsageBytes << std::endl;

    if (budget.BudgetBytes == 0)
    {
        std::cerr << "VideoMemoryBudgetVulkanTest failed: budget is 0" << std::endl;
        return 1;
    }
    if (budget.UsageBytes == 0)
    {
        std::cerr << "VideoMemoryBudgetVulkanTest failed: usage is 0" << std::endl;
        return 1;
    }
    if (budget.UsageBytes > budget.BudgetBytes)
    {
        std::cerr << "VideoMemoryBudgetVulkanTest failed: usage exceeds budget" << std::endl;
        return 1;
    }

    std::cout << "VideoMemoryBudgetVulkanTest passed" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
