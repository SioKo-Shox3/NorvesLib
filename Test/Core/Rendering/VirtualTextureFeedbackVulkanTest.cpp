// VT のフィードバック（材質のサンプルの箇所がタイルの要求を GPU のバッファへ書く）の GPU テスト。
// 1024x1024 の BC7 の sparse テクスチャ（ミップ 1 の 2x2 枚とミップ 2 の 1 枚のタイル、ミップテイルだけを結ぶ）を、材質のシェーダーと同じ関数
// （Common/SparseResidencySampling.glsl・Common/VirtualTextureFeedback.glsl）で既知の UV の面に描き、
// VirtualTextureFeedbackRing のバッファへ期待のタイルの要求が書かれることを確かめる。
//   - 常駐している領域: 4×4 の画素のうち、フレームごとに巡回する 1 画素だけが、その画素の UV の欲しいミップとタイルを書く。
//     タイル境界をまたぐ画素の並びで、位相 0〜15 の画素が別々のタイルを書くこと、ミップ・x・y・テクスチャの番号が正しく詰まることを確かめる。
//   - 非常駐で粗いミップへ逃げた領域: 巡回によらず全画素が書く（欲しいミップのタイルすべてが要求になる）。
//   - 同じタイルの要求は、ハッシュの表で要求の列の 1 語に減り、重なった件数は件数の表へ足される。
//     読み戻した各タイルの件数（HitCount）は、そのタイルを要求した画素の数になる（面積の違う 4 タイルで 5・7・7・13 の順）。
//   - パラメータ 0（VT でない材質）は何も書かない。
//   - 要求の件数が capacity を超えたとき、溢れた件数がヘッダに残る（capacity はバッファの語数から求める）。
// Vulkan デバイスが無い、sparse の結び付け・BC7・shaderResourceResidency・fragmentStoresAndAtomics が使えない環境では 125（スキップ）を返す。
#include "Rendering/ShaderManager.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/VirtualTextureFeedbackRing.h"
#include "Rendering/VirtualTextureRequestSet.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
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
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "VirtualTextureFeedbackVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint64_t Page = RHI::SparsePageSizeBytes;

    // 確認（4×4 画素）の数と、描画先の大きさ
    constexpr uint32_t ProbeCount = 4u;
    constexpr uint32_t TargetWidth = ProbeCount * 4u;
    constexpr uint32_t TargetHeight = 4u;
    // 要求に載せるテクスチャの番号（0 でない値で、番号の詰め方を確かめる）
    constexpr uint32_t TextureIndex = 5u;
    // BC7 の標準ブロック形状（texel）
    constexpr uint32_t TileSize = 256u;

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

    DescriptorSetDesc MakeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {ResourceBindType::CombinedImageSampler, ResourceBindType::ConstantBuffer,
                                          ResourceBindType::RWBuffer};
        for (uint32_t bindingIndex = 0; bindingIndex < 3u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    struct ProbeResources
    {
        DevicePtr Device;
        RenderPassPtr RenderPass;
        FramebufferPtr Framebuffer;
        PipelinePtr Pipeline;
        SamplerPtr Sampler;
        TexturePtr Target;
        BufferPtr ParamBuffer;
    };

    bool CreateProbeResources(ProbeResources& resources, ShaderManager& shaderManager)
    {
        ShaderPtr vertexShader = shaderManager.LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        ShaderPtr pixelShader = shaderManager.LoadShader("vt_feedback_probe.frag", RHI::ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            std::cerr << "vt_feedback_probe.frag をコンパイルできませんでした\n";
            return false;
        }

        resources.Target = resources.Device->CreateTexture(
            TextureDesc::RenderTarget(TargetWidth, TargetHeight, Format::R8G8B8A8_UNORM, "VirtualTextureFeedbackTarget"));

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
        framebufferDesc.width = TargetWidth;
        framebufferDesc.height = TargetHeight;
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
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
        resources.Pipeline = resources.Device->CreateGraphicsPipeline(pipelineDesc);

        // 勾配が等方なので異方性は使わない。ミップの間は補間する（材質の既定のサンプラーと同じ。粗い側のミップも常駐を見る）。
        SamplerDesc samplerDesc;
        samplerDesc.filterMin = FilterMode::Linear;
        samplerDesc.filterMag = FilterMode::Linear;
        samplerDesc.filterMip = FilterMode::Linear;
        samplerDesc.addressU = TextureAddressMode::Clamp;
        samplerDesc.addressV = TextureAddressMode::Clamp;
        samplerDesc.addressW = TextureAddressMode::Clamp;
        samplerDesc.maxAnisotropy = 1;
        resources.Sampler = resources.Device->CreateSampler(samplerDesc);

        resources.ParamBuffer = resources.Device->CreateBuffer(
            BufferDesc(16u, ResourceUsage::ConstantBuffer, true, "VirtualTextureFeedbackParams"));
        if (!resources.Framebuffer || !resources.Pipeline || !resources.Sampler || !resources.ParamBuffer)
        {
            std::cerr << "描画の確認用の資源を作れませんでした\n";
            return false;
        }
        return true;
    }

    // 1 フレーム分: リングからこのフレームのバッファを得て、パラメータ param で描き、読み戻し用のバリアを積んで提出する。
    // 提出後に完了を待ち、リングを進めて（2 フレーム分の空回し）、読み戻した要求を out へ受け取る。
    // bViaFloat が true のときは、材質の UBO と同じく param を float にして渡す（シェーダーが DecodeVirtualTextureFeedbackParam で戻す）。
    bool RunFrame(ProbeResources& resources, VirtualTextureFeedbackRing& ring, const TexturePtr& texture, uint32_t param,
                  uint64_t& inOutSerial, VirtualTextureRequestSet& out, bool bViaFloat = false)
    {
        ring.BeginFrame(inOutSerial);
        BufferPtr feedbackBuffer = ring.GetCurrentBuffer();
        if (!feedbackBuffer)
        {
            std::cerr << "このフレームの要求のバッファを獲得できませんでした\n";
            return false;
        }

        uint32_t params[4] = {param, 0u, 0u, 0u};
        if (bViaFloat)
        {
            const float floatParam = static_cast<float>(param);
            params[0] = 0u;
            std::memcpy(&params[1], &floatParam, sizeof(floatParam));
        }
        resources.ParamBuffer->Update(params, sizeof(params), 0u);

        DescriptorSetPtr descriptorSet = resources.Device->CreateDescriptorSet(MakeDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindTexture(0u, texture);
        descriptorSet->BindSampler(0u, resources.Sampler);
        descriptorSet->BindConstantBuffer(1u, resources.ParamBuffer, 0u, static_cast<uint32_t>(sizeof(params)));
        descriptorSet->BindStorageBuffer(2u, feedbackBuffer, 0u, static_cast<uint32_t>(ring.GetBufferBytes()));
        descriptorSet->Update();

        CommandListPtr commandList = resources.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        Viewport viewport;
        viewport.width = static_cast<float>(TargetWidth);
        viewport.height = static_cast<float>(TargetHeight);
        ScissorRect scissor;
        scissor.right = static_cast<int32_t>(TargetWidth);
        scissor.bottom = static_cast<int32_t>(TargetHeight);

        commandList->Begin();
        commandList->BeginRenderPass(resources.RenderPass, resources.Framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(resources.Pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Draw(3u);
        commandList->EndRenderPass();
        const bool bBarrier = ring.RecordHostReadBarrier(*commandList);
        Expect(bBarrier, "獲得したフレームでは読み戻し用のバリアを記録できなければならない");
        commandList->End();
        commandList->Submit(true);
        resources.Device->WaitIdle();

        ring.CommitFrame(++inOutSerial);
        // 書いたフレームから 2 フレーム以上経つと読み戻される。空回しのフレームは何も書かないので中止する。
        for (int idle = 0; idle < 2; ++idle)
        {
            ring.BeginFrame(inOutSerial);
            ring.AbortFrame();
        }
        out.Clear();
        ring.TakeRequests(out);
        return true;
    }

    bool HasTile(const VirtualTextureRequestSet& set, uint32_t textureIndex, uint32_t mip, uint32_t x, uint32_t y)
    {
        VirtualTextureTileKey key;
        key.TextureIndex = textureIndex;
        key.Mip = mip;
        key.X = x;
        key.Y = y;
        uint64_t frame = 0;
        return set.Find(key, frame);
    }

    struct ExpectedTile
    {
        uint32_t Mip;
        uint32_t X;
        uint32_t Y;
        /** 期待の件数（そのタイルを要求した画素の数） */
        uint32_t Hits;
    };

    // 要求の集合が期待のタイルと過不足なく一致し、各タイルの件数が期待の画素の数と一致することを確かめる。
    void ExpectExactly(const char* phase, const VirtualTextureRequestSet& set, uint32_t expectedTextureIndex,
                       const ExpectedTile* tiles, uint32_t tileCount)
    {
        bool bAllFound = true;
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            const bool bFound = HasTile(set, expectedTextureIndex, tiles[i].Mip, tiles[i].X, tiles[i].Y);
            if (!bFound)
            {
                std::cerr << TestName << " " << phase << " 期待のタイルが無い mip=" << tiles[i].Mip << " x=" << tiles[i].X
                          << " y=" << tiles[i].Y << std::endl;
            }
            bAllFound = bAllFound && bFound;
        }
        Expect(bAllFound, "期待のタイルの要求がすべて書かれていなければならない");

        // 重複は要求の列の 1 語と件数の表へ減るので、集合のタイルの数 = 期待のタイルの数、各タイルの件数 = 要求した画素の数
        uint32_t totalHits = 0;
        uint32_t expectedTotalHits = 0;
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            expectedTotalHits += tiles[i].Hits;
        }
        for (uint32_t textureIndex : set.GetTextureIndices())
        {
            Expect(textureIndex == expectedTextureIndex, "要求のテクスチャの番号が材質のパラメータの番号と一致しなければならない");
            for (const VirtualTextureTileRequest& request : set.GetRequests(textureIndex))
            {
                totalHits += request.HitCount;
                for (uint32_t i = 0; i < tileCount; ++i)
                {
                    if (tiles[i].Mip == request.Mip && tiles[i].X == request.X && tiles[i].Y == request.Y)
                    {
                        if (request.HitCount != tiles[i].Hits)
                        {
                            std::cerr << TestName << " " << phase << " 件数が違う mip=" << request.Mip << " x=" << request.X
                                      << " y=" << request.Y << " hits=" << request.HitCount << " expected=" << tiles[i].Hits
                                      << std::endl;
                        }
                        Expect(request.HitCount == tiles[i].Hits, "タイルの件数は、そのタイルを要求した画素の数と同じでなければならない");
                    }
                }
            }
        }
        if (set.GetRequestCount() != tileCount || totalHits != expectedTotalHits)
        {
            std::cerr << TestName << " " << phase << " 件数が違う tiles=" << set.GetRequestCount() << " hits=" << totalHits
                      << " expected=" << expectedTotalHits << std::endl;
        }
        Expect(set.GetRequestCount() == tileCount, "余計なタイルの要求が書かれてはならない");
        Expect(totalHits == expectedTotalHits, "書かれた件数の合計は期待の画素の数の合計と同じでなければならない");
        Expect(set.GetOverflowCount() == 0, "capacity に収まる要求で溢れてはならない");
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
        const auto& capabilities = device->GetCapabilities();
        const SparseCapabilities& sparse = capabilities.Sparse;
        if (!sparse.bSparseBinding || !sparse.bResidencyImage2D)
        {
            return SkipGpuTest("sparse の結び付けまたは 2D の residency が無いデバイス（VT は使わず BC の全常駐で描く）");
        }
        if (!capabilities.SupportsVirtualTextureFeedback())
        {
            return SkipGpuTest("フィードバックを書けないデバイス（shaderResourceResidency または fragmentStoresAndAtomics が無い。フィードバック無しで描く）");
        }
        if (!capabilities.bTextureCompressionBC)
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
        if (!CreateProbeResources(resources, shaderManager))
        {
            return 1;
        }

        {
            // 先に宣言したものが後に破棄される（ページを返す前にテクスチャを破棄する）。
            SparsePagePool pool(device, 16 * Page);
            VariableArray<SparsePagePool::PageLease> leases;

            TextureDesc desc;
            desc.Width = 1024;
            desc.Height = 1024;
            desc.MipLevels = 11;
            desc.TextureFormat = Format::BC7_UNORM;
            desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            desc.bSparse = true;
            desc.DebugName = "VirtualTextureFeedbackTexture";
            TexturePtr texture = device->CreateTexture(desc);
            Expect(texture != nullptr, "sparse の BC7 テクスチャを作れなければならない");
            if (texture == nullptr)
            {
                return 1;
            }

            SparseTextureInfo info;
            Expect(texture->GetSparseInfo(info), "GetSparseInfo が成功しなければならない");
            Expect(info.TileWidth == TileSize && info.TileHeight == TileSize, "BC7 のタイルは 256x256");
            Expect(info.MipTailFirstLevel >= 3, "ミップ 1・2 はミップテイルの外（タイルで結ぶ）");
            Expect(info.TilesX[0] == 4 && info.TilesX[1] == 2 && info.TilesX[2] == 1,
                   "ミップ 0 は 4x4 枚、ミップ 1 は 2x2 枚、ミップ 2 は 1 枚のタイル");
            const uint32_t tailPages = static_cast<uint32_t>((info.MipTailSize + Page - 1) / Page);

            // ---- 結ぶ: ミップ 1 の 2x2 枚のタイル・ミップ 2 の 1 枚のタイル・ミップテイル（ミップ 0 は結ばない） ----
            // lod 1.58 の標本は粗い側のミップ 2 も読むので、ミップ 2 も結ぶ（結ばないと常駐の領域が非常駐になる）。
            SparseBindRequest bindRequest;
            for (uint32_t tileY = 0; tileY < 2u; ++tileY)
            {
                for (uint32_t tileX = 0; tileX < 2u; ++tileX)
                {
                    leases.push_back(pool.Acquire());
                    Expect(leases.back().IsValid(), "ミップ 1 のタイル用のページを借りられなければならない");
                    SparseTileBind tile;
                    tile.Texture = texture.get();
                    tile.MipLevel = 1;
                    tile.TileX = tileX;
                    tile.TileY = tileY;
                    tile.Page = leases.back().GetPage();
                    bindRequest.Tiles.push_back(tile);
                }
            }
            {
                leases.push_back(pool.Acquire());
                Expect(leases.back().IsValid(), "ミップ 2 のタイル用のページを借りられなければならない");
                SparseTileBind tile;
                tile.Texture = texture.get();
                tile.MipLevel = 2;
                tile.TileX = 0;
                tile.TileY = 0;
                tile.Page = leases.back().GetPage();
                bindRequest.Tiles.push_back(tile);
            }
            for (uint32_t page = 0; page < tailPages; ++page)
            {
                leases.push_back(pool.Acquire());
                Expect(leases.back().IsValid(), "ミップテイル用のページを借りられなければならない");
                SparseMipTailBind tail;
                tail.Texture = texture.get();
                tail.PageIndex = page;
                tail.Page = leases.back().GetPage();
                bindRequest.MipTails.push_back(tail);
            }
            Expect(Bind(*device, bindRequest, "ミップ1の2x2タイル・ミップ2のタイル・ミップテイル"), "結び付けが成功しなければならない");

            {
                CommandListPtr transition = device->CreateCommandList();
                transition->Begin();
                transition->TextureBarrier(texture, ResourceState::Undefined, ResourceState::ShaderResource);
                transition->End();
                transition->Submit(true);
                device->WaitIdle();
            }

            VirtualTextureFeedbackRing::Config ringConfig;
            ringConfig.Capacity = 1024u;
            VirtualTextureFeedbackRing ring(device, ringConfig);
            Expect(ring.SetEnabled(true), "要求のバッファのリングを有効にできなければならない");
            uint64_t serial = 0;
            VirtualTextureRequestSet requests;

            // ---- 位相 0〜15: 常駐の領域は位相の画素だけ、非常駐の領域は全画素が書く ----
            for (uint32_t phase = 0; phase < VirtualTextureFeedback::PhaseCount; ++phase)
            {
                const uint32_t param = VirtualTextureFeedback::PackMaterialParam(TextureIndex, TileSize, TileSize, phase);
                Expect(param != 0u, "材質のパラメータを詰められなければならない");
                if (!RunFrame(resources, ring, texture, param, serial, requests))
                {
                    return 1;
                }

                // 確認 0: 位相の画素 (phase & 3, phase >> 2) のタイル。確認 1: ミップ 1 のタイル(1, 1)。
                // 確認 2: ミップ 0 の 4 タイルすべて（4 画素ずつ）。確認 3: 同じ 4 タイルを 1・3・3・9 画素ずつ。
                // 確認 0 が(1, 1)なら確認 1 と同じ要求なので件数が 2 になる。
                const uint32_t phaseTileX = (phase & 3u) >= 2u ? 1u : 0u;
                const uint32_t phaseTileY = (phase >> 2u) >= 2u ? 1u : 0u;
                // 期待の集合は重複を除き、同じタイルの件数を足して作る（確認 0 が確認 1 と同じタイルなら 5 種類、違えば 6 種類）。
                ExpectedTile expected[6];
                uint32_t expectedCount = 0;
                const ExpectedTile candidates[6] = {{1, phaseTileX, phaseTileY, 1}, {1, 1, 1, 1}, {0, 0, 0, 5},
                                                    {0, 1, 0, 7},                   {0, 0, 1, 7}, {0, 1, 1, 13}};
                for (const ExpectedTile& candidate : candidates)
                {
                    bool bDuplicate = false;
                    for (uint32_t i = 0; i < expectedCount; ++i)
                    {
                        if (expected[i].Mip == candidate.Mip && expected[i].X == candidate.X && expected[i].Y == candidate.Y)
                        {
                            expected[i].Hits += candidate.Hits;
                            bDuplicate = true;
                        }
                    }
                    if (!bDuplicate)
                    {
                        expected[expectedCount++] = candidate;
                    }
                }
                std::cout << TestName << " phase=" << phase << " tiles=" << requests.GetRequestCount()
                          << " expected=" << expectedCount << std::endl;
                ExpectExactly("位相", requests, TextureIndex, expected, expectedCount);

                // 面積の大きいタイルほど件数が多い（読み込みの優先度が画面上の大きさの順になる）
                uint32_t hitsByTile[2][2] = {};
                for (const VirtualTextureTileRequest& request : requests.GetRequests(TextureIndex))
                {
                    if (request.Mip == 0u && request.X < 2u && request.Y < 2u)
                    {
                        hitsByTile[request.Y][request.X] = request.HitCount;
                    }
                }
                Expect(hitsByTile[0][0] < hitsByTile[0][1] && hitsByTile[0][1] == hitsByTile[1][0] &&
                           hitsByTile[1][0] < hitsByTile[1][1],
                       "面積が 1・3・3・9 のタイルの件数は面積の順（小さいタイルほど少ない）でなければならない");
            }

            // ---- float の UBO 経由（GBuffer・MegaGeometry の経路）: 24bit のパラメータが float で変わらず戻る ----
            // 番号 + 1 が 2^11 以上でパラメータが 2^23 を超え、幅の log2 が奇数（タイル幅 128）なので奇数になる。
            // 丸めが入ると幅の log2 がずれて、タイルの x が変わる。タイルの大きさは確認用の値（実際の 256 texel とは別）。
            {
                constexpr uint32_t HighTextureIndex = 2047u;
                const uint32_t param = VirtualTextureFeedback::PackMaterialParam(HighTextureIndex, 128u, 256u, 0u);
                Expect(param > (1u << 23) && (param & 1u) == 1u, "float 経由の確認のパラメータは 2^23 を超える奇数でなければならない");
                if (!RunFrame(resources, ring, texture, param, serial, requests, true))
                {
                    return 1;
                }
                // 確認 0: 位相 0 の画素の標本はミップ 1 の texel (253, 253) でタイル(1, 0)。確認 1: texel (400, 400) でタイル(3, 1)。
                // 確認 2: 非常駐でミップ 0 の標本の texel (253|255|256|258) が全画素で書き、タイル x は 1・1・2・2、y は 0・0・1・1（4 画素ずつ）。
                // 確認 3: texel (255|256|258|259) で、タイル x は 1・2・2・2、y は 0・1・1・1（1・3・3・9 画素）。
                const ExpectedTile expected[6] = {{1, 1, 0, 1}, {1, 3, 1, 1}, {0, 1, 0, 5},
                                                  {0, 2, 0, 7}, {0, 1, 1, 7}, {0, 2, 1, 13}};
                ExpectExactly("float 経由", requests, HighTextureIndex, expected, 6u);
            }

            // ---- パラメータ 0（VT でない材質）は何も書かない ----
            if (!RunFrame(resources, ring, texture, 0u, serial, requests))
            {
                return 1;
            }
            Expect(requests.IsEmpty(), "パラメータ 0 では要求を書いてはならない");

            // ---- 件数が capacity を超えたとき、溢れた件数が残る（capacity はバッファの語数から求める） ----
            {
                VirtualTextureFeedbackRing::Config smallConfig;
                smallConfig.Capacity = 4u;
                VirtualTextureFeedbackRing smallRing(device, smallConfig);
                Expect(smallRing.SetEnabled(true), "小さなリングを有効にできなければならない");
                uint64_t smallSerial = 0;
                const uint32_t param = VirtualTextureFeedback::PackMaterialParam(TextureIndex, TileSize, TileSize, 0u);
                if (!RunFrame(resources, smallRing, texture, param, smallSerial, requests))
                {
                    return 1;
                }
                // 位相 0 の要求は 6 種類（確認 0 は(0, 0)、確認 1 は(1, 1)、確認 2 は 4 件）。4 件だけ入り、2 件が溢れる。
                std::cout << TestName << " overflow tiles=" << requests.GetRequestCount()
                          << " overflow=" << requests.GetOverflowCount() << std::endl;
                Expect(requests.GetRequestCount() == 4u, "capacity 4 のバッファには 4 件だけ入る");
                Expect(requests.GetOverflowCount() == 2u, "6 種類の要求のうち capacity を超えた 2 件が溢れた件数に残る");
                device->WaitIdle();
            }

            // ---- 後片付け: 全部外して完了を待ってから、ページを返す（先にテクスチャを破棄する） ----
            SparseBindRequest releaseAll;
            for (uint32_t tileY = 0; tileY < 2u; ++tileY)
            {
                for (uint32_t tileX = 0; tileX < 2u; ++tileX)
                {
                    SparseTileBind tile;
                    tile.Texture = texture.get();
                    tile.MipLevel = 1;
                    tile.TileX = tileX;
                    tile.TileY = tileY;
                    releaseAll.Tiles.push_back(tile);
                }
            }
            {
                SparseTileBind tile;
                tile.Texture = texture.get();
                tile.MipLevel = 2;
                tile.TileX = 0;
                tile.TileY = 0;
                releaseAll.Tiles.push_back(tile);
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
