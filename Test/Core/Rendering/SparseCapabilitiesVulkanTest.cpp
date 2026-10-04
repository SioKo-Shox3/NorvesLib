// Vulkan デバイスの sparse（部分常駐）テクスチャの機能照会（DeviceCapabilities::Sparse）の契約テスト。
// sparse の可否と、BC7・BC5・BC4・R16 の 2D の標準ブロック形状（64 KiB のタイル）を確かめる。
// Vulkan デバイスが無い、または sparse の 2D residency が無い環境では 125（スキップ）を返す。
#include "CoreTypes.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"

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
    std::cout << "SparseCapabilitiesVulkanTest skipped: " << reason << std::endl;
    return GpuTestSkipReturnCode;
}

struct ExpectedTile
{
    RHI::Format TextureFormat;
    const char* Name;
    uint32_t Width;
    uint32_t Height;
};

int RunTest()
{
    if (IsGpuTestSkipForced())
    {
        return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
    }

    RHI::RHIDeviceDesc desc;
    desc.Api = RHI::GraphicsAPI::Vulkan;
    desc.bEnableValidation = false;

    RHI::DevicePtr device = RHI::CreateRHIDevice(desc);
    if (!IsValid(device))
    {
        return SkipGpuTest("Vulkan デバイスが無い");
    }

    const RHI::SparseCapabilities& sparse = device->GetCapabilities().Sparse;
    std::cout << "SparseCapabilitiesVulkanTest binding=" << sparse.bSparseBinding
              << " residency2D=" << sparse.bResidencyImage2D
              << " aliased=" << sparse.bResidencyAliased
              << " shaderResidency=" << sparse.bShaderResourceResidency
              << " minLod=" << sparse.bShaderResourceMinLod
              << " formats=" << sparse.FormatCount << std::endl;

    if (!sparse.bSparseBinding || !sparse.bResidencyImage2D)
    {
        return SkipGpuTest("sparse の結び付けまたは 2D の residency が無いデバイス（VT は使わず BC の全常駐で描く）");
    }

    // 開発機（RTX 4080）では shader の residency と MinLod も使える。
    if (!sparse.bShaderResourceResidency || !sparse.bShaderResourceMinLod)
    {
        std::cerr << "SparseCapabilitiesVulkanTest failed: shaderResourceResidency / shaderResourceMinLod が無効" << std::endl;
        return 1;
    }

    // 64 KiB のタイル: BC7・BC5 は 16 バイト/4x4 で 256x256、BC4 は 8 バイト/4x4 で 512x256、R16 は 2 バイト/texel で 256x128。
    const ExpectedTile expectedTiles[] = {
        {RHI::Format::BC7_UNORM, "BC7_UNORM", 256, 256},
        {RHI::Format::BC7_SRGB, "BC7_SRGB", 256, 256},
        {RHI::Format::BC5_UNORM, "BC5_UNORM", 256, 256},
        {RHI::Format::BC4_UNORM, "BC4_UNORM", 512, 256},
        {RHI::Format::R16_UNORM, "R16_UNORM", 256, 128},
    };

    for (const ExpectedTile& expected : expectedTiles)
    {
        const RHI::SparseFormatProperties* format = sparse.FindFormat(expected.TextureFormat);
        if (format == nullptr)
        {
            std::cerr << "SparseCapabilitiesVulkanTest failed: " << expected.Name << " を照会していない" << std::endl;
            return 1;
        }

        std::cout << "SparseCapabilitiesVulkanTest " << expected.Name << " supported=" << format->bSupported
                  << " granularity=" << format->GranularityWidth << "x" << format->GranularityHeight
                  << " standard=" << format->bStandardBlockShape << std::endl;

        if (!format->bSupported)
        {
            std::cerr << "SparseCapabilitiesVulkanTest failed: " << expected.Name << " の 2D の sparse が使えない" << std::endl;
            return 1;
        }
        if (!format->bStandardBlockShape)
        {
            std::cerr << "SparseCapabilitiesVulkanTest failed: " << expected.Name << " が標準のブロック形状ではない" << std::endl;
            return 1;
        }
        if (format->GranularityWidth != expected.Width || format->GranularityHeight != expected.Height)
        {
            std::cerr << "SparseCapabilitiesVulkanTest failed: " << expected.Name << " のタイルが期待の "
                      << expected.Width << "x" << expected.Height << " ではない" << std::endl;
            return 1;
        }
    }

    if (sparse.FindFormat(RHI::Format::UNKNOWN) != nullptr)
    {
        std::cerr << "SparseCapabilitiesVulkanTest failed: 未知の形式を引けてしまう" << std::endl;
        return 1;
    }

    std::cout << "SparseCapabilitiesVulkanTest passed" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
