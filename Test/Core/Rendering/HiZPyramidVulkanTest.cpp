// 深度から作る HZB（HiZPyramid）の全ミップを読み戻し、各 texel が覆う深度の最大と一致することを確かめる。
// 既知の深度（R32_FLOAT。シェーダーは深度も同じ texelFetch の .r で読む）から、奇数の大きさ 37x23 を含む複数の解像度で作り、
// 期待は実装の縮め方を写さず、「ミップ m の texel が覆う深度の矩形」を直接取って最大を計算する。
// 矩形は、長さ 2^(m+1) の幅で並び、最後の列・行だけは深度の端まで伸びる（奇数ではみ出す行・列を含めて保守的にする）。
// 深度の入れ替えで結び直すこと、解像度の変更で作り直すことも続けて確かめる。
#include "Rendering/HiZPyramidPass.h"
#include "Rendering/ShaderManager.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "HiZPyramidVulkanTest";
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
        std::cout << TestName << " skipped: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // 決定的な擬似乱数（0 以上 1 未満の、float で正確に表せる値）。seed を変えると別の深度になる。
    float DepthValue(uint32_t x, uint32_t y, uint32_t seed)
    {
        uint32_t state = x * 73856093u ^ y * 19349663u ^ seed * 83492791u;
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        state *= 2654435761u;
        state ^= state >> 15u;
        return static_cast<float>(state >> 8u) / 16777216.0f;
    }

    VariableArray<float> MakeDepth(uint32_t width, uint32_t height, uint32_t seed)
    {
        VariableArray<float> depth;
        depth.resize(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                depth[static_cast<size_t>(y) * width + x] = DepthValue(x, y, seed);
            }
        }
        return depth;
    }

    // ミップ m の texel index（0..mipSize-1）が覆う深度の範囲 [begin, end]（両端を含む）
    void FootprintRange(uint32_t index, uint32_t mipSize, uint32_t mip, uint32_t depthSize, uint32_t& outBegin, uint32_t& outEnd)
    {
        outBegin = index << (mip + 1u);
        outEnd = (index + 1u == mipSize) ? depthSize - 1u : ((index + 1u) << (mip + 1u)) - 1u;
        outEnd = std::min(outEnd, depthSize - 1u);
    }

    float ExpectedTexel(const VariableArray<float>& depth,
                        uint32_t depthWidth,
                        uint32_t depthHeight,
                        uint32_t mipWidth,
                        uint32_t mipHeight,
                        uint32_t mip,
                        uint32_t x,
                        uint32_t y)
    {
        uint32_t beginX = 0;
        uint32_t endX = 0;
        uint32_t beginY = 0;
        uint32_t endY = 0;
        FootprintRange(x, mipWidth, mip, depthWidth, beginX, endX);
        FootprintRange(y, mipHeight, mip, depthHeight, beginY, endY);
        float maxDepth = 0.0f;
        for (uint32_t depthY = beginY; depthY <= endY; ++depthY)
        {
            for (uint32_t depthX = beginX; depthX <= endX; ++depthX)
            {
                maxDepth = std::max(maxDepth, depth[static_cast<size_t>(depthY) * depthWidth + depthX]);
            }
        }
        return maxDepth;
    }

    TexturePtr CreateDepthSource(const DevicePtr& device, uint32_t width, uint32_t height, const VariableArray<float>& depth)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.TextureFormat = Format::R32_FLOAT;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = "HiZPyramidTestDepth";
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        const uint32_t rowPitch = width * static_cast<uint32_t>(sizeof(float));
        texture->Update(depth.data(), rowPitch, rowPitch * height, 0, 0);
        return texture;
    }

    // HZB を作って全ミップを読み戻し、期待と一致するか確かめる。違う texel の数を返す（-1 は実行の失敗）。
    int BuildAndCompare(const DevicePtr& device,
                        HiZPyramid& pyramid,
                        const TexturePtr& depthTexture,
                        const VariableArray<float>& depth,
                        uint32_t depthWidth,
                        uint32_t depthHeight,
                        const char* label)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << label << ": コマンドリストを作れませんでした\n";
            return -1;
        }

        commandList->Begin();
        if (!pyramid.Build(commandList.get(), depthTexture))
        {
            std::cerr << label << ": HZB の生成を記録できませんでした\n";
            return -1;
        }

        const uint32_t mipCount = pyramid.GetMipCount();
        const uint32_t expectedWidth = (depthWidth + 1u) / 2u;
        const uint32_t expectedHeight = (depthHeight + 1u) / 2u;
        uint32_t longest = std::max(expectedWidth, expectedHeight);
        uint32_t expectedMipCount = 0;
        for (; longest > 0; longest >>= 1u)
        {
            ++expectedMipCount;
        }
        if (pyramid.GetWidth() != expectedWidth || pyramid.GetHeight() != expectedHeight || mipCount != expectedMipCount)
        {
            std::cerr << label << ": 大きさ・ミップ数が期待と違います (" << pyramid.GetWidth() << "x" << pyramid.GetHeight()
                      << ", " << mipCount << " mips)\n";
            return -1;
        }

        // 全ミップを 1 つのバッファへ並べて読み戻す
        VariableArray<uint64_t> mipOffsets;
        uint64_t totalBytes = 0;
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            mipOffsets.push_back(totalBytes);
            totalBytes += static_cast<uint64_t>(std::max(expectedWidth >> mip, 1u)) * std::max(expectedHeight >> mip, 1u) * sizeof(float);
        }
        BufferPtr readback = device->CreateBuffer(BufferDesc(totalBytes, ResourceUsage::TransferDst, true, "HiZPyramidReadback"));
        if (!readback)
        {
            std::cerr << label << ": 読み戻しのバッファを作れませんでした\n";
            return -1;
        }

        commandList->TextureBarrier(pyramid.GetTexture(), ResourceState::ShaderResource, ResourceState::CopySource, 0, 0, mipCount, 0);
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            commandList->CopyTextureToBuffer(pyramid.GetTexture(),
                                             readback,
                                             std::max(expectedWidth >> mip, 1u),
                                             std::max(expectedHeight >> mip, 1u),
                                             mipOffsets[mip],
                                             mip,
                                             0);
        }
        commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, totalBytes);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        const uint8_t* mapped = static_cast<const uint8_t*>(readback->Map(0u, totalBytes));
        if (mapped == nullptr)
        {
            std::cerr << label << ": 読み戻しのバッファを写像できませんでした\n";
            return -1;
        }

        int mismatchCount = 0;
        uint32_t checkedCount = 0;
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t mipWidth = std::max(expectedWidth >> mip, 1u);
            const uint32_t mipHeight = std::max(expectedHeight >> mip, 1u);
            for (uint32_t y = 0; y < mipHeight; ++y)
            {
                for (uint32_t x = 0; x < mipWidth; ++x)
                {
                    float actual = 0.0f;
                    std::memcpy(&actual, mapped + mipOffsets[mip] + (static_cast<uint64_t>(y) * mipWidth + x) * sizeof(float), sizeof(float));
                    const float expected = ExpectedTexel(depth, depthWidth, depthHeight, mipWidth, mipHeight, mip, x, y);
                    ++checkedCount;
                    if (actual != expected)
                    {
                        if (mismatchCount < 8)
                        {
                            std::cerr << label << ": mip=" << mip << " (" << x << "," << y << ") 実際=" << actual << " 期待=" << expected << '\n';
                        }
                        ++mismatchCount;
                    }
                }
            }
        }
        readback->Unmap();

        std::cout << label << " 深度=" << depthWidth << "x" << depthHeight << " HZB=" << pyramid.GetWidth() << "x"
                  << pyramid.GetHeight() << " mips=" << mipCount << " 確認した texel=" << checkedCount
                  << " 不一致=" << mismatchCount << '\n';
        return mismatchCount;
    }

    struct SizeCase
    {
        uint32_t Width;
        uint32_t Height;
    };

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 was set.");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }

        bool bPassed = true;
        {
            HiZPyramid pyramid;
            if (!pyramid.Initialize(device.get(), &shaderManager))
            {
                std::cerr << "HiZPyramid を初期化できませんでした\n";
                return 1;
            }

            // 奇数の大きさ（37x23）、奇数の幅だけ・高さだけ、2 のべき乗、最小（1x1）。同じ HiZPyramid で解像度を替えて続ける。
            const SizeCase cases[] = {{37, 23}, {64, 32}, {9, 65}, {1, 1}, {130, 7}, {37, 23}};
            uint32_t seed = 1;
            for (const SizeCase& sizeCase : cases)
            {
                const VariableArray<float> depth = MakeDepth(sizeCase.Width, sizeCase.Height, seed);
                TexturePtr source = CreateDepthSource(device, sizeCase.Width, sizeCase.Height, depth);
                if (!source)
                {
                    std::cerr << "深度のテクスチャを作れませんでした\n";
                    return 1;
                }
                const int mismatch = BuildAndCompare(device, pyramid, source, depth, sizeCase.Width, sizeCase.Height, "HZB");
                bPassed = bPassed && mismatch == 0;

                // 同じ解像度で別の深度（別のテクスチャ）へ入れ替える。結び直されて、新しい深度の HZB になる。
                const VariableArray<float> nextDepth = MakeDepth(sizeCase.Width, sizeCase.Height, seed + 100u);
                TexturePtr nextSource = CreateDepthSource(device, sizeCase.Width, sizeCase.Height, nextDepth);
                if (!nextSource)
                {
                    std::cerr << "深度のテクスチャを作れませんでした\n";
                    return 1;
                }
                const int nextMismatch = BuildAndCompare(device, pyramid, nextSource, nextDepth, sizeCase.Width, sizeCase.Height, "HZB(入れ替え)");
                bPassed = bPassed && nextMismatch == 0;
                ++seed;
            }

            device->WaitIdle();
            pyramid.Shutdown();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        if (validationErrorCount != 0u)
        {
            std::cerr << "Vulkan validation errorを検出しました: " << validationErrorCount << '\n';
            bPassed = false;
        }

        std::cout << (bPassed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return bPassed ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
