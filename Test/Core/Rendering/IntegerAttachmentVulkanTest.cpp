// 整数のカラー添付（R32_UINT・R32G32_UINT）と geometryShader の機能（フラグメントシェーダーの gl_PrimitiveID）の GPU テスト。
// ビジビリティバッファの ID（描画の番号 << 7 | 描画の中の三角形の番号）を整数の添付へ書き、読み戻して三角形ごとに期待の値になることを確かめる。
//   - 描画先は 24x8 画素（4x4 画素のセルが 6 列 x 2 行）。1 回の描画が 6 個の三角形を出し、三角形 t が行 r の列 t のセルを覆う。
//     行 0 は描画の番号 0x0123、行 1 は描画の番号 0x1FFF（開始インスタンスで渡す）。gl_PrimitiveID は描画ごとに 0 から数え直す。
//   - セルの画素 (2, 0) は三角形が覆うので ID（描画の番号 << 7 | t）、画素 (0, 3) は覆わないので整数のクリア値のままであること。
//     クリア値は 0xDEADBEEF（G は 0xCAFEF00D）で、浮動小数の値として渡すと壊れる整数の値を使う。
//   - R32G32_UINT は、R に同じ ID、G に描画の番号だけを書く。
//   - Vulkan の validation error が 0 件であること。
// Vulkan デバイスが無い、または geometryShader が使えない環境では 125（スキップ）を返す。
#include "Rendering/ShaderManager.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

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
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "IntegerAttachmentVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;

    // セル（4x4 画素）が 6 列 x 2 行
    constexpr uint32_t CellSize = 4u;
    constexpr uint32_t ColumnCount = 6u;
    constexpr uint32_t RowCount = 2u;
    constexpr uint32_t TargetWidth = ColumnCount * CellSize;
    constexpr uint32_t TargetHeight = RowCount * CellSize;
    // 1 回の描画が出す三角形の頂点数
    constexpr uint32_t VerticesPerDraw = ColumnCount * 3u;

    // 行ごとの描画の番号（頂点シェーダーへは開始インスタンスの (行 << 16) | 描画の番号で渡す）
    constexpr uint32_t DrawNumbers[RowCount] = {0x0123u, 0x1FFFu};

    // 整数のクリア値。float を経由すると別の値になる
    constexpr uint32_t ClearR = 0xDEADBEEFu;
    constexpr uint32_t ClearG = 0xCAFEF00Du;

    int g_failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << std::endl;
            ++g_failures;
        }
    }

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
        std::cout << TestName << " スキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // 指定の形式の整数の添付へ 2 回の描画（行ごとに 1 回）を行い、読み戻した全画素を out へ受け取る（1 画素あたり componentCount 個の uint32）。
    bool RenderAndReadBack(const DevicePtr& device, ShaderManager& shaderManager, Format format, uint32_t componentCount,
                           const char* fragmentShaderName, uint32_t* out)
    {
        ShaderPtr vertexShader = shaderManager.LoadShader("integer_attachment_probe.vert", ShaderStage::Vertex);
        ShaderPtr pixelShader = shaderManager.LoadShader(fragmentShaderName, ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            std::cerr << TestName << " シェーダーをコンパイルできませんでした: " << fragmentShaderName << std::endl;
            return false;
        }

        TextureDesc textureDesc;
        textureDesc.Width = TargetWidth;
        textureDesc.Height = TargetHeight;
        textureDesc.TextureFormat = format;
        textureDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
        textureDesc.DebugName = "IntegerAttachmentTarget";
        TexturePtr target = device->CreateTexture(textureDesc);

        AttachmentDesc colorAttachment;
        colorAttachment.format = format;
        colorAttachment.clear = true;
        colorAttachment.clearColorUint[0] = ClearR;
        colorAttachment.clearColorUint[1] = ClearG;
        colorAttachment.loadOp = AttachmentLoadOp::Clear;
        colorAttachment.storeOp = AttachmentStoreOp::Store;
        colorAttachment.initialState = ResourceState::Undefined;
        colorAttachment.finalState = ResourceState::RenderTarget;
        RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        RenderPassPtr renderPass = device->CreateRenderPass(renderPassDesc);

        FramebufferPtr framebuffer;
        if (target && renderPass)
        {
            FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = renderPass;
            framebufferDesc.colorTargets.push_back(target);
            framebufferDesc.width = TargetWidth;
            framebufferDesc.height = TargetHeight;
            framebuffer = device->CreateFramebuffer(framebufferDesc);
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
        // 整数の添付はブレンドできない
        BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = renderPass;
        PipelinePtr pipeline = framebuffer ? device->CreateGraphicsPipeline(pipelineDesc) : nullptr;

        const uint64_t readbackBytes = static_cast<uint64_t>(TargetWidth) * TargetHeight * componentCount * sizeof(uint32_t);
        BufferDesc bufferDesc;
        bufferDesc.Size = readbackBytes;
        bufferDesc.Usage = ResourceUsage::TransferDst;
        bufferDesc.CPUAccessible = true;
        bufferDesc.DebugName = "IntegerAttachmentReadback";
        BufferPtr readback = device->CreateBuffer(bufferDesc);

        CommandListPtr commandList = pipeline && readback ? device->CreateCommandList() : nullptr;
        if (!commandList)
        {
            std::cerr << TestName << " 描画の確認用の資源を作れませんでした" << std::endl;
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
        for (uint32_t row = 0; row < RowCount; ++row)
        {
            commandList->DrawInstanced(VerticesPerDraw, 1u, 0u, (row << 16) | DrawNumbers[row]);
        }
        commandList->EndRenderPass();
        commandList->TextureBarrier(target, ResourceState::RenderTarget, ResourceState::CopySource);
        commandList->CopyTextureToBuffer(target, readback, TargetWidth, TargetHeight, 0);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        const void* mapped = readback->Map(0, readbackBytes);
        if (mapped == nullptr)
        {
            std::cerr << TestName << " 読み戻しのバッファを map できませんでした" << std::endl;
            return false;
        }
        std::memcpy(out, mapped, static_cast<size_t>(readbackBytes));
        readback->Unmap();
        return true;
    }

    // セル (列, 行) の、三角形が覆う画素 (2, 0) と、覆わない画素 (0, 3) の値を検証する。
    void VerifyPixels(const char* formatName, const uint32_t* pixels, uint32_t componentCount)
    {
        uint32_t checked = 0;
        for (uint32_t row = 0; row < RowCount; ++row)
        {
            for (uint32_t column = 0; column < ColumnCount; ++column)
            {
                const uint32_t insideIndex = ((row * CellSize + 0u) * TargetWidth + column * CellSize + 2u) * componentCount;
                const uint32_t outsideIndex = ((row * CellSize + 3u) * TargetWidth + column * CellSize + 0u) * componentCount;

                const uint32_t expectedId = (DrawNumbers[row] << 7) | column;
                const uint32_t actualId = pixels[insideIndex];
                if (actualId != expectedId)
                {
                    std::cerr << TestName << " " << formatName << " 三角形の画素が違う 行=" << row << " 列=" << column << " 期待=0x"
                              << std::hex << expectedId << " 実際=0x" << actualId << std::dec << std::endl;
                }
                Expect(actualId == expectedId, "三角形が覆う画素は (描画の番号 << 7 | 描画の中の三角形の番号) でなければならない");

                if (componentCount >= 2u)
                {
                    const uint32_t actualDraw = pixels[insideIndex + 1u];
                    Expect(actualDraw == DrawNumbers[row], "R32G32 の G は描画の番号でなければならない");
                }

                const uint32_t actualClearR = pixels[outsideIndex];
                if (actualClearR != ClearR)
                {
                    std::cerr << TestName << " " << formatName << " 覆われない画素がクリア値でない 行=" << row << " 列=" << column
                              << " 実際=0x" << std::hex << actualClearR << std::dec << std::endl;
                }
                Expect(actualClearR == ClearR, "覆われない画素は整数のクリア値のままでなければならない");
                if (componentCount >= 2u)
                {
                    Expect(pixels[outsideIndex + 1u] == ClearG, "R32G32 の覆われない画素の G も整数のクリア値でなければならない");
                }
                ++checked;
            }
        }
        std::cout << TestName << " " << formatName << " 確認した三角形=" << checked << std::endl;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
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

        const auto& capabilities = device->GetCapabilities();
        std::cout << TestName << " geometryShader=" << capabilities.bGeometryShader
                  << " shaderStorageImageExtendedFormats=" << capabilities.bShaderStorageImageExtendedFormats << std::endl;
        if (!capabilities.bGeometryShader)
        {
            return SkipGpuTest("geometryShader が使えないデバイス（フラグメントシェーダーの gl_PrimitiveID を使えない。従来の GBuffer の経路で描く）");
        }
        // 開発機（RTX 4080）では RG16F などの storage image の形式も使える。
        Expect(capabilities.bShaderStorageImageExtendedFormats,
               "shaderStorageImageExtendedFormats は開発機で有効でなければならない");

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Test/Core/Rendering/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManagerを初期化できませんでした" << std::endl;
            return 1;
        }

        uint32_t pixels[TargetWidth * TargetHeight * 2u] = {};
        if (RenderAndReadBack(device, shaderManager, Format::R32_UINT, 1u, "integer_attachment_probe.frag", pixels))
        {
            VerifyPixels("R32_UINT", pixels, 1u);
        }
        else
        {
            ++g_failures;
        }

        std::memset(pixels, 0, sizeof(pixels));
        if (RenderAndReadBack(device, shaderManager, Format::R32G32_UINT, 2u, "integer_attachment_probe_rg.frag", pixels))
        {
            VerifyPixels("R32G32_UINT", pixels, 2u);
        }
        else
        {
            ++g_failures;
        }

        device->WaitIdle();
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        bool bPassed = g_failures == 0;
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
