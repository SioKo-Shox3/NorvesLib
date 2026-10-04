// sparse（部分常駐）テクスチャへの、物理ページの結び付け・外しの GPU テスト。
// SparsePagePool から借りたページを、タイル（ミップ・x・y）とミップテイルへ vkQueueBindSparse 1回でまとめて結び、
// 結んだタイルへ書いた BC7 の色を、計算シェーダーのサンプル（texelFetch）で読み戻して期待と比べる。
// 結び付けから描画へはセマフォで順序付けられるので、結び付けの直後の提出がそのまま結んだタイルを読める。
// 外したページは GpuRetireQueue を通って、最後に使った提出の serial が完了してからプールの空きへ戻る。
// 不正な要求（範囲外のタイル・ミップテイルのミップ・sparse でないテクスチャ）は何も変えずに断られる。
// Vulkan デバイスが無い、sparse の結び付けや BC7 が使えない環境では 125（スキップ）を返す。
#include "Rendering/GpuRetireQueue.h"
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

#include <algorithm>
#include <chrono>
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

    constexpr const char* TestName = "SparseBindVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ProbeCount = 5u;
    constexpr uint32_t ProbeBufferBytes = 8u * 4u * sizeof(float);
    // 期待の色との許容差（2/255）
    constexpr float Tolerance = 2.0f / 255.0f;
    // 1回の vkQueueBindSparse の許容（これを超えたら記録する。テストの合否には使わない）
    constexpr double BindMsLimit = 2.0;

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

    // ---- BC7 の手組みブロック ----

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

    // タイル（幅・高さ blocksPerRow 個のブロック）を1色で埋め、ブロック (0,0)(1,0)(0,1)(1,1) だけ指定の色にする。
    void FillTile(uint8_t* dst, uint32_t blocksPerRow, const uint8_t filler[4], const uint8_t corners[4][4])
    {
        for (uint32_t block = 0; block < blocksPerRow * blocksPerRow; ++block)
        {
            PackBC7Block(dst + block * 16u, filler);
        }
        for (uint32_t corner = 0; corner < 4u; ++corner)
        {
            const uint32_t bx = corner & 1u;
            const uint32_t by = corner >> 1u;
            PackBC7Block(dst + (by * blocksPerRow + bx) * 16u, corners[corner]);
        }
    }

    // ---- 読み戻し（texture_mip_probe.comp） ----

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

    // ミップ0の (1,1)(5,1)(1,5)(5,5) とミップ1の (2,2) の色を読み戻す。
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

    // ---- 結び付け ----

    struct BindTiming
    {
        double TotalMs = 0.0;
        double MaxMs = 0.0;
        uint32_t Count = 0;
    };
    BindTiming g_bindTiming;

    // BindSparse の所要時間を計って記録する
    bool TimedBindSparse(IDevice& device, const SparseBindRequest& request, const char* label)
    {
        const auto begin = std::chrono::steady_clock::now();
        const bool bOk = device.BindSparse(request);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        g_bindTiming.TotalMs += ms;
        g_bindTiming.MaxMs = std::max(g_bindTiming.MaxMs, ms);
        ++g_bindTiming.Count;
        std::cout << TestName << " bind_ms=" << ms << " label=" << label << " tiles=" << request.Tiles.size()
                  << " tailPages=" << request.MipTails.size() << (ms > BindMsLimit ? " 2ms超過" : "") << std::endl;
        return bOk;
    }

    struct CaseParams
    {
        const char* Name;
        uint32_t Size;
        uint32_t MipLevels;
        // 期待するミップテイルの開始段（標準のブロック形状: タイルに満たない最初のミップ）
        uint32_t ExpectedTailFirst;
        // ミップ1のデータを置くミップ（タイルなら 1、ミップテイルの中なら 1）と、その大きさ（texel）
        uint32_t Mip1Size;
    };

    // 1つのテクスチャについて、結び付け・書き込み・読み戻し・外しを通しで確かめる。
    bool RunCase(IDevice& device, ProbeResources& resources, SparsePagePool& pool, GpuRetireQueue& queue,
                 const CaseParams& params, uint64_t& inOutSerial)
    {
        std::cout << TestName << " --- " << params.Name << " ---" << std::endl;
        const uint32_t startFailures = static_cast<uint32_t>(g_failures);
        constexpr uint64_t Page = RHI::SparsePageSizeBytes;

        TextureDesc desc;
        desc.Width = params.Size;
        desc.Height = params.Size;
        desc.MipLevels = params.MipLevels;
        desc.TextureFormat = Format::BC7_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.bSparse = true;
        desc.DebugName = params.Name;

        // 先に宣言したものが後に破棄される（ページを返す前にテクスチャを破棄する）。
        VariableArray<SparsePagePool::PageLease> tailLeases;
        SparsePagePool::PageLease tileLeaseA;
        SparsePagePool::PageLease tileLeaseB;
        TexturePtr texture = device.CreateTexture(desc);
        Expect(texture != nullptr, "sparse の BC7 テクスチャを作れなければならない");
        if (texture == nullptr)
        {
            return false;
        }

        SparseTextureInfo info;
        Expect(texture->GetSparseInfo(info), "GetSparseInfo が成功しなければならない");
        Expect(info.TileWidth == 256 && info.TileHeight == 256 && info.TileSizeBytes == Page, "BC7 のタイルは 256x256・64 KiB");
        Expect(info.MipTailFirstLevel == params.ExpectedTailFirst, "ミップテイルの開始段が期待と違う");
        const uint32_t tailPages = static_cast<uint32_t>((info.MipTailSize + Page - 1) / Page);
        Expect(tailPages >= 1, "ミップテイルが 1 ページ以上ある");
        Expect(texture->GetSparseBoundBytes() == 0, "作成直後は何も結んでいない");

        // ---- 結ぶ: ミップ0のタイル(0,0)・(ミップテイルの前なら)ミップ1のタイル(0,0)・ミップテイル全部 ----
        const bool bMip1IsTile = info.MipTailFirstLevel > 1;
        tileLeaseA = pool.Acquire();
        Expect(tileLeaseA.IsValid(), "ミップ0のタイル用のページを借りられなければならない");
        SparseBindRequest bindRequest;
        {
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 0;
            tile.TileX = 0;
            tile.TileY = 0;
            tile.Page = tileLeaseA.GetPage();
            bindRequest.Tiles.push_back(tile);
        }
        if (bMip1IsTile)
        {
            tileLeaseB = pool.Acquire();
            Expect(tileLeaseB.IsValid(), "ミップ1のタイル用のページを借りられなければならない");
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 1;
            tile.TileX = 0;
            tile.TileY = 0;
            tile.Page = tileLeaseB.GetPage();
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

        const uint64_t expectedBound = (bMip1IsTile ? 2u : 1u) * Page + static_cast<uint64_t>(tailPages) * Page;
        Expect(TimedBindSparse(device, bindRequest, "結ぶ"), "ページの結び付けが成功しなければならない");
        Expect(texture->GetSparseBoundBytes() == expectedBound, "結んだ量がタイルとミップテイルのページ数の合計と一致しなければならない");
        Expect(pool.GetStats().UsedBytes >= expectedBound, "プールの貸し出し量が結んだ量以上でなければならない");

        // 同じ要求をもう一度出しても、結んだ量は増えない（状態は結び付けごとに1度だけ数える）。
        Expect(TimedBindSparse(device, bindRequest, "同じ結び付けの再提出"), "同じ結び付けの再提出が成功しなければならない");
        Expect(texture->GetSparseBoundBytes() == expectedBound, "同じ結び付けを再提出しても結んだ量は変わらない");

        // ---- 書く: ミップ0のタイルの4つのブロックの色と、ミップ1のデータ ----
        const uint8_t filler[4] = {10u, 20u, 30u, 255u};
        const uint8_t corners[4][4] = {{255u, 0u, 0u, 255u}, {0u, 255u, 0u, 128u}, {0u, 0u, 255u, 64u}, {90u, 170u, 50u, 255u}};
        const uint8_t mip1Color[4] = {200u, 10u, 120u, 32u};
        const uint8_t mip1Filler[4] = {1u, 2u, 3u, 255u};
        const uint8_t mip1Corners[4][4] = {{200u, 10u, 120u, 32u}, {5u, 5u, 5u, 255u}, {6u, 6u, 6u, 255u}, {7u, 7u, 7u, 255u}};
        (void)mip1Color;

        const uint32_t mip1BlocksPerRow = params.Mip1Size / 4u;
        const uint64_t mip1Bytes = static_cast<uint64_t>(mip1BlocksPerRow) * mip1BlocksPerRow * 16u;
        const uint64_t mip1Offset = Page;
        const uint64_t stagingBytes = mip1Offset + mip1Bytes;
        VariableArray<uint8_t> staging;
        staging.resize(static_cast<size_t>(stagingBytes));
        FillTile(staging.data(), 64u, filler, corners);
        FillTile(staging.data() + mip1Offset, mip1BlocksPerRow, mip1Filler, mip1Corners);

        BufferPtr stagingBuffer = device.CreateBuffer(BufferDesc(stagingBytes, ResourceUsage::TransferSrc, true, "SparseBindStaging"));
        Expect(stagingBuffer != nullptr, "ステージングのバッファを作れなければならない");
        if (stagingBuffer == nullptr)
        {
            return false;
        }
        stagingBuffer->Update(staging.data(), stagingBytes, 0u);

        // 結び付けの直後の提出がそのまま結んだタイルへ書ける（結び付けから提出へはセマフォで順序付けられる）。
        CommandListPtr upload = device.CreateCommandList();
        upload->Begin();
        upload->TextureBarrier(texture, ResourceState::Undefined, ResourceState::CopyDest);
        upload->CopyBufferToTexture(stagingBuffer, texture, 256u, 256u, 0u, 0u);
        upload->CopyBufferToTexture(stagingBuffer, texture, params.Mip1Size, params.Mip1Size, mip1Offset, 1u);
        upload->TextureBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderResource);
        upload->End();
        upload->Submit(true);
        device.WaitIdle();

        // ---- 読み戻す ----
        float expected[ProbeCount][4];
        ExpectedBC7Color(corners[0], expected[0]);
        ExpectedBC7Color(corners[1], expected[1]);
        ExpectedBC7Color(corners[2], expected[2]);
        ExpectedBC7Color(corners[3], expected[3]);
        ExpectedBC7Color(mip1Corners[0], expected[4]);

        float colors[ProbeCount][4] = {};
        const bool bRead = ReadProbeColors(resources, texture, colors);
        Expect(bRead, "計算シェーダーで読み戻せなければならない");
        if (bRead)
        {
            for (uint32_t probe = 0; probe < ProbeCount; ++probe)
            {
                float maxDifference = 0.0f;
                for (uint32_t channel = 0; channel < 4u; ++channel)
                {
                    maxDifference = std::fmax(maxDifference, std::fabs(colors[probe][channel] - expected[probe][channel]));
                }
                std::cout << TestName << " " << params.Name << " probe=" << probe << " readback=(" << colors[probe][0]
                          << ", " << colors[probe][1] << ", " << colors[probe][2] << ", " << colors[probe][3]
                          << ") expected=(" << expected[probe][0] << ", " << expected[probe][1] << ", "
                          << expected[probe][2] << ", " << expected[probe][3] << ") max_diff=" << maxDifference << std::endl;
                Expect(maxDifference <= Tolerance, "結んだタイル・ミップテイルへ書いた色を読み戻せなければならない");
            }
        }

        // ---- 外す: ミップ0のタイルを外し、ページは GpuRetireQueue 経由でプールの空きへ戻す ----
        const uint64_t usedBeforeUnbind = pool.GetStats().UsedBytes;
        SparseBindRequest unbindRequest;
        {
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 0;
            tile.TileX = 0;
            tile.TileY = 0;
            unbindRequest.Tiles.push_back(tile);
        }
        Expect(TimedBindSparse(device, unbindRequest, "外す"), "タイルを外せなければならない");
        Expect(texture->GetSparseBoundBytes() == expectedBound - Page, "外した分だけ結んだ量が減らなければならない");

        // 外したページは、最後に使った提出の serial（ここでは直前の提出）が完了してから戻る。
        queue.BeginFrame(inOutSerial);
        queue.CommitFrame(inOutSerial + 1u);
        ++inOutSerial;
        queue.Retire(std::move(tileLeaseA));
        Expect(pool.GetStats().UsedBytes == usedBeforeUnbind, "最後に使った提出が完了するまでページはプールへ戻らない");
        queue.Collect(inOutSerial - 1u);
        Expect(pool.GetStats().UsedBytes == usedBeforeUnbind, "完了の serial が届かない間はページが戻らない");
        queue.Collect(inOutSerial);
        Expect(pool.GetStats().UsedBytes == usedBeforeUnbind - Page, "完了の serial が届いたらページがプールの空きへ戻る");

        // 外した後の空きを別のタイルへ結べる（同じページでも、物理メモリは何にも結ばれていない状態に戻っている）。
        // ミップ0が複数のタイルを持つなら隣のタイル (1,1)、1枚だけなら外したタイル (0,0) に結び直す。
        const uint32_t reboundTile = info.TilesX[0] > 1 ? 1u : 0u;
        SparsePagePool::PageLease reboundLease = pool.Acquire();
        SparseBindRequest rebindRequest;
        {
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 0;
            tile.TileX = reboundTile;
            tile.TileY = reboundTile;
            tile.Page = reboundLease.GetPage();
            rebindRequest.Tiles.push_back(tile);
        }
        Expect(TimedBindSparse(device, rebindRequest, "別のタイルへ結び直す"), "戻ったページを別のタイルへ結べなければならない");
        Expect(texture->GetSparseBoundBytes() == expectedBound, "結び直した分だけ結んだ量が増える");

        // 後片付け: 全部外してからページを返す（外していない間はテクスチャが結んでいる）。
        SparseBindRequest releaseAll;
        {
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 0;
            tile.TileX = reboundTile;
            tile.TileY = reboundTile;
            releaseAll.Tiles.push_back(tile);
        }
        if (bMip1IsTile)
        {
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 1;
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
        Expect(TimedBindSparse(device, releaseAll, "全部外す"), "全部外せなければならない");
        Expect(texture->GetSparseBoundBytes() == 0, "全部外すと結んだ量が 0 になる");
        device.WaitIdle();

        queue.BeginFrame(inOutSerial);
        queue.CommitFrame(inOutSerial + 1u);
        ++inOutSerial;
        queue.Retire(std::move(reboundLease));
        queue.Retire(std::move(tileLeaseB));
        for (SparsePagePool::PageLease& lease : tailLeases)
        {
            queue.Retire(std::move(lease));
        }
        tailLeases.clear();
        queue.Collect(inOutSerial);
        Expect(pool.GetStats().UsedBytes == 0, "全部外して完了を待つと、プールの貸し出しが 0 に戻る");

        texture.reset();
        return static_cast<uint32_t>(g_failures) == startFailures;
    }

    // 不正な結び付けの要求は、何も変えずに断られる（エラーはログに出る）。
    void TestRejections(IDevice& device, SparsePagePool& pool)
    {
        std::cout << TestName << " --- 不正な要求 ---（以下のエラーログは想定内）" << std::endl;
        constexpr uint64_t Page = RHI::SparsePageSizeBytes;

        TextureDesc desc;
        desc.Width = 512;
        desc.Height = 512;
        desc.MipLevels = 10;
        desc.TextureFormat = Format::BC7_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.bSparse = true;
        desc.DebugName = "SparseBindRejectionTarget";
        SparsePagePool::PageLease lease = pool.Acquire();
        TexturePtr texture = device.CreateTexture(desc);
        if (texture == nullptr || !lease.IsValid())
        {
            Expect(false, "拒否のテストの準備（テクスチャとページ）に失敗");
            return;
        }
        SparseTextureInfo info;
        texture->GetSparseInfo(info);

        // 範囲外のタイル
        {
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = 0;
            tile.TileX = info.TilesX[0];
            tile.TileY = 0;
            tile.Page = lease.GetPage();
            request.Tiles.push_back(tile);
            Expect(!device.BindSparse(request), "範囲外のタイルの結び付けは断られなければならない");
        }
        // ミップテイルのミップへのタイル指定
        {
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.MipLevel = info.MipTailFirstLevel;
            tile.Page = lease.GetPage();
            request.Tiles.push_back(tile);
            Expect(!device.BindSparse(request), "ミップテイルのミップへのタイル指定は断られなければならない");
        }
        // ミップテイルの範囲外のページ
        {
            SparseBindRequest request;
            SparseMipTailBind tail;
            tail.Texture = texture.get();
            tail.PageIndex = static_cast<uint32_t>((info.MipTailSize + Page - 1) / Page);
            tail.Page = lease.GetPage();
            request.MipTails.push_back(tail);
            Expect(!device.BindSparse(request), "ミップテイルの範囲外のページは断られなければならない");
        }
        // 位置が 64 KiB の倍数でないページ
        {
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.Page = lease.GetPage();
            tile.Page.OffsetBytes += 4096;
            request.Tiles.push_back(tile);
            Expect(!device.BindSparse(request), "64 KiB の倍数でない位置のページは断られなければならない");
        }
        // 塊の外へはみ出すページ
        {
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.Page = lease.GetPage();
            tile.Page.OffsetBytes = tile.Page.Block->GetSizeBytes();
            request.Tiles.push_back(tile);
            Expect(!device.BindSparse(request), "塊の外へはみ出すページは断られなければならない");
        }
        // 位置の加算がオーバーフローして 0 に戻る、塊の外のページ
        {
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = texture.get();
            tile.Page = lease.GetPage();
            tile.Page.OffsetBytes = 0xffffffffffff0000ull;
            request.Tiles.push_back(tile);
            Expect(!device.BindSparse(request), "加算がオーバーフローして範囲内に見える位置のページは断られなければならない");
        }
        // sparse でないテクスチャ
        {
            TextureDesc plainDesc;
            plainDesc.Width = 64;
            plainDesc.Height = 64;
            plainDesc.DebugName = "SparseBindRejectionPlain";
            TexturePtr plain = device.CreateTexture(plainDesc);
            SparseBindRequest request;
            SparseTileBind tile;
            tile.Texture = plain.get();
            tile.Page = lease.GetPage();
            request.Tiles.push_back(tile);
            Expect(plain != nullptr && !device.BindSparse(request), "sparse でないテクスチャへの結び付けは断られなければならない");
        }
        // 1つでも不正なら、有効な要求も含めて何も結ばない
        {
            SparseBindRequest request;
            SparseTileBind good;
            good.Texture = texture.get();
            good.Page = lease.GetPage();
            request.Tiles.push_back(good);
            SparseTileBind bad = good;
            bad.TileX = 99;
            request.Tiles.push_back(bad);
            Expect(!device.BindSparse(request), "不正な要求を含む結び付けは断られなければならない");
        }
        Expect(texture->GetSparseBoundBytes() == 0, "断られた結び付けでは結んだ量が変わらない");
        // 空の要求は何もせず成功する
        Expect(device.BindSparse(SparseBindRequest{}), "空の結び付けは成功しなければならない");
        device.WaitIdle();
        texture.reset();
    }

    // 結び付けと描画側の提出を、完了を待たずに交互に重ねて出す。
    // セマフォの通知が、待つ側の消費より前に重なっても validation error が出ないこと（結び付けの待ちが
    // 消費されない順序付け）を確かめる。
    void TestOverlappedSubmits(IDevice& device, SparsePagePool& pool, GpuRetireQueue& queue, uint64_t& inOutSerial)
    {
        std::cout << TestName << " --- 完了を待たない連続提出 ---" << std::endl;
        constexpr uint32_t TileCount = 4u;
        constexpr uint32_t RoundCount = 24u;

        TextureDesc desc;
        desc.Width = 512;
        desc.Height = 512;
        desc.MipLevels = 10;
        desc.TextureFormat = Format::BC7_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.bSparse = true;
        desc.DebugName = "SparseBindOverlappedTarget";
        TexturePtr texture = device.CreateTexture(desc);
        SparseTextureInfo info;
        if (texture == nullptr || !texture->GetSparseInfo(info) || info.TilesX[0] * info.TilesY[0] < TileCount)
        {
            Expect(false, "連続提出のテストの準備（テクスチャ）に失敗");
            return;
        }

        VariableArray<SparsePagePool::PageLease> leases;
        for (uint32_t tile = 0; tile < TileCount; ++tile)
        {
            leases.push_back(pool.Acquire());
            if (!leases.back().IsValid())
            {
                Expect(false, "連続提出のテストの準備（ページ）に失敗");
                return;
            }
        }

        // 結び付け(A) → 提出(待たない) → 結び付け(B) → 提出(待たない) … を WaitIdle なしで繰り返す
        VariableArray<CommandListPtr> commandLists;
        bool bAllBound = true;
        bool bAllSubmitted = true;
        for (uint32_t round = 0; round < RoundCount; ++round)
        {
            const uint32_t tile = round % TileCount;
            SparseBindRequest request;
            SparseTileBind bind;
            bind.Texture = texture.get();
            bind.MipLevel = 0;
            bind.TileX = tile % info.TilesX[0];
            bind.TileY = tile / info.TilesX[0];
            bind.Page = leases[tile].GetPage();
            request.Tiles.push_back(bind);
            bAllBound = TimedBindSparse(device, request, "連続提出の結び付け") && bAllBound;

            CommandListPtr commandList = device.CreateCommandList();
            if (!commandList)
            {
                bAllSubmitted = false;
                break;
            }
            commandList->Begin();
            commandList->End();
            commandList->Submit(false);
            commandLists.push_back(std::move(commandList));
        }
        device.WaitIdle();
        Expect(bAllBound, "連続提出の結び付けがすべて成功しなければならない");
        Expect(bAllSubmitted, "連続提出の描画側の提出がすべて成功しなければならない");
        commandLists.clear();

        // 全部外して、完了を待ってからページを戻す
        SparseBindRequest release;
        for (uint32_t tile = 0; tile < TileCount; ++tile)
        {
            SparseTileBind unbind;
            unbind.Texture = texture.get();
            unbind.MipLevel = 0;
            unbind.TileX = tile % info.TilesX[0];
            unbind.TileY = tile / info.TilesX[0];
            release.Tiles.push_back(unbind);
        }
        Expect(TimedBindSparse(device, release, "連続提出のあとに全部外す"), "連続提出のあとに外せなければならない");
        device.WaitIdle();
        queue.BeginFrame(inOutSerial);
        queue.CommitFrame(inOutSerial + 1u);
        ++inOutSerial;
        for (SparsePagePool::PageLease& lease : leases)
        {
            queue.Retire(std::move(lease));
        }
        leases.clear();
        queue.Collect(inOutSerial);
        Expect(pool.GetStats().UsedBytes == 0, "連続提出のあとに外して完了を待つと、プールの貸し出しが 0 に戻る");
        texture.reset();

        // 台帳（VRAM_LEDGER）は、貸し出しが変わったときだけ出し直す（貸し借りのたびには出さない）
        pool.LogLedgerIfChanged();
        Expect(!pool.LogLedgerIfChanged(), "変わっていなければ台帳を出し直さない");
        SparsePagePool::PageLease ledgerLease = pool.Acquire();
        Expect(ledgerLease.IsValid() && pool.LogLedgerIfChanged(), "ページを借りたあとは台帳を出し直す");
        Expect(!pool.LogLedgerIfChanged(), "出し直した直後は、変わるまで出さない");
        ledgerLease.Reset();
        Expect(pool.LogLedgerIfChanged(), "ページを返したあとは台帳を出し直す");
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
            ShaderPtr shader = shaderManager.LoadShader("texture_mip_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << "texture_mip_probe.comp を読み込めませんでした\n";
                return 1;
            }
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeProbeDescriptorSetDesc());
            resources.Pipeline = device->CreateComputePipeline(pipelineDesc);

            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            resources.Sampler = device->CreateSampler(samplerDesc);

            BufferDesc resultDesc;
            resultDesc.Size = ProbeBufferBytes;
            resultDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
            resultDesc.DebugName = "SparseBindProbeResults";
            resources.ResultBuffer = device->CreateBuffer(resultDesc);
            resources.ReadbackBuffer = device->CreateBuffer(
                BufferDesc(ProbeBufferBytes, ResourceUsage::TransferDst, true, "SparseBindProbeReadback"));
        }
        if (!resources.Pipeline || !resources.Sampler || !resources.ResultBuffer || !resources.ReadbackBuffer)
        {
            std::cerr << "確認用の資源を作れませんでした\n";
            return 1;
        }

        // 1つの塊を 2 ページにして、複数の塊にまたがって結べることも確かめる（1つ目のケースだけで 3 ページ要る）。
        constexpr uint64_t BlockBytes = 2 * RHI::SparsePageSizeBytes;
        {
            SparsePagePool pool(device, BlockBytes);
            GpuRetireQueue queue;
            uint64_t serial = 0;

            // 512x512 の BC7（10段）: ミップ0は 2x2 枚、ミップ1は 1 枚、ミップ2からがミップテイル。
            // タイル2つ（ミップ0・ミップ1）とミップテイルを結ぶ。
            const CaseParams tiledCase{"512x512タイル2つとミップテイル", 512, 10, 2, 256};
            // 256x256 の BC7（9段）: ミップ0が 1 枚、ミップ1（128x128）からがミップテイル。
            // タイル1つとミップテイルを結び、ミップテイルの中のミップ1を読む。
            const CaseParams tailCase{"256x256タイル1つとミップテイル", 256, 9, 1, 128};

            const bool bTiled = RunCase(*device, resources, pool, queue, tiledCase, serial);
            std::cout << tiledCase.Name << (bTiled ? " PASS" : " FAIL") << '\n';
            const bool bTail = RunCase(*device, resources, pool, queue, tailCase, serial);
            std::cout << tailCase.Name << (bTail ? " PASS" : " FAIL") << '\n';
            TestRejections(*device, pool);
            TestOverlappedSubmits(*device, pool, queue, serial);

            device->WaitIdle();
            queue.Clear();
            const SparsePagePool::Stats stats = pool.GetStats();
            std::cout << TestName << " pool blocks=" << stats.BlockCount << " capacity=" << stats.CapacityBytes
                      << " used=" << stats.UsedBytes << std::endl;
            Expect(stats.UsedBytes == 0, "テストの最後にプールの貸し出しが残っていない");
            Expect(stats.BlockCount >= 2, "1つの塊（2ページ）では足りず、複数の塊にまたがって結べた");
        }

        device->WaitIdle();
        resources = ProbeResources{};
        shaderManager.Shutdown();

        std::cout << TestName << " bind_count=" << g_bindTiming.Count << " bind_total_ms=" << g_bindTiming.TotalMs
                  << " bind_max_ms=" << g_bindTiming.MaxMs << " limit_ms=" << BindMsLimit << std::endl;

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
