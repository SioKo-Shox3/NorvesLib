// 間接描画の戻り値と描画結果を Vulkan の GPU で確かめる。
// A: 計算シェーダーが書いた引数をバリアの後に読み、3 セルを描く。
// B: 件数バッファが 0 と 2 のとき、対応するセルだけを描く（非対応時はスキップ）。
// C: 複数描画の能力がある場合の 2 件と、能力を無効にした場合の 2 件の拒否・1 件の描画。
// D: 件数付き描画の能力を無効にすると拒否され、何も描かれない。
// E: バッファ・用途・オフセット・範囲・stride が不正な呼び出しを拒否し、何も描かれない。
// 全ケースで検証エラーの捕捉件数が 0。Vulkan を利用できない場合は 125 を返す。
#include "Container/Containers.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/IndirectCommands.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanDevice.h"

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
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "IndirectDrawVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t CellSize = 8u;
    constexpr uint32_t CellCount = 8u;
    constexpr uint32_t Width = CellSize * CellCount;
    constexpr uint32_t Height = CellSize;
    constexpr uint32_t TableCount = 3u;
    constexpr uint32_t Stride = sizeof(DrawIndirectCommand);
    constexpr uint64_t ReadbackBytes = Width * Height * 4u;
    int g_failures = 0;

    constexpr const char* VertexSource = R"glsl(
#version 450
void main()
{
    const vec2 corners[3] = vec2[3](vec2(1.0, 1.0), vec2(7.0, 1.0), vec2(1.0, 6.75));
    int triangle = gl_VertexIndex / 3;
    vec2 pixel = corners[gl_VertexIndex % 3] + vec2(float(triangle * 8), 0.0);
    gl_Position = vec4(pixel / vec2(64.0, 8.0) * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

    constexpr const char* FragmentSource = R"glsl(
#version 450
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)glsl";

    constexpr const char* ComputeSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0, std430) buffer Arguments { uint values[]; } args;
void main()
{
    args.values[0] = 9u;
    args.values[1] = 1u;
    args.values[2] = 0u;
    args.values[3] = 0u;
}
)glsl";

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cerr << TestName << " 失敗: " << message << '\n';
            ++g_failures;
        }
    }

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || value == nullptr)
        {
            return false;
        }
        const bool bSkip = std::strcmp(value, "1") == 0;
        free(value);
        return bSkip;
    }

    class ValidationCapture
    {
    public:
        ValidationCapture() { Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~ValidationCapture() { Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t Hits() const { return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // 元の能力を保持し、例外時を含めてスコープ終了時に戻す。
    class CapabilityOverride
    {
    public:
        explicit CapabilityOverride(Vulkan::VulkanDevice& device)
            : m_Device(device), m_Original(device.GetCapabilities()) {}
        ~CapabilityOverride() { m_Device.SetCapabilitiesForTesting(m_Original); }
        void DisableMultiDraw()
        {
            RHI::DeviceCapabilities capabilities = m_Device.GetCapabilities();
            capabilities.bMultiDrawIndirect = false;
            m_Device.SetCapabilitiesForTesting(capabilities);
        }
        void DisableDrawCount()
        {
            RHI::DeviceCapabilities capabilities = m_Device.GetCapabilities();
            capabilities.bDrawIndirectCount = false;
            m_Device.SetCapabilitiesForTesting(capabilities);
        }
    private:
        Vulkan::VulkanDevice& m_Device;
        RHI::DeviceCapabilities m_Original;
    };

    ShaderPtr CompileShader(const DevicePtr& device, const ShaderCompilerPtr& compiler,
                            ShaderStage stage, const char* source, const char* name)
    {
        const ShaderCompileResult result = compiler->CompileFromSource(String(source), stage, String(name));
        if (!result.bSuccess)
        {
            std::cerr << TestName << " シェーダーのコンパイルに失敗: " << name << " " << result.ErrorMessage << '\n';
            return {};
        }
        ShaderDesc desc;
        desc.stage = stage;
        desc.byteCode = result.ByteCode;
        return device->CreateShader(desc);
    }

    DescriptorSetDesc MakeWriterLayout()
    {
        DescriptorSetDesc desc;
        DescriptorBinding binding;
        binding.binding = 0u;
        binding.type = ResourceBindType::RWBuffer;
        binding.stages = ShaderStage::Compute;
        desc.bindings.push_back(binding);
        return desc;
    }

    struct Fixture
    {
        DevicePtr Device;
        TSharedPtr<Vulkan::VulkanDevice> VulkanDevice;
        RenderPassPtr RenderPass;
        PipelinePtr GraphicsPipeline;
        PipelinePtr ComputePipeline;
        DescriptorSetDesc WriterLayout;
    };

    bool CreateFixture(Fixture& fixture)
    {
        ShaderCompilerPtr compiler = fixture.Device->CreateShaderCompiler();
        if (!compiler)
        {
            return false;
        }
        ShaderPtr vertex = CompileShader(fixture.Device, compiler, ShaderStage::Vertex, VertexSource, "indirect_draw.vert");
        ShaderPtr fragment = CompileShader(fixture.Device, compiler, ShaderStage::Pixel, FragmentSource, "indirect_draw.frag");
        ShaderPtr compute = CompileShader(fixture.Device, compiler, ShaderStage::Compute, ComputeSource, "indirect_draw.comp");
        if (!vertex || !fragment || !compute)
        {
            return false;
        }

        AttachmentDesc attachment;
        attachment.format = Format::R8G8B8A8_UNORM;
        attachment.clear = true;
        attachment.clearColor[0] = 0.0f;
        attachment.clearColor[1] = 0.0f;
        attachment.clearColor[2] = 0.0f;
        attachment.clearColor[3] = 0.0f;
        attachment.loadOp = AttachmentLoadOp::Clear;
        attachment.storeOp = AttachmentStoreOp::Store;
        attachment.initialState = ResourceState::Undefined;
        attachment.finalState = ResourceState::RenderTarget;
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back(attachment);
        passDesc.hasDepthStencil = false;
        fixture.RenderPass = fixture.Device->CreateRenderPass(passDesc);
        if (!fixture.RenderPass)
        {
            return false;
        }

        GraphicsPipelineDesc graphics;
        graphics.vertexShader = vertex;
        graphics.pixelShader = fragment;
        graphics.primitiveTopology = PrimitiveTopology::TriangleList;
        graphics.rasterState.polygonMode = PolygonMode::Fill;
        graphics.rasterState.cullMode = CullMode::None;
        graphics.rasterState.frontFace = FrontFace::CounterClockwise;
        graphics.rasterState.lineWidth = 1.0f;
        graphics.depthStencilState.depthTestEnable = false;
        graphics.depthStencilState.depthWriteEnable = false;
        BlendAttachmentDesc blend;
        blend.blendEnable = false;
        blend.colorWriteMask = ColorWriteMask::All;
        graphics.blendState.attachments.push_back(blend);
        graphics.renderPass = fixture.RenderPass;
        fixture.GraphicsPipeline = fixture.Device->CreateGraphicsPipeline(graphics);

        fixture.WriterLayout = MakeWriterLayout();
        ComputePipelineDesc computeDesc;
        computeDesc.computeShader = compute;
        computeDesc.descriptorSetLayouts.push_back(fixture.WriterLayout);
        fixture.ComputePipeline = fixture.Device->CreateComputePipeline(computeDesc);
        return fixture.GraphicsPipeline && fixture.ComputePipeline;
    }

    bool WriteBuffer(const BufferPtr& buffer, const void* data, uint64_t size)
    {
        void* mapped = buffer->Map(0u, size);
        if (!mapped)
        {
            return false;
        }
        std::memcpy(mapped, data, static_cast<size_t>(size));
        buffer->Unmap();
        return true;
    }

    // 4 倍の整数座標で辺の外積を求める。どの辺も画素中心を通らない。
    bool IsCovered(uint32_t x, uint32_t y)
    {
        const int32_t px = static_cast<int32_t>(4u * x + 2u);
        const int32_t py = static_cast<int32_t>(4u * y + 2u);
        const int32_t ab = 24 * (py - 4);
        const int32_t bc = -24 * (py - 4) - 23 * (px - 28);
        const int32_t ca = 23 * (px - 4);
        return ab > 0 && bc > 0 && ca > 0;
    }

    uint32_t CountExpectedPixels()
    {
        uint32_t count = 0u;
        for (uint32_t y = 0u; y < CellSize; ++y)
        {
            for (uint32_t x = 0u; x < CellSize; ++x)
            {
                count += IsCovered(x, y) ? 1u : 0u;
            }
        }
        return count;
    }

    enum class CaseMode
    {
        Compute,
        CountZero,
        CountTwo,
        MultiTwo,
        MultiDisabledTwo,
        MultiDisabledOne,
        CountDisabled,
        Invalid,
    };

    void CheckInvalidArguments(const CommandListPtr& list, const BufferPtr& args,
                               const BufferPtr& count, const BufferPtr& noIndirect, bool bCountSupported)
    {
        Expect(!list->DrawIndirect(BufferPtr{}, 0u, 1u, Stride), "描画引数が null のときは false");
        Expect(!list->DrawIndirect(noIndirect, 0u, 1u, Stride), "描画引数の用途が不正なときは false");
        Expect(!list->DrawIndirect(args, 2u, 1u, Stride), "描画引数のオフセットが不整列なら false");
        Expect(!list->DrawIndirect(args, args->GetSize() - 4u, 1u, Stride), "描画引数が範囲外なら false");
        Expect(!list->DrawIndirect(args, 0u, 2u, 12u), "複数描画の stride が 16 未満なら false");
        if (!bCountSupported)
        {
            std::cout << TestName << " ケース E: 件数付き描画の不正引数は能力がないためスキップ\n";
            return;
        }
        Expect(!list->DrawIndirectCount(BufferPtr{}, 0u, count, 0u, TableCount, Stride), "件数付き描画の引数が null なら false");
        Expect(!list->DrawIndirectCount(args, 0u, BufferPtr{}, 0u, TableCount, Stride), "件数バッファが null なら false");
        Expect(!list->DrawIndirectCount(noIndirect, 0u, count, 0u, TableCount, Stride), "件数付き描画の引数の用途が不正なら false");
        Expect(!list->DrawIndirectCount(args, 0u, noIndirect, 0u, TableCount, Stride), "件数バッファの用途が不正なら false");
        Expect(!list->DrawIndirectCount(args, 2u, count, 0u, TableCount, Stride), "件数付き描画の引数のオフセットが不整列なら false");
        Expect(!list->DrawIndirectCount(args, 0u, count, 2u, TableCount, Stride), "件数のオフセットが不整列なら false");
        Expect(!list->DrawIndirectCount(args, args->GetSize() - 4u, count, 0u, TableCount, Stride), "件数付き描画の引数が範囲外なら false");
        Expect(!list->DrawIndirectCount(args, 0u, count, count->GetSize(), TableCount, Stride), "件数が範囲外なら false");
        Expect(!list->DrawIndirectCount(args, 0u, count, 0u, TableCount, 12u), "件数付き描画の stride が 16 未満なら false");
    }

    bool RunCase(Fixture& fixture, const char* name, CaseMode mode, uint32_t expectedCells, uint32_t expectedPixels)
    {
        TextureDesc textureDesc;
        textureDesc.Width = Width;
        textureDesc.Height = Height;
        textureDesc.TextureFormat = Format::R8G8B8A8_UNORM;
        textureDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
        textureDesc.DebugName = "IndirectDrawTarget";
        TexturePtr target = fixture.Device->CreateTexture(textureDesc);
        BufferPtr readback = fixture.Device->CreateBuffer(BufferDesc(ReadbackBytes, ResourceUsage::TransferDst, true, "IndirectDrawReadback"));
        BufferPtr args = fixture.Device->CreateBuffer(BufferDesc(TableCount * Stride,
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::IndirectBuffer, true, "IndirectDrawArguments"));
        BufferPtr count = fixture.Device->CreateBuffer(BufferDesc(sizeof(uint32_t), ResourceUsage::IndirectBuffer, true, "IndirectDrawCount"));
        BufferPtr noIndirect = fixture.Device->CreateBuffer(BufferDesc(TableCount * Stride,
            ResourceUsage::StorageBuffer, true, "IndirectDrawInvalidUsage"));
        DescriptorSetPtr writer = fixture.Device->CreateDescriptorSet(fixture.WriterLayout);
        CommandListPtr commands = fixture.Device->CreateCommandList();
        if (!target || !readback || !args || !count || !noIndirect || !writer || !commands)
        {
            std::cerr << TestName << " ケース " << name << " の資源を作れませんでした\n";
            return false;
        }
        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = fixture.RenderPass;
        framebufferDesc.colorTargets.push_back(target);
        framebufferDesc.width = Width;
        framebufferDesc.height = Height;
        FramebufferPtr framebuffer = fixture.Device->CreateFramebuffer(framebufferDesc);
        if (!framebuffer)
        {
            std::cerr << TestName << " ケース " << name << " のフレームバッファを作れませんでした\n";
            return false;
        }

        DrawIndirectCommand table[TableCount] = {};
        if (mode != CaseMode::Compute)
        {
            for (uint32_t i = 0u; i < TableCount; ++i)
            {
                table[i] = {3u, 1u, 3u * i, 0u};
            }
        }
        const uint32_t countValue = mode == CaseMode::CountZero ? 0u : 2u;
        if (!WriteBuffer(args, table, sizeof(table)) || !WriteBuffer(count, &countValue, sizeof(countValue)) ||
            !WriteBuffer(noIndirect, table, sizeof(table)))
        {
            std::cerr << TestName << " ケース " << name << " の引数をマップできませんでした\n";
            return false;
        }
        writer->BindStorageBuffer(0u, args, 0u, static_cast<uint32_t>(args->GetSize()));
        writer->Update();

        Viewport viewport;
        viewport.width = static_cast<float>(Width);
        viewport.height = static_cast<float>(Height);
        ScissorRect scissor;
        scissor.right = static_cast<int32_t>(Width);
        scissor.bottom = static_cast<int32_t>(Height);
        commands->Begin();
        if (mode == CaseMode::Compute)
        {
            commands->BufferBarrier(args, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, args->GetSize());
            commands->SetPipeline(fixture.ComputePipeline);
            commands->SetDescriptorSet(writer, 0u);
            commands->Dispatch(1u, 1u, 1u);
            commands->BufferBarrier(args, ResourceState::UnorderedAccess, ResourceState::IndirectArgument, 0u, args->GetSize());
        }
        commands->BeginRenderPass(fixture.RenderPass, framebuffer);
        commands->SetViewport(viewport);
        commands->SetScissor(scissor);
        commands->SetPipeline(fixture.GraphicsPipeline);
        bool bRecorded = false;
        switch (mode)
        {
        case CaseMode::Compute:
        case CaseMode::MultiDisabledOne:
            bRecorded = commands->DrawIndirect(args, 0u, 1u, Stride);
            Expect(bRecorded, "1 件の間接描画は true");
            break;
        case CaseMode::MultiTwo:
            bRecorded = commands->DrawIndirect(args, 0u, 2u, Stride);
            Expect(bRecorded, "能力のある複数描画は true");
            break;
        case CaseMode::MultiDisabledTwo:
            bRecorded = commands->DrawIndirect(args, 0u, 2u, Stride);
            Expect(!bRecorded, "能力のない複数描画は false");
            break;
        case CaseMode::CountZero:
        case CaseMode::CountTwo:
            bRecorded = commands->DrawIndirectCount(args, 0u, count, 0u, TableCount, Stride);
            Expect(bRecorded, "能力のある件数付き描画は true");
            break;
        case CaseMode::CountDisabled:
            bRecorded = commands->DrawIndirectCount(args, 0u, count, 0u, TableCount, Stride);
            Expect(!bRecorded, "能力のない件数付き描画は false");
            break;
        case CaseMode::Invalid:
            CheckInvalidArguments(commands, args, count, noIndirect, fixture.Device->GetCapabilities().bDrawIndirectCount);
            break;
        }
        commands->EndRenderPass();
        commands->TextureBarrier(target, ResourceState::RenderTarget, ResourceState::CopySource);
        commands->CopyTextureToBuffer(target, readback, Width, Height, 0u);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, ReadbackBytes);
        commands->End();
        commands->Submit(true);
        fixture.Device->WaitIdle();

        const uint8_t* pixels = static_cast<const uint8_t*>(readback->Map(0u, ReadbackBytes));
        if (!pixels)
        {
            std::cerr << TestName << " ケース " << name << " の画素を読み戻せませんでした\n";
            return false;
        }
        uint32_t cellPixels[CellCount] = {};
        uint32_t mismatchCount = 0u;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const uint8_t* pixel = pixels + (y * Width + x) * 4u;
                const uint32_t cell = x / CellSize;
                const bool bCovered = cell < expectedCells && IsCovered(x % CellSize, y);
                cellPixels[cell] += pixel[0] != 0u ? 1u : 0u;
                if (pixel[0] != (bCovered ? 255u : 0u) || pixel[1] != 0u || pixel[2] != 0u ||
                    pixel[3] != (bCovered ? 255u : 0u))
                {
                    ++mismatchCount;
                }
            }
        }
        readback->Unmap();
        uint32_t total = 0u;
        std::cout << TestName << " ケース " << name << " 戻り値=" << bRecorded << " 各セル=";
        for (uint32_t i = 0u; i < CellCount; ++i)
        {
            total += cellPixels[i];
            std::cout << (i == 0u ? "" : ",") << cellPixels[i];
            Expect(cellPixels[i] == (i < expectedCells ? expectedPixels : 0u), "セルの被覆画素数が違います");
        }
        std::cout << " 合計=" << total << " 画素の不一致=" << mismatchCount << '\n';
        Expect(total == expectedCells * expectedPixels, "被覆画素の合計が違います");
        Expect(mismatchCount == 0u, "画素の色または三角形の被覆が違います");
        return true;
    }

    void RunAndCheck(Fixture& fixture, const ValidationCapture& capture, const char* name,
                     CaseMode mode, uint32_t expectedCells, uint32_t expectedPixels)
    {
        const uint32_t before = capture.Hits();
        bool bCompleted = false;
        {
            CapabilityOverride restore(*fixture.VulkanDevice);
            if (mode == CaseMode::MultiDisabledTwo || mode == CaseMode::MultiDisabledOne || mode == CaseMode::CountTwo)
            {
                restore.DisableMultiDraw();
            }
            if (mode == CaseMode::CountDisabled)
            {
                restore.DisableDrawCount();
            }
            bCompleted = RunCase(fixture, name, mode, expectedCells, expectedPixels);
        }
        Expect(bCompleted, "ケースを実行できませんでした");
        const uint32_t hits = capture.Hits() - before;
        std::cout << TestName << " ケース " << name << " 検証エラー=" << hits << '\n';
        Expect(hits == 0u, "ケースで Vulkan の検証エラーが出てはならない");
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            std::cout << TestName << " スキップ: NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された\n";
            return GpuTestSkipReturnCode;
        }
        ValidationCapture capture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            std::cout << TestName << " スキップ: Vulkan デバイスを利用できません\n";
            return GpuTestSkipReturnCode;
        }
        {
            Fixture fixture;
            fixture.Device = device;
            fixture.VulkanDevice = DynamicPointerCast<Vulkan::VulkanDevice>(device);
            if (!fixture.VulkanDevice || !CreateFixture(fixture))
            {
                std::cerr << TestName << " 確認用のパイプラインを作れませんでした\n";
                return 1;
            }
            const RHI::DeviceCapabilities capabilities = device->GetCapabilities();
            const uint32_t expectedPixels = CountExpectedPixels();
            std::cout << TestName << " 1 三角形の被覆画素 P=" << expectedPixels
                      << " 複数描画=" << capabilities.bMultiDrawIndirect
                      << " 件数付き描画=" << capabilities.bDrawIndirectCount << '\n';
            Expect(expectedPixels > 0u && expectedPixels < CellSize * CellSize, "三角形の被覆は空でもセル全体でもない");

            RunAndCheck(fixture, capture, "A", CaseMode::Compute, 3u, expectedPixels);
            if (capabilities.bDrawIndirectCount)
            {
                RunAndCheck(fixture, capture, "B 件数0", CaseMode::CountZero, 0u, expectedPixels);
                // 件数付き描画は複数描画の能力値とは独立している。
                RunAndCheck(fixture, capture, "B 件数2", CaseMode::CountTwo, 2u, expectedPixels);
            }
            else
            {
                std::cout << TestName << " ケース B スキップ: 件数付き描画に非対応\n";
            }
            if (capabilities.bMultiDrawIndirect)
            {
                RunAndCheck(fixture, capture, "C 複数描画", CaseMode::MultiTwo, 2u, expectedPixels);
            }
            else
            {
                std::cout << TestName << " ケース C の複数描画スキップ: 複数描画に非対応\n";
            }
            RunAndCheck(fixture, capture, "C 能力なし2件", CaseMode::MultiDisabledTwo, 0u, expectedPixels);
            RunAndCheck(fixture, capture, "C 能力なし1件", CaseMode::MultiDisabledOne, 1u, expectedPixels);
            RunAndCheck(fixture, capture, "D", CaseMode::CountDisabled, 0u, expectedPixels);
            RunAndCheck(fixture, capture, "E", CaseMode::Invalid, 0u, expectedPixels);
            device->WaitIdle();
        }
        const uint32_t hits = capture.Hits();
        std::cout << TestName << " 検証エラー合計=" << hits << '\n';
        Expect(hits == 0u, "Vulkan の検証エラーが出てはならない");
        std::cout << (g_failures == 0 ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return g_failures == 0 ? 0 : 1;
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
        std::cerr << TestName << " 例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}
