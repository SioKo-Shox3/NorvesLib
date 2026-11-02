// 歪みの合成がゼロを保ち、画面の高さを単位として指定方向へずらすことを実 GPU で確かめる。
// 幅と高さを異ならせ、ずらしの単位が画面の高さであることを反証する。
#include "Rendering/DistortionPass.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "DistortionPassVulkanTest";
    constexpr uint32_t Width = 96u;
    constexpr uint32_t Height = 64u;
    constexpr uint32_t PixelCount = Width * Height;
    constexpr uint32_t ExpectedShiftX = 4u;
    constexpr uint32_t ExpectedShiftY = 2u;
    constexpr uint64_t ColorBytes = static_cast<uint64_t>(PixelCount) * 4u * sizeof(uint16_t);
    constexpr uint64_t OffsetBytes = static_cast<uint64_t>(PixelCount) * 2u * sizeof(uint16_t);
    constexpr float ShiftTolerance = 1.0e-3f;

    // この試験で使うゼロと通常の半精度の範囲内の値を符号付きで変換する。
    uint16_t FloatToHalf(float value)
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = (bits >> 16u) & 0x8000u;
        const int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
        if (exponent <= 0 || exponent >= 31)
        {
            return static_cast<uint16_t>(sign);
        }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) |
                                     ((bits & 0x7fffffu) >> 13u));
    }

    float HalfToFloat(uint16_t bits)
    {
        const float sign = (bits & 0x8000u) ? -1.0f : 1.0f;
        const uint32_t exponent = (bits >> 10u) & 0x1fu;
        const uint32_t mantissa = bits & 0x3ffu;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 0x1fu)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                 static_cast<int>(exponent) - 15);
    }

    void PrintPixel(const uint16_t (&pixel)[4])
    {
        std::cout << '(';
        for (uint32_t channel = 0u; channel < 4u; ++channel)
        {
            if (channel != 0u)
            {
                std::cout << ',';
            }
            std::cout << HalfToFloat(pixel[channel]) << "[0x" << std::hex << pixel[channel] << std::dec << ']';
        }
        std::cout << ')';
    }

    bool ReportSetupFailure(const char* label, const char* reason)
    {
        std::cout << label << " 不合格 不一致画素数=未計測 最初の不一致=未計測 理由=" << reason << '\n';
        return false;
    }

    bool CheckPixels(const char* label, const uint16_t* input, const uint8_t* output, bool bShifted)
    {
        uint32_t mismatchCount = 0u;
        uint32_t firstX = 0u;
        uint32_t firstY = 0u;
        uint16_t firstExpected[4] = {};
        uint16_t firstActual[4] = {};
        for (uint32_t y = bShifted ? ExpectedShiftY : 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < (bShifted ? Width - ExpectedShiftX : Width); ++x)
            {
                const uint32_t expectedPixel = (bShifted ? y - ExpectedShiftY : y) * Width +
                                               (bShifted ? x + ExpectedShiftX : x);
                const uint32_t actualPixel = y * Width + x;
                uint16_t actual[4];
                std::memcpy(actual, output + static_cast<uint64_t>(actualPixel) * sizeof(actual), sizeof(actual));
                bool bMismatch = false;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    const uint16_t expected = input[expectedPixel * 4u + channel];
                    if (bShifted)
                    {
                        bMismatch = bMismatch || !(std::abs(HalfToFloat(actual[channel]) - HalfToFloat(expected)) <= ShiftTolerance);
                    }
                    else
                    {
                        bMismatch = bMismatch || actual[channel] != expected;
                    }
                }
                if (bMismatch)
                {
                    if (mismatchCount == 0u)
                    {
                        firstX = x;
                        firstY = y;
                        std::memcpy(firstExpected, input + expectedPixel * 4u, sizeof(firstExpected));
                        std::memcpy(firstActual, actual, sizeof(firstActual));
                    }
                    ++mismatchCount;
                }
            }
        }

        std::cout << label << (mismatchCount == 0u ? " 合格" : " 不合格")
                  << " 不一致画素数=" << mismatchCount;
        if (mismatchCount == 0u)
        {
            std::cout << " 最初の不一致=なし";
        }
        else
        {
            std::cout << " 最初の不一致=(" << firstX << ',' << firstY << ") 期待=";
            PrintPixel(firstExpected);
            std::cout << " 実際=";
            PrintPixel(firstActual);
        }
        std::cout << '\n';
        return mismatchCount == 0u;
    }

    bool RunCase(const DevicePtr& device, DistortionPass& pass, FrameResourceOwners& owners,
                 const uint16_t* colors, bool bShifted, uint64_t caseIndex, const char* label)
    {
        uint16_t offsets[PixelCount * 2u];
        for (uint32_t pixel = 0u; pixel < PixelCount; ++pixel)
        {
            offsets[pixel * 2u] =
                FloatToHalf(bShifted ? static_cast<float>(ExpectedShiftX) / static_cast<float>(Height) : 0.0f);
            offsets[pixel * 2u + 1u] =
                FloatToHalf(bShifted ? -static_cast<float>(ExpectedShiftY) / static_cast<float>(Height) : 0.0f);
        }

        TextureDesc colorDesc = TextureDesc::RenderTarget(Width, Height, Format::R16G16B16A16_FLOAT, "DistortionTestColor");
        colorDesc.Usage = colorDesc.Usage | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
        TextureDesc offsetDesc = TextureDesc::RenderTarget(Width, Height, Format::R16G16_FLOAT, "DistortionTestOffset");
        offsetDesc.Usage = offsetDesc.Usage | ResourceUsage::TransferDst;
        TexturePtr color = device->CreateTexture(colorDesc);
        TexturePtr offset = device->CreateTexture(offsetDesc);
        BufferPtr colorUpload = device->CreateBuffer(BufferDesc(ColorBytes, ResourceUsage::TransferSrc, true, "DistortionTestColorUpload"));
        BufferPtr offsetUpload = device->CreateBuffer(BufferDesc(OffsetBytes, ResourceUsage::TransferSrc, true, "DistortionTestOffsetUpload"));
        BufferPtr readback = device->CreateBuffer(BufferDesc(ColorBytes, ResourceUsage::TransferDst, true, "DistortionTestReadback"));
        CommandListPtr commands = device->CreateCommandList();
        if (!color || !offset || !colorUpload || !offsetUpload || !readback || !commands)
        {
            return ReportSetupFailure(label, "テクスチャまたはバッファを作れません");
        }
        colorUpload->Update(colors, ColorBytes);
        offsetUpload->Update(offsets, OffsetBytes);
        FrameResourceOwner* frame = owners.BeginFrame(0u, caseIndex);
        if (!frame)
        {
            return ReportSetupFailure(label, "フレームの資源を作れません");
        }

        commands->Begin();
        commands->TextureBarrier(color, ResourceState::Undefined, ResourceState::CopyDest);
        commands->TextureBarrier(offset, ResourceState::Undefined, ResourceState::CopyDest);
        commands->CopyBufferToTexture(colorUpload, color, Width, Height, 0u);
        commands->CopyBufferToTexture(offsetUpload, offset, Width, Height, 0u);
        commands->TextureBarrier(color, ResourceState::CopyDest, ResourceState::RenderTarget);
        commands->TextureBarrier(offset, ResourceState::CopyDest, ResourceState::ShaderResource);
        if (!pass.RecordComposite(color, offset, *commands, *frame))
        {
            commands->End();
            owners.AbortFrame();
            return ReportSetupFailure(label, "歪みの合成を記録できません");
        }

        // 最終レイアウトは読み取り用だが、直前の色添付への書き込みを転送まで待つ。
        ExplicitTextureBarrierDesc barrier;
        barrier.BeforeState = ResourceState::RenderTarget;
        barrier.AfterState = ResourceState::CopySource;
        barrier.OldLayout = TextureLayout::ShaderReadOnly;
        barrier.NewLayout = TextureLayout::TransferSource;
        commands->TextureBarrierExplicit(color, barrier);
        commands->CommitTextureLayout(color.get(), TextureLayout::TransferSource);
        commands->CopyTextureToBuffer(color, readback, Width, Height, 0u);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, ColorBytes);
        commands->End();
        commands->Submit(true);
        const bool bCommitted = owners.CommitSubmitted(caseIndex + 1u);
        device->WaitIdle();
        if (!bCommitted)
        {
            return ReportSetupFailure(label, "フレームの提出を記録できません");
        }
        const uint8_t* output = static_cast<const uint8_t*>(readback->Map(0u, ColorBytes));
        if (!output)
        {
            return ReportSetupFailure(label, "読み戻しを写像できません");
        }
        const bool bPassed = CheckPixels(label, colors, output, bShifted);
        readback->Unmap();
        return bPassed;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPU 試験のスキップが指定されました");
        }
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkan デバイスを利用できません");
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << TestName << " シェーダー管理を初期化できません\n";
            return 1;
        }
        DeviceFrameUploadPageSource pageSource;
        pageSource.SetDevice(device);
        FrameResourceOwners owners;
        owners.ConfigureUpload(&pageSource, FrameUploadArena::DefaultInitialPageBytes);
        if (!owners.ConfigureDescriptors(device.get()))
        {
            std::cerr << TestName << " フレームの記述子を準備できません\n";
            return 1;
        }
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        DistortionPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << TestName << " 歪みのパスを初期化できません\n";
            return 1;
        }

        uint16_t colors[PixelCount * 4u];
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const uint32_t pixel = (y * Width + x) * 4u;
                colors[pixel] = FloatToHalf(static_cast<float>(x) / static_cast<float>(Width));
                colors[pixel + 1u] = FloatToHalf(static_cast<float>(y) / static_cast<float>(Height));
                colors[pixel + 2u] = FloatToHalf(static_cast<float>(y * Width + x) / static_cast<float>(PixelCount));
                colors[pixel + 3u] = FloatToHalf(1.0f);
            }
        }
        bool bPassed = RunCase(device, pass, owners, colors, false, 0u, "ZERO_OFFSET");
        bPassed = RunCase(device, pass, owners, colors, true, 1u, "KNOWN_OFFSET") && bPassed;

        device->WaitIdle();
        pass.Shutdown();
        owners.Reset();
        owners.ReleaseUploadPages();
        owners.ReleaseDescriptorPools();
        shaderManager.Shutdown();
        return bPassed ? 0 : 1;
    }
}

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << " 例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}
