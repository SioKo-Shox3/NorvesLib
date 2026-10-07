// sparse（部分常駐）テクスチャの常駐フォールバックの GPU テスト。
// 512x512 の BC7（ミップ0は 256x256 のタイル 2x2 枚、ミップ2からがミップテイル）へ、ミップ0のタイル(0,0)と
// ミップテイルだけを結び、材質のシェーダーが使う標本関数（Common/SparseResidencySampling.glsl）で引く。
//   - 結んだタイルは、そのミップの色（逃げない）。
//   - 結んでいない領域は、常駐している最も細かいミップ（ミップ1も結んでいなければミップテイルの中のミップ2）の色。
//   - ミップ1のタイルを結ぶと、同じ領域がミップ1の色へ変わる（1段だけ下げて止まる）。
//   - 異方性 4 倍の勾配（実際の標本ミップは 0）でも、常駐しているミップ1を飛ばさず、ミップ1を結んでいなければミップ2になる。
//   - 異方性の上限（4 倍）を超える勾配（8:1。実際の標本ミップは 1）で、ミップ0のタイルが常駐していても、実際の標本ミップより
//     細かいミップ0へ戻らず、ミップ1を結んでいなければミップ2、結べばミップ1になる（POM の明示勾配と同じ形）。
//     勾配を明示する関数（コンピュート）と、暗黙の勾配で引く関数（描画。sparseTextureARB で標本する）の両方で確かめる。
// どの標本も、黒や未定義の値（読み戻しの初期値の -1 を含む）にならないことを確かめる。
// Vulkan デバイスが無い、sparse の結び付け・BC7・shaderResourceResidency が使えない環境では 125（スキップ）を返す。
#include "Rendering/ShaderManager.h"
#include "Rendering/SparsePagePool.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
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

    constexpr const char* TestName = "VirtualTextureResidencyVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ProbeCount = 8u;
    // 暗黙の勾配で引く確認の数（sparse_residency_probe.frag の quad ごと）と、その描画先の大きさ（quad 1つが 2x2 画素）
    constexpr uint32_t FragmentProbeCount = 5u;
    constexpr uint32_t FragmentTargetWidth = FragmentProbeCount * 2u;
    constexpr uint32_t FragmentTargetHeight = 2u;
    constexpr uint32_t FragmentReadbackBytes = FragmentTargetWidth * FragmentTargetHeight * 4u;
    constexpr uint32_t ProbeBufferBytes = 8u * 4u * sizeof(float);
    // 期待の色との許容差（2/255）
    constexpr float Tolerance = 2.0f / 255.0f;
    constexpr uint64_t Page = RHI::SparsePageSizeBytes;

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

    // ---- BC7 の手組みブロック（SparseBindVulkanTest と同じ） ----

    // BC7 のビット書き込み（下位ビットから詰める）
    class BitWriter
    {
    public:
        explicit BitWriter(uint8_t* out) : m_Out(out) { std::memset(out, 0, 16); }

        void Put(uint32_t value, uint32_t bitCount)
        {
            for (uint32_t bit = 0; bit < bitCount; ++bit, ++m_Position)
            {
                if (((value >> bit) & 1u) != 0u)
                {
                    m_Out[m_Position / 8u] = static_cast<uint8_t>(m_Out[m_Position / 8u] | (1u << (m_Position % 8u)));
                }
            }
        }

    private:
        uint8_t* m_Out;
        uint32_t m_Position = 0;
    };

    // BC7 モード6（1サブセット）。端点0 = 端点1 = 目的の色、インデックスはすべて 0 なので全画素が同じ色になる。
    void PackBC7Block(uint8_t* out, const uint8_t rgba[4])
    {
        BitWriter writer(out);
        writer.Put(1u << 6u, 7u);
        for (uint32_t channel = 0; channel < 4u; ++channel)
        {
            writer.Put(rgba[channel] >> 1u, 7u);
            writer.Put(rgba[channel] >> 1u, 7u);
        }
        writer.Put(rgba[0] & 1u, 1u);
        writer.Put(rgba[0] & 1u, 1u);
    }

    // P ビットは4チャンネルで共有なので、復元される値は上位7ビット＋チャンネル0の最下位ビット
    void ExpectedBC7Color(const uint8_t rgba[4], float out[4])
    {
        const uint32_t pBit = rgba[0] & 1u;
        for (uint32_t channel = 0; channel < 4u; ++channel)
        {
            out[channel] = static_cast<float>(((rgba[channel] >> 1u) << 1u) | pBit) / 255.0f;
        }
    }

    // 領域（blocksPerRow x blocksPerRow ブロック）を1色で埋める。
    void FillBlocks(uint8_t* dst, uint32_t blocksPerRow, const uint8_t color[4])
    {
        for (uint32_t block = 0; block < blocksPerRow * blocksPerRow; ++block)
        {
            PackBC7Block(dst + block * 16u, color);
        }
    }

    // ---- 読み戻し（sparse_residency_probe.comp） ----

    DescriptorSetDesc MakeProbeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {ResourceBindType::CombinedImageSampler, ResourceBindType::RWBuffer};
        for (uint32_t bindingIndex = 0; bindingIndex < 2u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    struct ProbeResources
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        SamplerPtr Sampler;
        BufferPtr ResultBuffer;
        BufferPtr ReadbackBuffer;
    };

    bool ReadProbeColors(ProbeResources& resources, const TexturePtr& texture, float outColors[ProbeCount][4])
    {
        DescriptorSetPtr descriptorSet = resources.Device->CreateDescriptorSet(MakeProbeDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindTexture(0u, texture);
        descriptorSet->BindSampler(0u, resources.Sampler);
        descriptorSet->BindStorageBuffer(1u, resources.ResultBuffer, 0u, ProbeBufferBytes);
        descriptorSet->Update();

        CommandListPtr commandList = resources.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        commandList->Begin();
        commandList->BufferBarrier(resources.ResultBuffer, ResourceState::Undefined, ResourceState::UnorderedAccess,
                                   0u, ProbeBufferBytes);
        commandList->SetPipeline(resources.Pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Dispatch(1u, 1u, 1u);
        commandList->BufferBarrier(resources.ResultBuffer, ResourceState::UnorderedAccess, ResourceState::CopySource,
                                   0u, ProbeBufferBytes);
        commandList->CopyBuffer(resources.ResultBuffer, resources.ReadbackBuffer, ProbeBufferBytes);
        commandList->BufferBarrier(resources.ReadbackBuffer, ResourceState::CopyDest, ResourceState::HostRead,
                                   0u, ProbeBufferBytes);
        commandList->End();
        commandList->Submit(true);
        resources.Device->WaitIdle();

        const void* mapped = resources.ReadbackBuffer->Map(0u, ProbeBufferBytes);
        if (mapped == nullptr)
        {
            std::cerr << "読み戻しのバッファを写像できませんでした\n";
            return false;
        }
        std::memcpy(outColors, mapped, ProbeCount * 4u * sizeof(float));
        resources.ReadbackBuffer->Unmap();
        return true;
    }

    // ---- 読み戻し（sparse_residency_probe.frag。暗黙の勾配で引く） ----

    DescriptorSetDesc MakeFragmentProbeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        DescriptorBinding binding;
        binding.binding = 0;
        binding.type = ResourceBindType::CombinedImageSampler;
        binding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(binding);
        return desc;
    }

    struct FragmentProbeResources
    {
        DevicePtr Device;
        RenderPassPtr RenderPass;
        FramebufferPtr Framebuffer;
        PipelinePtr Pipeline;
        SamplerPtr Sampler;
        TexturePtr Target;
        BufferPtr ReadbackBuffer;
    };

    bool CreateFragmentProbeResources(FragmentProbeResources& resources, ShaderManager& shaderManager)
    {
        ShaderPtr vertexShader = shaderManager.LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        ShaderPtr pixelShader = shaderManager.LoadShader("sparse_residency_probe.frag", RHI::ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            // shaderc が GL_ARB_sparse_texture2 を通さない場合はここで失敗する（stop-when）。
            std::cerr << "sparse_residency_probe.frag をコンパイルできませんでした\n";
            return false;
        }

        TextureDesc targetDesc = TextureDesc::RenderTarget(FragmentTargetWidth, FragmentTargetHeight,
                                                           Format::R8G8B8A8_UNORM, "VirtualTextureResidencyFragmentTarget");
        targetDesc.Usage = targetDesc.Usage | ResourceUsage::TransferSrc;
        resources.Target = resources.Device->CreateTexture(targetDesc);

        RenderPassDesc renderPassDesc;
        AttachmentDesc colorAttachment;
        colorAttachment.format = Format::R8G8B8A8_UNORM;
        colorAttachment.clear = true;
        colorAttachment.loadOp = AttachmentLoadOp::Clear;
        colorAttachment.storeOp = AttachmentStoreOp::Store;
        colorAttachment.initialState = ResourceState::Undefined;
        colorAttachment.finalState = ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        resources.RenderPass = resources.Device->CreateRenderPass(renderPassDesc);
        if (!resources.Target || !resources.RenderPass)
        {
            std::cerr << "描画先またはレンダーパスを作れませんでした\n";
            return false;
        }

        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = resources.RenderPass;
        framebufferDesc.colorTargets.push_back(resources.Target);
        framebufferDesc.width = FragmentTargetWidth;
        framebufferDesc.height = FragmentTargetHeight;
        resources.Framebuffer = resources.Device->CreateFramebuffer(framebufferDesc);

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
        BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = resources.RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(MakeFragmentProbeDescriptorSetDesc());
        resources.Pipeline = resources.Device->CreateGraphicsPipeline(pipelineDesc);

        // 異方性 4 倍。ミップの間は補間せず（点）、色が1つのミップの色そのものになるようにする。
        SamplerDesc samplerDesc;
        samplerDesc.filterMin = FilterMode::Linear;
        samplerDesc.filterMag = FilterMode::Linear;
        samplerDesc.filterMip = FilterMode::Point;
        samplerDesc.addressU = TextureAddressMode::Clamp;
        samplerDesc.addressV = TextureAddressMode::Clamp;
        samplerDesc.addressW = TextureAddressMode::Clamp;
        samplerDesc.maxAnisotropy = 4;
        resources.Sampler = resources.Device->CreateSampler(samplerDesc);

        resources.ReadbackBuffer = resources.Device->CreateBuffer(
            BufferDesc(FragmentReadbackBytes, ResourceUsage::TransferDst, true, "VirtualTextureResidencyFragmentReadback"));
        if (!resources.Framebuffer || !resources.Pipeline || !resources.Sampler || !resources.ReadbackBuffer)
        {
            std::cerr << "描画の確認用の資源を作れませんでした\n";
            return false;
        }
        return true;
    }

    // quad ごとの画素 (quad * 2, 0) の色を 0〜1 の浮動小数で読み戻す。
    bool ReadFragmentProbeColors(FragmentProbeResources& resources, const TexturePtr& texture,
                                 float outColors[FragmentProbeCount][4])
    {
        DescriptorSetPtr descriptorSet = resources.Device->CreateDescriptorSet(MakeFragmentProbeDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindTexture(0u, texture);
        descriptorSet->BindSampler(0u, resources.Sampler);
        descriptorSet->Update();

        CommandListPtr commandList = resources.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        Viewport viewport;
        viewport.width = static_cast<float>(FragmentTargetWidth);
        viewport.height = static_cast<float>(FragmentTargetHeight);
        ScissorRect scissor;
        scissor.right = static_cast<int32_t>(FragmentTargetWidth);
        scissor.bottom = static_cast<int32_t>(FragmentTargetHeight);

        commandList->Begin();
        commandList->BeginRenderPass(resources.RenderPass, resources.Framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(resources.Pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Draw(3u);
        commandList->EndRenderPass();
        commandList->TextureBarrier(resources.Target, ResourceState::ShaderResource, ResourceState::CopySource);
        commandList->BufferBarrier(resources.ReadbackBuffer, ResourceState::Undefined, ResourceState::CopyDest, 0u,
                                   FragmentReadbackBytes);
        commandList->CopyTextureToBuffer(resources.Target, resources.ReadbackBuffer, FragmentTargetWidth,
                                         FragmentTargetHeight, 0u);
        commandList->TextureBarrier(resources.Target, ResourceState::CopySource, ResourceState::ShaderResource);
        commandList->BufferBarrier(resources.ReadbackBuffer, ResourceState::CopyDest, ResourceState::HostRead, 0u,
                                   FragmentReadbackBytes);
        commandList->End();
        commandList->Submit(true);
        resources.Device->WaitIdle();

        const void* mapped = resources.ReadbackBuffer->Map(0u, FragmentReadbackBytes);
        if (mapped == nullptr)
        {
            std::cerr << "読み戻しのバッファを写像できませんでした\n";
            return false;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(mapped);
        for (uint32_t probe = 0; probe < FragmentProbeCount; ++probe)
        {
            const uint8_t* pixel = bytes + probe * 2u * 4u;
            for (uint32_t channel = 0; channel < 4u; ++channel)
            {
                outColors[probe][channel] = static_cast<float>(pixel[channel]) / 255.0f;
            }
        }
        resources.ReadbackBuffer->Unmap();
        return true;
    }

    // 読み戻した色が期待の色と一致し、黒や未定義の値でないことを確かめる。
    void ExpectProbe(const char* phase, uint32_t probe, const char* what, const float color[4], const uint8_t expectedRgba[4])
    {
        float expected[4];
        ExpectedBC7Color(expectedRgba, expected);
        float maxDifference = 0.0f;
        for (uint32_t channel = 0; channel < 4u; ++channel)
        {
            maxDifference = std::fmax(maxDifference, std::fabs(color[channel] - expected[channel]));
        }
        std::cout << TestName << " " << phase << " probe=" << probe << " " << what << " readback=(" << color[0] << ", "
                  << color[1] << ", " << color[2] << ", " << color[3] << ") expected=(" << expected[0] << ", "
                  << expected[1] << ", " << expected[2] << ", " << expected[3] << ") max_diff=" << maxDifference
                  << std::endl;
        Expect(maxDifference <= Tolerance, what);
        // 期待の色はどれも黒ではないので、黒（非常駐の標本が返しうる 0）や初期値（-1）にならない。
        Expect(color[0] + color[1] + color[2] > 0.1f, "標本した色が黒や未定義の値になってはならない");
    }

    bool Bind(IDevice& device, const SparseBindRequest& request, const char* label)
    {
        const bool bOk = device.BindSparse(request);
        std::cout << TestName << " bind label=" << label << " tiles=" << request.Tiles.size()
                  << " tailPages=" << request.MipTails.size() << " ok=" << bOk << std::endl;
        return bOk;
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
        const SparseCapabilities& sparse = device->GetCapabilities().Sparse;
        if (!sparse.bSparseBinding || !sparse.bResidencyImage2D)
        {
            return SkipGpuTest("sparse の結び付けまたは 2D の residency が無いデバイス（VT は使わず BC の全常駐で描く）");
        }
        if (!sparse.bShaderResourceResidency)
        {
            return SkipGpuTest("シェーダーで常駐を問い合わせられないデバイス（VT は使わず BC の全常駐で描く）");
        }
        if (!device->GetCapabilities().bTextureCompressionBC)
        {
            return SkipGpuTest("デバイスが textureCompressionBC に対応していません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }

        ProbeResources resources;
        resources.Device = device;
        {
            ShaderPtr shader = shaderManager.LoadShader("sparse_residency_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                // shaderc が GL_ARB_sparse_texture2 を通さない場合はここで失敗する（stop-when）。
                std::cerr << "sparse_residency_probe.comp をコンパイルできませんでした\n";
                return 1;
            }
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeProbeDescriptorSetDesc());
            resources.Pipeline = device->CreateComputePipeline(pipelineDesc);

            // 点サンプルにして、色がミップの色そのものになるようにする。異方性は 4 倍（勾配を明示する関数で、
            // 異方性の勾配の実際の標本ミップが細かい側になるようにする）。
            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            samplerDesc.maxAnisotropy = 4;
            resources.Sampler = device->CreateSampler(samplerDesc);

            BufferDesc resultDesc;
            resultDesc.Size = ProbeBufferBytes;
            resultDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
            resultDesc.DebugName = "VirtualTextureResidencyProbeResults";
            resources.ResultBuffer = device->CreateBuffer(resultDesc);
            resources.ReadbackBuffer = device->CreateBuffer(
                BufferDesc(ProbeBufferBytes, ResourceUsage::TransferDst, true, "VirtualTextureResidencyProbeReadback"));
        }
        if (!resources.Pipeline || !resources.Sampler || !resources.ResultBuffer || !resources.ReadbackBuffer)
        {
            std::cerr << "確認用の資源を作れませんでした\n";
            return 1;
        }

        FragmentProbeResources fragmentResources;
        fragmentResources.Device = device;
        if (!CreateFragmentProbeResources(fragmentResources, shaderManager))
        {
            return 1;
        }

        // ミップ0・ミップ1・ミップ2の色（見分けがつく3色）。
        const uint8_t mip0Color[4] = {200u, 40u, 20u, 255u};
        const uint8_t mip1Color[4] = {30u, 190u, 60u, 255u};
        const uint8_t mip2Color[4] = {20u, 60u, 220u, 255u};

        {
            // 1つの塊を 4 ページにして、ミップ0のタイル・ミップ1のタイル・ミップテイルを借りる。
            SparsePagePool pool(device, 4 * Page);

            // 先に宣言したものが後に破棄される（ページを返す前にテクスチャを破棄する）。
            VariableArray<SparsePagePool::PageLease> tailLeases;
            SparsePagePool::PageLease mip0Lease;
            SparsePagePool::PageLease mip1Lease;

            TextureDesc desc;
            desc.Width = 512;
            desc.Height = 512;
            desc.MipLevels = 10;
            desc.TextureFormat = Format::BC7_UNORM;
            desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            desc.bSparse = true;
            desc.DebugName = "VirtualTextureResidencyTarget";
            TexturePtr texture = device->CreateTexture(desc);
            Expect(texture != nullptr, "sparse の BC7 テクスチャを作れなければならない");
            if (texture == nullptr)
            {
                return 1;
            }

            SparseTextureInfo info;
            Expect(texture->GetSparseInfo(info), "GetSparseInfo が成功しなければならない");
            Expect(info.TileWidth == 256 && info.TileHeight == 256, "BC7 のタイルは 256x256");
            Expect(info.MipTailFirstLevel == 2, "ミップテイルはミップ2から");
            Expect(info.TilesX[0] == 2, "ミップ0は 2x2 枚のタイル");
            const uint32_t tailPages = static_cast<uint32_t>((info.MipTailSize + Page - 1) / Page);

            // ---- 結ぶ: ミップ0のタイル(0,0)とミップテイル全部（ミップ1のタイルはまだ結ばない） ----
            mip0Lease = pool.Acquire();
            Expect(mip0Lease.IsValid(), "ミップ0のタイル用のページを借りられなければならない");
            SparseBindRequest bindRequest;
            {
                SparseTileBind tile;
                tile.Texture = texture.get();
                tile.MipLevel = 0;
                tile.TileX = 0;
                tile.TileY = 0;
                tile.Page = mip0Lease.GetPage();
                bindRequest.Tiles.push_back(tile);
            }
            for (uint32_t page = 0; page < tailPages; ++page)
            {
                tailLeases.push_back(pool.Acquire());
                Expect(tailLeases.back().IsValid(), "ミップテイル用のページを借りられなければならない");
                SparseMipTailBind tail;
                tail.Texture = texture.get();
                tail.PageIndex = page;
                tail.Page = tailLeases.back().GetPage();
                bindRequest.MipTails.push_back(tail);
            }
            Expect(Bind(*device, bindRequest, "ミップ0のタイル(0,0)とミップテイル"), "結び付けが成功しなければならない");

            // ---- 書く: ミップ0のタイル(0,0)（全面 mip0Color）とミップ2（128x128、全面 mip2Color） ----
            // ミップ1のタイルは結んでいないので書かない。ミップ3以降は読まれない（ミップ2で止まる）。
            const uint64_t mip2Offset = Page;
            const uint64_t mip2Bytes = 32u * 32u * 16u;
            VariableArray<uint8_t> staging;
            staging.resize(static_cast<size_t>(mip2Offset + mip2Bytes));
            FillBlocks(staging.data(), 64u, mip0Color);
            FillBlocks(staging.data() + mip2Offset, 32u, mip2Color);
            BufferPtr stagingBuffer = device->CreateBuffer(
                BufferDesc(staging.size(), ResourceUsage::TransferSrc, true, "VirtualTextureResidencyStaging"));
            Expect(stagingBuffer != nullptr, "ステージングのバッファを作れなければならない");
            if (stagingBuffer == nullptr)
            {
                return 1;
            }
            stagingBuffer->Update(staging.data(), staging.size(), 0u);

            {
                CommandListPtr upload = device->CreateCommandList();
                upload->Begin();
                upload->TextureBarrier(texture, ResourceState::Undefined, ResourceState::CopyDest);
                upload->CopyBufferToTexture(stagingBuffer, texture, 256u, 256u, 0u, 0u);
                upload->CopyBufferToTexture(stagingBuffer, texture, 128u, 128u, mip2Offset, 2u);
                upload->TextureBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderResource);
                upload->End();
                upload->Submit(true);
                device->WaitIdle();
            }

            // ---- 読み戻す(1): ミップ1のタイルは未結合 ----
            float colors[ProbeCount][4] = {};
            bool bRead = ReadProbeColors(resources, texture, colors);
            Expect(bRead, "計算シェーダーで読み戻せなければならない");
            if (bRead)
            {
                ExpectProbe("ミップ1未結合", 0, "結んだタイルはミップ0の色（逃げない）", colors[0], mip0Color);
                ExpectProbe("ミップ1未結合", 1, "結んでいない領域は粗いミップ（ミップ2）の色へ逃げる（ミップ明示）", colors[1],
                            mip2Color);
                ExpectProbe("ミップ1未結合", 2, "結んでいない領域は粗いミップ（ミップ2）の色へ逃げる（勾配明示）", colors[2],
                            mip2Color);
                ExpectProbe("ミップ1未結合", 3, "ミップテイルの中のミップ2はそのまま引ける", colors[3], mip2Color);
                ExpectProbe("ミップ1未結合", 4, "結んだタイルは勾配明示でもミップ0の色（逃げない）", colors[4], mip0Color);
                ExpectProbe("ミップ1未結合", 5, "ミップ2相当の勾配はミップテイルの中のミップ2の色", colors[5], mip2Color);
                ExpectProbe("ミップ1未結合", 6, "異方性 4 倍の勾配でも、結んでいない領域はミップ2の色へ逃げる", colors[6], mip2Color);
                ExpectProbe("ミップ1未結合", 7, "異方性の上限を超える 8:1 の勾配は、標本ミップ1から逃げてミップ2の色（ミップ0へ戻らない）",
                            colors[7], mip2Color);
            }

            float fragmentColors[FragmentProbeCount][4] = {};
            bool bFragmentRead = ReadFragmentProbeColors(fragmentResources, texture, fragmentColors);
            Expect(bFragmentRead, "描画で読み戻せなければならない");
            if (bFragmentRead)
            {
                ExpectProbe("ミップ1未結合(暗黙)", 0, "結んでいない領域は粗いミップ（ミップ2）の色へ逃げる", fragmentColors[0], mip2Color);
                ExpectProbe("ミップ1未結合(暗黙)", 1, "異方性 4 倍でも結んでいない領域はミップ2の色へ逃げる", fragmentColors[1],
                            mip2Color);
                ExpectProbe("ミップ1未結合(暗黙)", 2, "標本ミップ 2 はミップテイルの中なので逃げずミップ2の色", fragmentColors[2],
                            mip2Color);
                ExpectProbe("ミップ1未結合(暗黙)", 3, "結んだタイルはミップ0の色（逃げない）", fragmentColors[3], mip0Color);
                ExpectProbe("ミップ1未結合(暗黙)", 4, "8:1 の勾配を POM と同じ形で引くと、標本ミップ1から逃げてミップ2の色",
                            fragmentColors[4], mip2Color);
            }

            // ---- ミップ1のタイルを結んで、ミップ1の色を書く ----
            mip1Lease = pool.Acquire();
            Expect(mip1Lease.IsValid(), "ミップ1のタイル用のページを借りられなければならない");
            SparseBindRequest mip1Request;
            {
                SparseTileBind tile;
                tile.Texture = texture.get();
                tile.MipLevel = 1;
                tile.TileX = 0;
                tile.TileY = 0;
                tile.Page = mip1Lease.GetPage();
                mip1Request.Tiles.push_back(tile);
            }
            Expect(Bind(*device, mip1Request, "ミップ1のタイル(0,0)"), "ミップ1のタイルを結べなければならない");

            {
                VariableArray<uint8_t> mip1Staging;
                mip1Staging.resize(static_cast<size_t>(Page));
                FillBlocks(mip1Staging.data(), 64u, mip1Color);
                BufferPtr mip1Buffer = device->CreateBuffer(
                    BufferDesc(mip1Staging.size(), ResourceUsage::TransferSrc, true, "VirtualTextureResidencyStagingMip1"));
                Expect(mip1Buffer != nullptr, "ミップ1のステージングのバッファを作れなければならない");
                if (mip1Buffer == nullptr)
                {
                    return 1;
                }
                mip1Buffer->Update(mip1Staging.data(), mip1Staging.size(), 0u);

                CommandListPtr upload = device->CreateCommandList();
                upload->Begin();
                upload->TextureBarrier(texture, ResourceState::ShaderResource, ResourceState::CopyDest);
                upload->CopyBufferToTexture(mip1Buffer, texture, 256u, 256u, 0u, 1u);
                upload->TextureBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderResource);
                upload->End();
                upload->Submit(true);
                device->WaitIdle();
            }

            // ---- 読み戻す(2): ミップ1のタイルも結んだ ----
            bRead = ReadProbeColors(resources, texture, colors);
            Expect(bRead, "ミップ1を結んだあとも計算シェーダーで読み戻せなければならない");
            if (bRead)
            {
                ExpectProbe("ミップ1結合", 0, "結んだタイルはミップ0の色（変わらない）", colors[0], mip0Color);
                ExpectProbe("ミップ1結合", 1, "結んでいない領域は1段だけ粗いミップ1の色へ逃げる（ミップ明示）", colors[1], mip1Color);
                ExpectProbe("ミップ1結合", 2, "結んでいない領域は1段だけ粗いミップ1の色へ逃げる（勾配明示）", colors[2], mip1Color);
                ExpectProbe("ミップ1結合", 3, "ミップテイルの中のミップ2は変わらない", colors[3], mip2Color);
                ExpectProbe("ミップ1結合", 4, "結んだタイルは勾配明示でもミップ0の色（変わらない）", colors[4], mip0Color);
                ExpectProbe("ミップ1結合", 5, "ミップ2相当の勾配はミップテイルの中のミップ2の色（変わらない）", colors[5], mip2Color);
                ExpectProbe("ミップ1結合", 6, "異方性 4 倍の勾配は常駐しているミップ1を飛ばさずミップ1の色", colors[6], mip1Color);
                ExpectProbe("ミップ1結合", 7, "異方性の上限を超える 8:1 の勾配は標本ミップ1の色", colors[7], mip1Color);
            }

            bFragmentRead = ReadFragmentProbeColors(fragmentResources, texture, fragmentColors);
            Expect(bFragmentRead, "ミップ1を結んだあとも描画で読み戻せなければならない");
            if (bFragmentRead)
            {
                ExpectProbe("ミップ1結合(暗黙)", 0, "結んでいない領域は1段だけ粗いミップ1の色へ逃げる", fragmentColors[0], mip1Color);
                ExpectProbe("ミップ1結合(暗黙)", 1, "異方性 4 倍でも常駐しているミップ1を飛ばさずミップ1の色", fragmentColors[1],
                            mip1Color);
                ExpectProbe("ミップ1結合(暗黙)", 2, "標本ミップ 2 はミップテイルの中なのでミップ2の色（変わらない）",
                            fragmentColors[2], mip2Color);
                ExpectProbe("ミップ1結合(暗黙)", 3, "結んだタイルはミップ0の色（変わらない）", fragmentColors[3], mip0Color);
                ExpectProbe("ミップ1結合(暗黙)", 4, "8:1 の勾配を POM と同じ形で引くと標本ミップ1の色", fragmentColors[4], mip1Color);
            }

            // ---- 後片付け: 全部外して完了を待ってから、ページを返す（先にテクスチャを破棄する） ----
            SparseBindRequest releaseAll;
            {
                SparseTileBind tile0;
                tile0.Texture = texture.get();
                tile0.MipLevel = 0;
                tile0.TileX = 0;
                tile0.TileY = 0;
                releaseAll.Tiles.push_back(tile0);
                SparseTileBind tile1 = tile0;
                tile1.MipLevel = 1;
                releaseAll.Tiles.push_back(tile1);
            }
            for (uint32_t page = 0; page < tailPages; ++page)
            {
                SparseMipTailBind tail;
                tail.Texture = texture.get();
                tail.PageIndex = page;
                releaseAll.MipTails.push_back(tail);
            }
            Expect(Bind(*device, releaseAll, "全部外す"), "全部外せなければならない");
            device->WaitIdle();
            texture.reset();
        }

        device->WaitIdle();
        resources = ProbeResources{};
        fragmentResources = FragmentProbeResources{};
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
