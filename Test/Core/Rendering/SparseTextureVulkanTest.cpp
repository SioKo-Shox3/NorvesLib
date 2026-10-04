// sparse（部分常駐）テクスチャの作成と、タイル・ミップテイルの情報（ITexture::GetSparseInfo）の契約テスト。
// 4096x4096 の BC7 を物理メモリを結ばずに作り、タイルの大きさ・ミップごとのタイルの数・ミップテイルの位置を確かめる。
// sparse に対応しない形式・用途の作成が失敗することも確かめる。
// Vulkan デバイスが無い、または sparse の 2D residency が無い環境では 125（スキップ）を返す。
#include "CoreTypes.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
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
    std::cout << "SparseTextureVulkanTest スキップ: " << reason << std::endl;
    return GpuTestSkipReturnCode;
}

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "SparseTextureVulkanTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

RHI::TextureDesc MakeSparseDesc(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format, const char* name)
{
    RHI::TextureDesc desc;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.TextureFormat = format;
    desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
    desc.bSparse = true;
    desc.DebugName = name;
    return desc;
}

void TestBc7Sparse(RHI::IDevice& device)
{
    // 4096x4096 の BC7 は、ミップ 0 が 256x256 texel のタイルで 16x16 枚。全ミップは 13 段。
    constexpr uint32_t Width = 4096;
    constexpr uint32_t MipLevels = 13;
    RHI::TexturePtr texture =
        device.CreateTexture(MakeSparseDesc(Width, Width, MipLevels, RHI::Format::BC7_UNORM, "SparseTextureTestBC7"));
    Expect(texture != nullptr, "4096x4096 の BC7 の sparse テクスチャを作れなければならない");
    if (texture == nullptr)
    {
        return;
    }

    Expect(texture->IsSparse(), "sparse で作ったテクスチャは IsSparse が true でなければならない");
    Expect(texture->GetWidth() == Width && texture->GetHeight() == Width && texture->GetMipLevels() == MipLevels,
           "大きさとミップ数は作成時のとおりでなければならない");
    Expect(texture->GetSparseBoundBytes() == 0, "作成した時点では何も結んでいない");

    RHI::SparseTextureInfo info;
    Expect(texture->GetSparseInfo(info), "sparse テクスチャは GetSparseInfo が成功しなければならない");

    std::cout << "SparseTextureVulkanTest BC7 tile=" << info.TileWidth << "x" << info.TileHeight
              << " tileBytes=" << info.TileSizeBytes << " mips=" << info.MipLevels
              << " tailFirst=" << info.MipTailFirstLevel << " tailSize=" << info.MipTailSize
              << " tailOffset=" << info.MipTailOffset << " tailStride=" << info.MipTailStride << std::endl;
    for (uint32_t mip = 0; mip < info.MipLevels; ++mip)
    {
        std::cout << "SparseTextureVulkanTest   mip" << mip << " tiles=" << info.TilesX[mip] << "x" << info.TilesY[mip] << std::endl;
    }

    Expect(info.TileWidth == 256 && info.TileHeight == 256, "BC7 のタイルは 256x256 texel でなければならない");
    Expect(info.TileSizeBytes == 65536, "タイルは 64 KiB でなければならない");
    Expect(info.MipLevels == MipLevels, "情報のミップ数は作成時のとおりでなければならない");

    // ミップ 0 は 16x16、以降は 1 段ごとに半分（256x256 texel 以上のミップだけがタイルを持つ）。
    Expect(info.TilesX[0] == 16 && info.TilesY[0] == 16, "ミップ 0 は 16x16 枚のタイルでなければならない");
    Expect(info.TilesX[1] == 8 && info.TilesY[1] == 8, "ミップ 1 は 8x8 枚");
    Expect(info.TilesX[2] == 4 && info.TilesY[2] == 4, "ミップ 2 は 4x4 枚");
    Expect(info.TilesX[3] == 2 && info.TilesY[3] == 2, "ミップ 3 は 2x2 枚");
    Expect(info.TilesX[4] == 1 && info.TilesY[4] == 1, "ミップ 4（256x256 texel）は 1 枚");

    // 標準のブロック形状では、タイルに満たない 128x128 texel のミップ 5 からがミップテイル。
    Expect(info.MipTailFirstLevel == 5, "ミップテイルはミップ 5 から始まらなければならない");
    Expect(info.MipTailSize > 0 && info.MipTailSize % 65536 == 0, "ミップテイルの大きさは 64 KiB の倍数でなければならない");
    for (uint32_t mip = info.MipTailFirstLevel; mip < RHI::SparseTextureInfo::MaxMipLevels; ++mip)
    {
        Expect(info.TilesX[mip] == 0 && info.TilesY[mip] == 0, "ミップテイル以降のミップはタイルを持たない");
    }
}

void TestOtherFormats(RHI::IDevice& device)
{
    // 形式ごとのタイルの大きさ（DeviceCapabilities と同じ）。
    RHI::TexturePtr bc4 = device.CreateTexture(MakeSparseDesc(2048, 2048, 12, RHI::Format::BC4_UNORM, "SparseTextureTestBC4"));
    Expect(bc4 != nullptr, "BC4 の sparse テクスチャを作れなければならない");
    if (bc4 != nullptr)
    {
        RHI::SparseTextureInfo info;
        Expect(bc4->GetSparseInfo(info) && info.TileWidth == 512 && info.TileHeight == 256 && info.TilesX[0] == 4 && info.TilesY[0] == 8,
               "2048x2048 の BC4 は 512x256 のタイルで 4x8 枚でなければならない");
    }

    RHI::TexturePtr r16 = device.CreateTexture(MakeSparseDesc(1024, 1024, 11, RHI::Format::R16_UNORM, "SparseTextureTestR16"));
    Expect(r16 != nullptr, "R16 の sparse テクスチャを作れなければならない");
    if (r16 != nullptr)
    {
        RHI::SparseTextureInfo info;
        Expect(r16->GetSparseInfo(info) && info.TileWidth == 256 && info.TileHeight == 128 && info.TilesX[0] == 4 && info.TilesY[0] == 8,
               "1024x1024 の R16 は 256x128 のタイルで 4x8 枚でなければならない");
    }
}

void TestNonSparseAndRejections(RHI::IDevice& device)
{
    // 通常のテクスチャは sparse ではなく、情報を返さない。
    RHI::TextureDesc plainDesc;
    plainDesc.Width = 64;
    plainDesc.Height = 64;
    plainDesc.DebugName = "SparseTextureTestPlain";
    RHI::TexturePtr plain = device.CreateTexture(plainDesc);
    Expect(plain != nullptr, "通常のテクスチャは従来どおり作れなければならない");
    if (plain != nullptr)
    {
        RHI::SparseTextureInfo info;
        info.TileWidth = 7;
        Expect(!plain->IsSparse(), "通常のテクスチャは IsSparse が false");
        Expect(!plain->GetSparseInfo(info) && info.TileWidth == 7, "通常のテクスチャは GetSparseInfo が失敗し、出力を変えない");
        Expect(plain->GetSparseBoundBytes() == 0, "通常のテクスチャの結んだ量は 0");
    }

    // sparse に対応しない形式・用途は、作成が失敗する（理由はログに出る）。
    RHI::TextureDesc noStandardFormat = MakeSparseDesc(1024, 1024, 11, RHI::Format::R32_FLOAT, "SparseTextureTestUnsupportedFormat");
    Expect(device.CreateTexture(noStandardFormat) == nullptr, "標準ブロック形状を照会していない形式の sparse は作成が失敗しなければならない");

    RHI::TextureDesc arrayDesc = MakeSparseDesc(1024, 1024, 11, RHI::Format::BC7_UNORM, "SparseTextureTestArray");
    arrayDesc.ArraySize = 2;
    Expect(device.CreateTexture(arrayDesc) == nullptr, "配列の sparse は作成が失敗しなければならない");

    RHI::TextureDesc renderTargetDesc = MakeSparseDesc(1024, 1024, 1, RHI::Format::BC7_UNORM, "SparseTextureTestRenderTarget");
    renderTargetDesc.Usage = renderTargetDesc.Usage | RHI::ResourceUsage::RenderTarget;
    Expect(device.CreateTexture(renderTargetDesc) == nullptr, "レンダーターゲットの sparse は作成が失敗しなければならない");

    RHI::TextureDesc tooManyMips = MakeSparseDesc(4096, 4096, RHI::SparseTextureInfo::MaxMipLevels + 1, RHI::Format::BC7_UNORM,
                                                  "SparseTextureTestTooManyMips");
    Expect(device.CreateTexture(tooManyMips) == nullptr, "持てるミップ数を超える sparse は作成が失敗しなければならない");
}

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
    if (!sparse.bSparseBinding || !sparse.bResidencyImage2D)
    {
        return SkipGpuTest("sparse の結び付けまたは 2D の residency が無いデバイス（VT は使わず BC の全常駐で描く）");
    }

    TestBc7Sparse(*device);
    TestOtherFormats(*device);
    TestNonSparseAndRejections(*device);

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "SparseTextureVulkanTest 合格" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
