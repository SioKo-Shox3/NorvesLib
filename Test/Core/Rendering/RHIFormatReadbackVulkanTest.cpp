// Vulkan の形式対応と、非圧縮形式の更新・描画後の読み戻しを検証する。
#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/ShaderManager.h"
#include "RHI/FormatInfo.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanFormatTable.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

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

    constexpr const char* TestName = "RHIFormatReadbackVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t GradientWidth = 256u;
    constexpr uint32_t GradientHeight = 256u;
    constexpr uint32_t PackedWidth = 32u;
    constexpr uint32_t PackedHeight = 32u;
    constexpr uint32_t CellSize = 4u;
    constexpr uint32_t TargetWidth = 24u;
    constexpr uint32_t TargetHeight = 8u;
    constexpr uint32_t VerticesPerDraw = 18u;

    // 描画した値の参照。各成分で異なる順に使い、非正規数と大きな値も含める。
    constexpr float FloatValues[] = {
        0.0f, 1.0f, 0.5f, 65000.0f,
        0.000003814697265625f, 0.00006103515625f, 0.1f, 0.333251953125f,
        1.2345f, 1000.0f, 31.75f, 0.003f};
    constexpr uint32_t FloatValueCount = static_cast<uint32_t>(sizeof(FloatValues) / sizeof(FloatValues[0]));

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

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
        std::cout << TestName << " をスキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    void LogUnsupported(const char* formatName, const char* usage)
    {
        NORVES_LOG_INFO("RHIFormatReadbackVulkanTest", "%s は装置が%sに対応しないためスキップします", formatName, usage);
        std::cout << TestName << " " << formatName << " をスキップ: 装置が" << usage << "に対応しません" << std::endl;
    }

    bool VerifyFormatMappings()
    {
        for (uint32_t index = 1u; index < FormatCount; ++index)
        {
            const Format format = static_cast<Format>(index);
            const vk::Format mapped = Vulkan::ToVkFormatFromTable(format);
            if (GetFormatInfo(format).Value != format || mapped == vk::Format::eUndefined ||
                Vulkan::FromVkFormatFromTable(mapped) != format)
            {
                std::cerr << TestName << " 形式の対応または逆引きが不正です: " << index << std::endl;
                return false;
            }
            for (uint32_t previous = 1u; previous < index; ++previous)
            {
                if (Vulkan::ToVkFormatFromTable(static_cast<Format>(previous)) == mapped)
                {
                    std::cerr << TestName << " Vulkan 形式の対応が重複しています: " << previous << ", " << index << std::endl;
                    return false;
                }
            }
        }

        // 生のコピーだけではチャンネル配置の取り違えが分からないため、実際の形式も確かめる。
        if (Vulkan::ToVkFormatFromTable(Format::R16_UNORM) != vk::Format::eR16Unorm ||
            Vulkan::ToVkFormatFromTable(Format::R16G16_UNORM) != vk::Format::eR16G16Unorm ||
            Vulkan::ToVkFormatFromTable(Format::R10G10B10A2_UNORM) != vk::Format::eA2B10G10R10UnormPack32 ||
            Vulkan::ToVkFormatFromTable(Format::R11G11B10_FLOAT) != vk::Format::eB10G11R11UfloatPack32)
        {
            std::cerr << TestName << " 読み戻す形式の Vulkan 側の値またはチャンネル配置が不正です" << std::endl;
            return false;
        }

        std::cout << TestName << " 形式の対応と逆引きを確認: " << FormatCount - 1u << " 形式" << std::endl;
        return true;
    }

    BufferPtr MakeReadbackBuffer(const DevicePtr& device, uint64_t byteCount)
    {
        BufferDesc desc;
        desc.Size = byteCount;
        desc.Usage = ResourceUsage::TransferDst;
        desc.CPUAccessible = true;
        desc.DebugName = "FormatReadbackBuffer";
        return device->CreateBuffer(desc);
    }

    BufferPtr UpdateAndReadBack(const DevicePtr& device, Format format, uint32_t width, uint32_t height,
                                uint32_t bytesPerPixel, const void* data)
    {
        const uint32_t rowBytes = width * bytesPerPixel;
        const uint32_t byteCount = rowBytes * height;
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.TextureFormat = format;
        desc.Usage = ResourceUsage::ShaderResource | ResourceUsage::TransferSrc;
        desc.DebugName = "FormatReadbackUpload";
        TexturePtr texture = device->CreateTexture(desc);
        BufferPtr readback = MakeReadbackBuffer(device, byteCount);
        CommandListPtr commandList = device->CreateCommandList();
        if (!texture || !readback || !commandList)
        {
            std::cerr << TestName << " 更新と読み戻し用の資源を作れませんでした" << std::endl;
            return nullptr;
        }

        texture->Update(data, rowBytes, byteCount, 0, 0);
        commandList->Begin();
        commandList->TextureBarrier(texture, ResourceState::ShaderResource, ResourceState::CopySource);
        commandList->CopyTextureToBuffer(texture, readback, width, height, 0);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();
        return readback;
    }

    bool VerifyUnorm16(const DevicePtr& device, Format format, uint32_t channelCount, const char* formatName)
    {
        const uint32_t pixelCount = GradientWidth * GradientHeight;
        const uint32_t byteCount = pixelCount * channelCount * sizeof(uint16_t);
        VariableArray<uint16_t> data;
        data.resize(pixelCount * channelCount);
        for (uint32_t k = 0; k < pixelCount; ++k)
        {
            data[k * channelCount] = static_cast<uint16_t>(k);
            if (channelCount == 2u)
            {
                data[k * channelCount + 1u] = static_cast<uint16_t>(65535u - k);
            }
        }

        BufferPtr readback = UpdateAndReadBack(device, format, GradientWidth, GradientHeight,
                                               channelCount * sizeof(uint16_t), data.data());
        if (!readback)
        {
            return false;
        }
        const auto* mapped = static_cast<const uint8_t*>(readback->Map(0, byteCount));
        if (!mapped)
        {
            std::cerr << TestName << " " << formatName << " の読み戻しをマップできませんでした" << std::endl;
            return false;
        }

        bool bPassed = true;
        for (uint32_t k = 0; k < pixelCount && bPassed; ++k)
        {
            for (uint32_t channel = 0; channel < channelCount; ++channel)
            {
                uint16_t actual = 0;
                const uint32_t offset = (k * channelCount + channel) * sizeof(uint16_t);
                std::memcpy(&actual, mapped + offset, sizeof(actual));
                const uint32_t expected = channel == 0u ? k : 65535u - k;
                const float difference = std::fabs(static_cast<float>(actual) / 65535.0f -
                                                   static_cast<float>(expected) / 65535.0f);
                if (difference > 1.0e-5f)
                {
                    std::cerr << TestName << " " << formatName << " 画素=" << k << " 成分=" << channel
                              << " 期待=" << expected << " 実際=" << actual << " 誤差=" << difference << std::endl;
                    bPassed = false;
                    break;
                }
            }
        }
        readback->Unmap();
        if (bPassed)
        {
            std::cout << TestName << " " << formatName << " の 65536 段階を確認しました" << std::endl;
        }
        return bPassed;
    }

    uint32_t PackUnorm10(uint32_t k)
    {
        const uint32_t r = k;
        const uint32_t g = 1023u - k;
        const uint32_t b = (k * 37u + 137u) & 1023u;
        const uint32_t a = k & 3u;
        return r | (g << 10u) | (b << 20u) | (a << 30u);
    }

    bool VerifyUnorm10(const DevicePtr& device)
    {
        constexpr uint32_t PixelCount = PackedWidth * PackedHeight;
        constexpr uint32_t ByteCount = PixelCount * sizeof(uint32_t);
        uint32_t data[PixelCount];
        for (uint32_t k = 0; k < PixelCount; ++k)
        {
            data[k] = PackUnorm10(k);
        }

        BufferPtr readback = UpdateAndReadBack(device, Format::R10G10B10A2_UNORM, PackedWidth, PackedHeight,
                                               sizeof(uint32_t), data);
        if (!readback)
        {
            return false;
        }
        const auto* mapped = static_cast<const uint8_t*>(readback->Map(0, ByteCount));
        if (!mapped)
        {
            std::cerr << TestName << " R10G10B10A2_UNORM の読み戻しをマップできませんでした" << std::endl;
            return false;
        }

        bool bPassed = true;
        for (uint32_t k = 0; k < PixelCount; ++k)
        {
            uint32_t actual = 0;
            std::memcpy(&actual, mapped + k * sizeof(uint32_t), sizeof(actual));
            if (actual != PackUnorm10(k))
            {
                std::cerr << TestName << " R10G10B10A2_UNORM 画素=" << k << " 期待=0x" << std::hex
                          << PackUnorm10(k) << " 実際=0x" << actual << std::dec << std::endl;
                bPassed = false;
                break;
            }
        }
        readback->Unmap();
        if (bPassed)
        {
            std::cout << TestName << " R10G10B10A2_UNORM の 1024 段階と A の 4 段階を確認しました" << std::endl;
        }
        return bPassed;
    }

    // 5 ビット指数・符号なしの 11/10 ビット浮動小数を復号する。
    bool DecodeUnsignedFloat(uint32_t bits, uint32_t mantissaBits, float& value)
    {
        const uint32_t mantissa = bits & ((1u << mantissaBits) - 1u);
        const uint32_t exponent = bits >> mantissaBits;
        if (exponent == 31u)
        {
            return false;
        }
        if (exponent == 0u)
        {
            value = std::ldexp(static_cast<float>(mantissa), -14 - static_cast<int>(mantissaBits));
        }
        else
        {
            value = std::ldexp(1.0f + static_cast<float>(mantissa) / static_cast<float>(1u << mantissaBits),
                               static_cast<int>(exponent) - 15);
        }
        return true;
    }

    bool VerifyFloatPixels(const uint8_t* pixels)
    {
        uint32_t checked = 0;
        // 既存の頂点シェーダーが三角形で確実に覆う、各セルの先頭行を調べる。
        for (uint32_t y = 0; y < TargetHeight; y += CellSize)
        {
            for (uint32_t x = 0; x < TargetWidth; ++x)
            {
                uint32_t packed = 0;
                std::memcpy(&packed, pixels + (y * TargetWidth + x) * sizeof(uint32_t), sizeof(packed));
                const uint32_t components[] = {packed & 0x7FFu, (packed >> 11u) & 0x7FFu, (packed >> 22u) & 0x3FFu};
                const uint32_t index = (x + 3u * y) % FloatValueCount;
                for (uint32_t channel = 0; channel < 3u; ++channel)
                {
                    const uint32_t mantissaBits = channel < 2u ? 6u : 5u;
                    const float expected = FloatValues[(index + channel * 4u) % FloatValueCount];
                    float actual = 0.0f;
                    const bool bFinite = DecodeUnsignedFloat(components[channel], mantissaBits, actual);
                    // 非正規数では半 ULP の絶対誤差を加え、それ以外は仮数の相対誤差を許す。
                    const float tolerance = expected / static_cast<float>(1u << mantissaBits) +
                                            std::ldexp(1.0f, -15 - static_cast<int>(mantissaBits));
                    if (!bFinite || std::fabs(actual - expected) > tolerance)
                    {
                        std::cerr << TestName << " R11G11B10_FLOAT x=" << x << " y=" << y << " 成分=" << channel
                                  << " 期待=" << expected << " 実際=" << actual << " 許容=" << tolerance
                                  << " 符号化値=0x" << std::hex << components[channel] << std::dec << std::endl;
                        return false;
                    }
                }
                ++checked;
            }
        }
        std::cout << TestName << " R11G11B10_FLOAT の描画先と " << checked << " 画素を確認しました" << std::endl;
        return true;
    }

    bool RenderAndVerifyFloat(const DevicePtr& device)
    {
        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Test/Core/Rendering/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " シェーダーマネージャーを初期化できませんでした" << std::endl;
            return false;
        }

        ShaderPtr vertexShader = shaderManager.LoadShader("integer_attachment_probe.vert", ShaderStage::Vertex);
        ShaderPtr pixelShader = shaderManager.LoadShader("format_readback_gradient.frag", ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            std::cerr << TestName << " 描画用シェーダーを読み込めませんでした" << std::endl;
            return false;
        }

        TextureDesc textureDesc;
        textureDesc.Width = TargetWidth;
        textureDesc.Height = TargetHeight;
        textureDesc.TextureFormat = Format::R11G11B10_FLOAT;
        textureDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
        textureDesc.DebugName = "FormatReadbackFloatTarget";
        TexturePtr target = device->CreateTexture(textureDesc);

        AttachmentDesc attachment;
        attachment.format = Format::R11G11B10_FLOAT;
        attachment.clearColor[0] = 0.25f;
        attachment.clearColor[1] = 0.75f;
        attachment.clearColor[2] = 0.25f;
        attachment.loadOp = AttachmentLoadOp::Clear;
        attachment.storeOp = AttachmentStoreOp::Store;
        attachment.initialState = ResourceState::Undefined;
        attachment.finalState = ResourceState::RenderTarget;
        RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(attachment);
        RenderPassPtr renderPass = device->CreateRenderPass(renderPassDesc);

        FramebufferPtr framebuffer;
        if (target && renderPass)
        {
            FramebufferDesc desc;
            desc.renderPass = renderPass;
            desc.colorTargets.push_back(target);
            desc.width = TargetWidth;
            desc.height = TargetHeight;
            framebuffer = device->CreateFramebuffer(desc);
        }

        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = pixelShader;
        pipelineDesc.primitiveTopology = PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = CullMode::None;
        pipelineDesc.rasterState.frontFace = FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        BlendAttachmentDesc blend;
        blend.blendEnable = false;
        blend.colorWriteMask = ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blend);
        pipelineDesc.renderPass = renderPass;
        PipelinePtr pipeline = framebuffer ? device->CreateGraphicsPipeline(pipelineDesc) : nullptr;
        constexpr uint32_t ByteCount = TargetWidth * TargetHeight * sizeof(uint32_t);
        BufferPtr readback = MakeReadbackBuffer(device, ByteCount);
        CommandListPtr commandList = pipeline && readback ? device->CreateCommandList() : nullptr;
        if (!commandList)
        {
            std::cerr << TestName << " 浮動小数の描画・読み戻し用の資源を作れませんでした" << std::endl;
            return false;
        }

        Viewport viewport;
        viewport.width = static_cast<float>(TargetWidth);
        viewport.height = static_cast<float>(TargetHeight);
        ScissorRect scissor;
        scissor.right = static_cast<int32_t>(TargetWidth);
        scissor.bottom = static_cast<int32_t>(TargetHeight);
        commandList->Begin();
        commandList->BeginRenderPass(renderPass, framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(pipeline);
        for (uint32_t row = 0; row < TargetHeight / CellSize; ++row)
        {
            commandList->DrawInstanced(VerticesPerDraw, 1u, 0u, row << 16u);
        }
        commandList->EndRenderPass();
        commandList->TextureBarrier(target, ResourceState::RenderTarget, ResourceState::CopySource);
        commandList->CopyTextureToBuffer(target, readback, TargetWidth, TargetHeight, 0);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        const auto* mapped = static_cast<const uint8_t*>(readback->Map(0, ByteCount));
        if (!mapped)
        {
            std::cerr << TestName << " 浮動小数の読み戻しをマップできませんでした" << std::endl;
            return false;
        }
        const bool bPassed = VerifyFloatPixels(mapped);
        readback->Unmap();
        shaderManager.Shutdown();
        return bPassed;
    }

    int RunTest()
    {
        if (!VerifyFormatMappings())
        {
            return 1;
        }
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定されました");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkan デバイスまたは検証レイヤーを利用できません");
        }

        bool bPassed = true;
        uint32_t executedCases = 0;
        const struct { Format Value; uint32_t Channels; const char* Name; } unorm16Cases[] = {
            {Format::R16_UNORM, 1u, "R16_UNORM"},
            {Format::R16G16_UNORM, 2u, "R16G16_UNORM"}};
        for (const auto& testCase : unorm16Cases)
        {
            if (!device->IsFormatSupported(testCase.Value, FormatUsage::Sampled))
            {
                LogUnsupported(testCase.Name, "サンプリング");
                continue;
            }
            ++executedCases;
            const bool bCasePassed = VerifyUnorm16(device, testCase.Value, testCase.Channels, testCase.Name);
            bPassed = bCasePassed && bPassed;
        }

        if (device->IsFormatSupported(Format::R10G10B10A2_UNORM, FormatUsage::Sampled))
        {
            ++executedCases;
            const bool bCasePassed = VerifyUnorm10(device);
            bPassed = bCasePassed && bPassed;
        }
        else
        {
            std::cerr << TestName << " R10G10B10A2_UNORM のサンプリングに対応せず、必須の読み戻しを検証できません" << std::endl;
            bPassed = false;
        }

        if (device->IsFormatSupported(Format::R11G11B10_FLOAT, FormatUsage::RenderTarget))
        {
            ++executedCases;
            std::cout << TestName << " R11G11B10_FLOAT は描画先に対応しています" << std::endl;
            const bool bCasePassed = RenderAndVerifyFloat(device);
            bPassed = bCasePassed && bPassed;
        }
        else
        {
            LogUnsupported("R11G11B10_FLOAT", "描画先");
        }

        device->WaitIdle();
        device = nullptr;
        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << TestName << " Vulkan 検証エラー数=" << validationErrorCount << std::endl;
        if (validationErrorCount != 0u)
        {
            bPassed = false;
        }
        if (!bPassed)
        {
            std::cerr << TestName << " は失敗しました" << std::endl;
            return 1;
        }
        if (executedCases == 0u)
        {
            return SkipGpuTest("対象の形式を装置が利用できません");
        }
        std::cout << TestName << " は成功しました" << std::endl;
        return 0;
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
        std::cerr << TestName << " で例外が出ました: " << exception.what() << std::endl;
        return 1;
    }
}
