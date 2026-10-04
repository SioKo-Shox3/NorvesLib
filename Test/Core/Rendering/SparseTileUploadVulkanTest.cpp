// sparse テクスチャのタイル・ミップテイルへ、ステージングのリング経由で GPU を待たずに書く経路（TileUploader）の GPU テスト。
// 連続する多数のフレームで、ミップ0・ミップ1のタイル（原点でない位置を含む）とミップテイルの中のミップへ書き、
// 同じフレームのコマンドで領域ごとに読み戻して、書いたバイト列と一致することを確かめる。
// その間に vkDeviceWaitIdle を一度も呼ばない（呼んだ回数をデバイスの計数で確かめる）。コマンドリストは3本を使い回し、
// 古い提出の完了はコマンドリストのフェンス（Begin）だけで待つ。
// リングの区画は、コピーを含む提出の serial が完了するまで再利用しない。1フレームにコピーする量には上限がある。
// フレームを提出しなかったときは、記録したコピーを次のフレームで出し直す。
// Vulkan デバイスが無い、sparse の結び付けや BC7 が使えない環境では 125（スキップ）を返す。
#include "Rendering/SparsePagePool.h"
#include "Rendering/TileUploader.h"

#include "Container/Containers.h"
#include "Container/Deque.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
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
uint64_t GetVulkanDeviceWaitIdleCallCountForTesting() noexcept;
uint32_t GetVulkanBufferMemoryPropertyFlagsForTesting(const IBuffer* buffer) noexcept;
int64_t GetVulkanLiveBufferCountForTesting() noexcept;
int64_t GetVulkanLiveBufferMemoryCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "SparseTileUploadVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t TextureSize = 1024u;
    constexpr uint32_t SlotCount = 3u;
    // フレームごとに作るコピーの数（1フレームの上限より多くして、持ち越しと詰まりを起こす）
    constexpr uint32_t NewCopiesPerFrame = 3u;
    constexpr uint32_t GeneratingFrames = 30u;
    constexpr uint32_t MaxFrames = 200u;
    constexpr uint64_t Page = RHI::SparsePageSizeBytes;
    // テスト用のリングは 4 タイル分、1フレームの上限は 2 タイル分にする（既定は 32 MiB・24 MiB）
    constexpr uint64_t TestRingBytes = 4u * Page;
    constexpr uint64_t TestFrameLimitBytes = 2u * Page;
    constexpr uint64_t SlotReadbackBytes = 4u * Page;
    // VkMemoryPropertyFlagBits の値（Vulkan のヘッダに依存しないテストのため）
    constexpr uint32_t VkMemoryDeviceLocalBit = 0x1u;
    constexpr uint32_t VkMemoryHostVisibleBit = 0x2u;
    constexpr uint32_t VkMemoryHostCoherentBit = 0x4u;

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

    uint64_t AlignUp256(uint64_t value)
    {
        return (value + 255u) / 256u * 256u;
    }

    // BC7 のバイト列として意味のある値である必要はない（バッファ→イメージ→バッファはブロックをそのまま運ぶ）。
    VariableArray<uint8_t> MakeData(uint64_t seed, uint64_t bytes)
    {
        VariableArray<uint8_t> data;
        data.resize(static_cast<size_t>(bytes));
        uint64_t state = seed * 0x9E3779B97F4A7C15ull + 0x1234567ull;
        for (uint64_t index = 0; index < bytes; ++index)
        {
            state ^= state << 13u;
            state ^= state >> 7u;
            state ^= state << 17u;
            data[static_cast<size_t>(index)] = static_cast<uint8_t>(state >> 24u);
        }
        return data;
    }

    // BC7 の矩形のバイト数（4x4 ブロック 16 バイト）
    uint64_t BC7Bytes(uint32_t width, uint32_t height)
    {
        return static_cast<uint64_t>((width + 3u) / 4u) * ((height + 3u) / 4u) * 16u;
    }

    struct RegionSpec
    {
        TextureRegionCopy Region;
        uint64_t Bytes = 0;
        const char* Name = "";
    };

    struct Planned
    {
        uint32_t RegionIndex = 0;
        VariableArray<uint8_t> Data;
    };

    struct VerifyItem
    {
        uint32_t RegionIndex = 0;
        uint64_t Offset = 0;
        VariableArray<uint8_t> Expected;
    };

    struct Harness
    {
        DevicePtr Device;
        TexturePtr Texture;
        VariableArray<RegionSpec> Regions;
        BufferPtr Readback[SlotCount];
        VariableArray<VerifyItem> SlotVerify[SlotCount];
        // 領域ごとの、最後に書いたはずの内容
        VariableArray<VariableArray<uint8_t>> Final;
        uint32_t VerifiedCount = 0;
    };

    // 読み戻したバッファを、記録したときの期待と比べる
    void VerifySlot(Harness& harness, uint32_t slot)
    {
        VariableArray<VerifyItem>& items = harness.SlotVerify[slot];
        if (items.empty())
        {
            return;
        }
        const uint8_t* mapped = static_cast<const uint8_t*>(harness.Readback[slot]->Map(0u, SlotReadbackBytes));
        Expect(mapped != nullptr, "読み戻しのバッファを写像できなければならない");
        if (mapped != nullptr)
        {
            for (const VerifyItem& item : items)
            {
                const bool bSame = std::memcmp(mapped + item.Offset, item.Expected.data(), item.Expected.size()) == 0;
                if (!bSame)
                {
                    std::cerr << TestName << " 不一致: region=" << harness.Regions[item.RegionIndex].Name << std::endl;
                }
                Expect(bSame, "書いたタイル・ミップテイルを読み戻した内容が一致しなければならない");
                ++harness.VerifiedCount;
            }
            harness.Readback[slot]->Unmap();
        }
        items.clear();
    }

    // 領域ごとに、記録済みのコピーの読み戻しを同じコマンドへ足す（コピーの記録より後）
    void RecordReadback(Harness& harness, ICommandList& commandList, uint32_t slot, Deque<Planned>& queued, uint32_t recorded)
    {
        if (recorded == 0)
        {
            return;
        }
        commandList.TextureBarrier(harness.Texture, ResourceState::ShaderResource, ResourceState::CopySource);
        uint64_t offset = 0;
        for (uint32_t index = 0; index < recorded; ++index)
        {
            Planned& planned = queued.front();
            TextureRegionCopy region = harness.Regions[planned.RegionIndex].Region;
            region.BufferOffset = offset;
            Expect(commandList.CopyTextureRegionToBuffer(harness.Texture, harness.Readback[slot], region),
                   "領域の読み戻しを記録できなければならない");
            VerifyItem item;
            item.RegionIndex = planned.RegionIndex;
            item.Offset = offset;
            item.Expected = planned.Data;
            harness.SlotVerify[slot].push_back(std::move(item));
            offset += AlignUp256(planned.Data.size());
            harness.Final[planned.RegionIndex] = std::move(planned.Data);
            queued.pop_front();
        }
        Expect(offset <= SlotReadbackBytes, "1フレームの読み戻しが読み戻しのバッファに収まらなければならない");
        commandList.TextureBarrier(harness.Texture, ResourceState::CopySource, ResourceState::ShaderResource);
        commandList.BufferBarrier(harness.Readback[slot], ResourceState::CopyDest, ResourceState::HostRead, 0u, offset);
    }

    // 連続フレームで書いて読み戻す。WaitIdle は呼ばない。
    void TestContinuousFrames(Harness& harness)
    {
        std::cout << TestName << " --- 連続フレームの書き込みと読み戻し ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = TestFrameLimitBytes;
        TileUploader uploader(harness.Device, config);

        CommandListPtr lists[SlotCount];
        for (CommandListPtr& list : lists)
        {
            list = harness.Device->CreateCommandList();
            if (!list)
            {
                Expect(false, "コマンドリストを作れなければならない");
                return;
            }
        }
        Expect(uploader.EnqueueInitialize(harness.Texture), "初期化の依頼を積めなければならない");

        Deque<Planned> waiting;
        Deque<Planned> queued;
        uint64_t generated = 0;
        uint32_t ringFullCount = 0;
        uint32_t deferredFrames = 0;
        uint32_t maxFrameCopyCount = 0;
        uint64_t totalCopiedBytes = 0;
        uint32_t frame = 0;

        const uint64_t idleBefore = RHI::Vulkan::GetVulkanDeviceWaitIdleCallCountForTesting();
        for (frame = 1; frame <= MaxFrames; ++frame)
        {
            const uint32_t slot = (frame - 1u) % SlotCount;
            CommandListPtr& commandList = lists[slot];
            // 3つ前のフレームの提出の完了をコマンドリストのフェンスで待つ（デバイス全体は待たない）
            commandList->Begin();
            VerifySlot(harness, slot);
            const uint64_t completed = frame > SlotCount ? frame - SlotCount : 0u;
            uploader.BeginFrame(completed);

            if (frame <= GeneratingFrames)
            {
                for (uint32_t copy = 0; copy < NewCopiesPerFrame; ++copy)
                {
                    Planned planned;
                    planned.RegionIndex = static_cast<uint32_t>(generated % harness.Regions.size());
                    planned.Data = MakeData(generated + 1u, harness.Regions[planned.RegionIndex].Bytes);
                    ++generated;
                    waiting.push_back(std::move(planned));
                }
            }
            while (!waiting.empty())
            {
                Planned& planned = waiting.front();
                const RegionSpec& spec = harness.Regions[planned.RegionIndex];
                if (!uploader.EnqueueTile(harness.Texture, spec.Region, planned.Data.data(), planned.Data.size()))
                {
                    ++ringFullCount;
                    break;
                }
                queued.push_back(std::move(planned));
                waiting.pop_front();
            }

            const uint32_t pendingBefore = static_cast<uint32_t>(queued.size());
            const uint32_t recorded = uploader.RecordCopies(*commandList);
            const TileUploader::Stats stats = uploader.GetStats();
            Expect(recorded <= pendingBefore, "記録した数が積んだ数を超えてはならない");
            Expect(stats.FrameCopiedBytes <= TestFrameLimitBytes, "1フレームにコピーする量は上限を超えてはならない");
            if (recorded < pendingBefore)
            {
                ++deferredFrames;
            }
            maxFrameCopyCount = std::max(maxFrameCopyCount, recorded);
            totalCopiedBytes += stats.FrameCopiedBytes;
            RecordReadback(harness, *commandList, slot, queued, recorded);
            commandList->End();
            commandList->Submit(false);
            uploader.CommitFrame(frame);

            if (frame >= GeneratingFrames && waiting.empty() && queued.empty())
            {
                break;
            }
        }
        const uint64_t idleAfter = RHI::Vulkan::GetVulkanDeviceWaitIdleCallCountForTesting();

        std::cout << TestName << " frames=" << frame << " generated=" << generated << " ring_full=" << ringFullCount
                  << " deferred_frames=" << deferredFrames << " max_copies_per_frame=" << maxFrameCopyCount
                  << " copied_mb=" << static_cast<double>(totalCopiedBytes) / (1024.0 * 1024.0)
                  << " wait_idle_calls=" << (idleAfter - idleBefore) << std::endl;
        Expect(idleAfter == idleBefore, "連続フレームの間に WaitIdle を呼んではならない");
        Expect(waiting.empty() && queued.empty(), "積んだコピーはすべて記録されなければならない");
        Expect(ringFullCount > 0, "リングより多く書くので、区画の完了待ちで一度は詰まるはず（区画を早く再利用していない確認）");
        Expect(deferredFrames > 0, "1フレームの上限を超える分は次のフレームへ持ち越されるはず");
        Expect(totalCopiedBytes > TestRingBytes * 4u, "リングの大きさを大きく超える量を、リングを回して書けたはず");

        // 残りのスロットの読み戻しを確かめる（デバイスを止めてよい。ここは連続フレームの外）
        harness.Device->WaitIdle();
        for (uint32_t slot = 0; slot < SlotCount; ++slot)
        {
            VerifySlot(harness, slot);
        }
        Expect(harness.VerifiedCount == generated, "書いたコピーはすべて読み戻して確かめられたはず");
        uploader.Clear();
    }

    // 最終的に、各領域が最後に書いた内容を保っていることを、別のコマンドで読み戻して確かめる。
    void TestFinalContents(Harness& harness)
    {
        std::cout << TestName << " --- 最後に書いた内容 ---" << std::endl;
        BufferPtr readback = harness.Device->CreateBuffer(
            BufferDesc(SlotReadbackBytes, ResourceUsage::TransferDst, true, "SparseTileUploadFinalReadback"));
        if (!readback)
        {
            Expect(false, "最終確認の読み戻しバッファを作れなければならない");
            return;
        }
        for (uint32_t regionIndex = 0; regionIndex < harness.Regions.size(); ++regionIndex)
        {
            const RegionSpec& spec = harness.Regions[regionIndex];
            Expect(harness.Final[regionIndex].size() == spec.Bytes, "すべての領域に1回以上書いているはず");
            if (harness.Final[regionIndex].size() != spec.Bytes)
            {
                continue;
            }
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            commandList->TextureBarrier(harness.Texture, ResourceState::ShaderResource, ResourceState::CopySource);
            TextureRegionCopy region = spec.Region;
            region.BufferOffset = 0;
            Expect(commandList->CopyTextureRegionToBuffer(harness.Texture, readback, region), "最終確認の読み戻しを記録できなければならない");
            commandList->TextureBarrier(harness.Texture, ResourceState::CopySource, ResourceState::ShaderResource);
            commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, spec.Bytes);
            commandList->End();
            commandList->Submit(true);

            const uint8_t* mapped = static_cast<const uint8_t*>(readback->Map(0u, spec.Bytes));
            const bool bSame = mapped != nullptr && std::memcmp(mapped, harness.Final[regionIndex].data(), static_cast<size_t>(spec.Bytes)) == 0;
            if (mapped != nullptr)
            {
                readback->Unmap();
            }
            std::cout << TestName << " 最終 region=" << spec.Name << (bSame ? " 一致" : " 不一致") << std::endl;
            Expect(bSame, "各領域は最後に書いた内容を保っていなければならない");
        }
    }

    // 空きの契約: 区画はコピーを含む提出の serial が完了するまで再利用しない。上限を超える先頭の1件は必ずコピーする。
    // 提出しなかったフレームのコピーは、次のフレームで出し直す。
    void TestRingContract(Harness& harness)
    {
        std::cout << TestName << " --- リングと上限の契約 ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = 8u * Page;
        TileUploader uploader(harness.Device, config);
        const RegionSpec& spec = harness.Regions[0];
        const VariableArray<uint8_t> data = MakeData(99u, spec.Bytes);

        Expect(uploader.GetRingBytes() == TestRingBytes, "リングの大きさは設定どおり");
        Expect(uploader.GetStats().RingBytes == 0, "最初の Enqueue までリングは作らない");
        Expect(!uploader.EnqueueTile(harness.Texture, spec.Region, data.data(), 0u), "大きさ 0 は断る");
        Expect(!uploader.EnqueueTile(harness.Texture, spec.Region, nullptr, spec.Bytes), "データが null は断る");
        Expect(!uploader.EnqueueTile(nullptr, spec.Region, data.data(), spec.Bytes), "テクスチャが null は断る");
        VariableArray<uint8_t> tooBig;
        tooBig.resize(static_cast<size_t>(TestRingBytes + 1u));
        Expect(!uploader.EnqueueTile(harness.Texture, spec.Region, tooBig.data(), tooBig.size()), "リングより大きいデータは断る");
        {
            // フレームのコピー量の上限より大きい1件は、リングに収まっても断る
            TileUploader::Config tightConfig;
            tightConfig.RingBytes = TestRingBytes;
            tightConfig.FrameCopyLimitBytes = Page / 2u;
            TileUploader tight(harness.Device, tightConfig);
            Expect(!tight.EnqueueTile(harness.Texture, spec.Region, data.data(), data.size()),
                   "フレームのコピー量の上限より大きい1件は断る");
            Expect(tight.GetStats().PendingCopies == 0u, "断った依頼は積まれない");
        }
        TextureRegionCopy emptyRegion = spec.Region;
        emptyRegion.Width = 0;
        Expect(!uploader.EnqueueTile(harness.Texture, emptyRegion, data.data(), spec.Bytes), "矩形が空のコピーは断る");
        Expect(uploader.GetStats().RingBytes == 0, "断った Enqueue ではリングを作らない");

        // 4タイル分を詰め込むとリングが満杯になる（テクスチャは連続フレームのテストで初期化済み）。
        // 領域が重なるコピーは同じフレームへ積めないので、4件は別々のタイルへ書く。
        for (uint32_t index = 0; index < 4u; ++index)
        {
            Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[index].Region, data.data(), data.size()),
                   "リングの空きがある間は積める");
        }
        {
            // リングは DeviceLocal でない host-visible のメモリでなければならない
            const BufferPtr ring = uploader.GetRingBuffer();
            Expect(ring != nullptr, "最初の Enqueue でリングを作る");
            const uint32_t flags = RHI::Vulkan::GetVulkanBufferMemoryPropertyFlagsForTesting(ring.get());
            std::cout << TestName << " ring_memory_flags=0x" << std::hex << flags << std::dec << std::endl;
            Expect((flags & VkMemoryHostVisibleBit) != 0u && (flags & VkMemoryHostCoherentBit) != 0u,
                   "リングは host-visible・host-coherent でなければならない");
            Expect((flags & VkMemoryDeviceLocalBit) == 0u, "リングは DeviceLocal のメモリであってはならない");
        }
        Expect(!uploader.EnqueueTile(harness.Texture, spec.Region, data.data(), data.size()), "満杯のリングへは積めない");
        Expect(uploader.GetStats().RingUsedBytes == TestRingBytes, "満杯のとき使用量はリングの大きさ");

        // 記録して提出済み（serial 10）にしても、完了するまで区画は空かない
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(0u);
            Expect(uploader.RecordCopies(*commandList) == 4u, "上限（8タイル分）に収まる4件をすべて記録する");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(10u);
        }
        uploader.BeginFrame(9u);
        Expect(!uploader.EnqueueTile(harness.Texture, spec.Region, data.data(), data.size()), "serial 9 の完了では区画をまだ再利用しない");
        Expect(uploader.GetStats().RingUsedBytes == TestRingBytes, "完了の serial が届くまで使用量は減らない");
        uploader.BeginFrame(10u);
        Expect(uploader.GetStats().RingUsedBytes == 0, "serial 10 の完了で区画が空く");
        for (uint32_t index = 0; index < 4u; ++index)
        {
            Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[index].Region, data.data(), data.size()),
                   "空いた区画へまた積める（リングを回す）");
        }

        // 提出しなかったフレームは、記録を未記録へ戻して次のフレームで出し直す
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(10u);
            Expect(uploader.RecordCopies(*commandList) == 4u, "積んだ4件を記録する");
            commandList->End();
            uploader.AbortFrame();
            const TileUploader::Stats stats = uploader.GetStats();
            Expect(stats.PendingCopies == 4u && stats.InFlightCopies == 0u, "提出しなかったフレームのコピーは未記録へ戻る");
        }
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(10u);
            Expect(uploader.RecordCopies(*commandList) == 4u, "次のフレームで同じ4件を出し直す");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(11u);
        }
        uploader.BeginFrame(11u);
        Expect(uploader.GetStats().RingUsedBytes == 0, "出し直した分も完了で区画が空く");

        // 上限が 1.5 タイル分なら、1フレームに入るのは1件まで（2件目で上限を超える）
        TileUploader::Config smallLimit;
        smallLimit.RingBytes = TestRingBytes;
        smallLimit.FrameCopyLimitBytes = Page + Page / 2u;
        TileUploader limited(harness.Device, smallLimit);
        for (uint32_t index = 0; index < 3u; ++index)
        {
            Expect(limited.EnqueueTile(harness.Texture, harness.Regions[index].Region, data.data(), data.size()),
                   "上限に収まる1件ずつは積める");
        }
        for (uint32_t frame = 0; frame < 3u; ++frame)
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            limited.BeginFrame(frame);
            Expect(limited.RecordCopies(*commandList) == 1u, "上限を超えない1件だけを1フレームに記録する");
            Expect(limited.GetStats().FrameCopiedBytes <= smallLimit.FrameCopyLimitBytes, "フレームのコピー量は上限以下");
            commandList->End();
            commandList->Submit(true);
            limited.CommitFrame(frame + 1u);
        }
        harness.Device->WaitIdle();
        uploader.Clear();
        limited.Clear();
        Expect(uploader.GetStats().RingBytes == 0 && uploader.GetStats().RingUsedBytes == 0, "Clear でリングと使用量を手放す");
    }

    // 取り消しと解除の契約: 積んだ依頼を記録する前に取り消せる。解除したテクスチャ宛ての依頼は GPU へ出さない。
    // 記録できる量の残りは、まだ記録していない依頼の分だけ減る。
    void TestDiscardAndAbandon(Harness& harness)
    {
        std::cout << TestName << " --- 取り消しと解除の契約 ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = 8u * Page;
        TileUploader uploader(harness.Device, config);
        const VariableArray<uint8_t> data = MakeData(7u, harness.Regions[0].Bytes);
        const uint64_t bytes = data.size();

        Expect(uploader.GetRecordableCopyBytes() == config.FrameCopyLimitBytes, "何も積んでいなければ、記録できる量は上限のまま");
        Expect(uploader.DiscardLastEnqueued(1u) == 0u, "何も積んでいなければ取り消すものは無い");

        // 取り消し: 後ろから取り消し、リングの区画と記録できる量が積む前へ戻る
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[0].Region, data.data(), bytes), "1件目を積む");
        const uint64_t usedAfterOne = uploader.GetStats().RingUsedBytes;
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[1].Region, data.data(), bytes), "2件目を積む");
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[2].Region, data.data(), bytes), "3件目を積む");
        Expect(uploader.GetRecordableCopyBytes() == config.FrameCopyLimitBytes - 3u * bytes, "積んだ分だけ記録できる量が減る");
        Expect(uploader.DiscardLastEnqueued(2u) == 2u, "後ろから2件を取り消す");
        Expect(uploader.GetStats().PendingCopies == 1u && uploader.GetStats().RingUsedBytes == usedAfterOne,
               "取り消した分のリングの区画は積む前へ戻る");
        Expect(uploader.GetRecordableCopyBytes() == config.FrameCopyLimitBytes - bytes, "取り消した分だけ記録できる量が戻る");
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[1].Region, data.data(), bytes), "取り消した後にまた積める");
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(0u);
            Expect(uploader.RecordCopies(*commandList) == 2u, "取り消さなかった1件と積み直した1件を記録する");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(1u);
        }
        Expect(uploader.DiscardLastEnqueued(1u) == 0u, "記録を始めた依頼は取り消さない");
        uploader.BeginFrame(1u);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "提出が完了したら区画が空く");

        // 解除: 未記録の依頼はその場で無効になり、記録されず、区画は順番どおりに手放される
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[0].Region, data.data(), bytes), "解除前の依頼を積む");
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[1].Region, data.data(), bytes), "解除前の依頼をもう1件積む");
        uploader.AbandonTexture(harness.Texture);
        Expect(uploader.GetStats().PendingCopies == 0u, "解除したテクスチャ宛ての未記録の依頼は無効になる");
        Expect(uploader.GetRecordableCopyBytes() == config.FrameCopyLimitBytes, "無効にした依頼は記録できる量を使わない");
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(1u);
            Expect(uploader.RecordCopies(*commandList) == 0u, "無効にした依頼は記録しない");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(2u);
        }
        uploader.BeginFrame(1u);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "無効にした依頼の区画は、先頭から順に手放される");

        // 解除: 記録中のフレームを提出できなかったときは、出し直さず無効にする
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[0].Region, data.data(), bytes), "記録する依頼を積む");
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(2u);
            Expect(uploader.RecordCopies(*commandList) == 1u, "依頼を記録する");
            uploader.AbandonTexture(harness.Texture);
            commandList->End();
            uploader.AbortFrame();
            const TileUploader::Stats stats = uploader.GetStats();
            Expect(stats.PendingCopies == 0u && stats.InFlightCopies == 0u,
                   "記録中に解除して提出しなかった依頼は、未記録へ戻さず無効にする");
        }
        uploader.BeginFrame(2u);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "提出しなかった無効な依頼の区画も手放される");

        // 解除: 記録中のフレームを提出したときは、そのフレームの完了まで区画を持つ
        Expect(uploader.EnqueueTile(harness.Texture, harness.Regions[0].Region, data.data(), bytes), "もう一度、記録する依頼を積む");
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(2u);
            Expect(uploader.RecordCopies(*commandList) == 1u, "依頼を記録する");
            uploader.AbandonTexture(harness.Texture);
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(3u);
        }
        uploader.BeginFrame(2u);
        Expect(uploader.GetStats().InFlightCopies == 1u && uploader.GetStats().RingUsedBytes > 0u,
               "記録中に解除して提出した依頼は、完了まで保持する");
        uploader.BeginFrame(3u);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "提出の完了で区画が空く");

        harness.Device->WaitIdle();
        uploader.Clear();
    }

    // 領域を最後に書いた内容を、別のコマンドで読み戻して確かめる
    bool RegionEquals(Harness& harness, const RegionSpec& spec, const VariableArray<uint8_t>& expected)
    {
        BufferPtr readback = harness.Device->CreateBuffer(
            BufferDesc(SlotReadbackBytes, ResourceUsage::TransferDst, true, "SparseTileUploadOverlapReadback"));
        if (!readback)
        {
            return false;
        }
        CommandListPtr commandList = harness.Device->CreateCommandList();
        commandList->Begin();
        commandList->TextureBarrier(harness.Texture, ResourceState::ShaderResource, ResourceState::CopySource);
        TextureRegionCopy region = spec.Region;
        region.BufferOffset = 0;
        const bool bRecorded = commandList->CopyTextureRegionToBuffer(harness.Texture, readback, region);
        commandList->TextureBarrier(harness.Texture, ResourceState::CopySource, ResourceState::ShaderResource);
        commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, spec.Bytes);
        commandList->End();
        commandList->Submit(true);
        const uint8_t* mapped = static_cast<const uint8_t*>(readback->Map(0u, spec.Bytes));
        const bool bSame = bRecorded && mapped != nullptr && expected.size() == spec.Bytes &&
                           std::memcmp(mapped, expected.data(), static_cast<size_t>(spec.Bytes)) == 0;
        if (mapped != nullptr)
        {
            readback->Unmap();
        }
        return bSame;
    }

    // 同じ領域へ異なる内容を続けて積んでも、後に積んだ内容が残る。
    // 領域が重なるコピーは同じフレームへ入らず、重ならないコピーは同じフレームへ入る。
    void TestOverlappingWrites(Harness& harness)
    {
        std::cout << TestName << " --- 同じ領域への連続コピー ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = 8u * Page;
        TileUploader uploader(harness.Device, config);
        const RegionSpec& r0 = harness.Regions[0];
        const RegionSpec& r1 = harness.Regions[1];
        const RegionSpec& r2 = harness.Regions[2];
        const VariableArray<uint8_t> first = MakeData(1001u, r0.Bytes);
        const VariableArray<uint8_t> second = MakeData(1002u, r0.Bytes);
        const VariableArray<uint8_t> other = MakeData(1003u, r1.Bytes);

        // 重ならない（辺が接するだけの）3領域は、同じフレームへ入る
        Expect(uploader.EnqueueTile(harness.Texture, r0.Region, first.data(), first.size()), "r0 を積む");
        Expect(uploader.EnqueueTile(harness.Texture, r1.Region, other.data(), other.size()), "r1 を積む");
        Expect(uploader.EnqueueTile(harness.Texture, r2.Region, other.data(), other.size()), "r2 を積む");
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(0u);
            Expect(uploader.RecordCopies(*commandList) == 3u, "重ならない3件は同じフレームへ記録する");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(1u);
        }

        // r0 へ first→second の順、間に r1 を挟む。r0 の2件目は次のフレームへ送り、順序を保つ
        uploader.BeginFrame(1u);
        Expect(uploader.EnqueueTile(harness.Texture, r0.Region, first.data(), first.size()), "r0 に first を積む");
        Expect(uploader.EnqueueTile(harness.Texture, r1.Region, other.data(), other.size()), "r1 を積む");
        Expect(uploader.EnqueueTile(harness.Texture, r0.Region, second.data(), second.size()), "r0 に second を積む");
        Expect(uploader.EnqueueTile(harness.Texture, r2.Region, other.data(), other.size()), "r2 を積む");
        uint64_t serial = 2u;
        uint32_t frames = 0;
        uint32_t firstFrameCopies = 0;
        while (uploader.GetStats().PendingCopies > 0u && frames < 8u)
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            uploader.BeginFrame(serial - 1u);
            const uint32_t recorded = uploader.RecordCopies(*commandList);
            if (frames == 0u)
            {
                firstFrameCopies = recorded;
            }
            Expect(recorded > 0u, "積んだコピーは必ず進む（重なりで詰まらない）");
            commandList->End();
            commandList->Submit(true);
            uploader.CommitFrame(serial);
            ++serial;
            ++frames;
        }
        std::cout << TestName << " overlap_frames=" << frames << " first_frame_copies=" << firstFrameCopies << std::endl;
        Expect(firstFrameCopies == 2u, "最初のフレームは r0・r1 の2件で、r0 の2件目で区切る");
        Expect(frames == 2u, "重なる2件は2フレームに分かれる");
        Expect(RegionEquals(harness, r0, second), "同じ領域へ続けて積んだときは後の内容が残る");
        Expect(RegionEquals(harness, r1, other), "挟んだ別領域の内容も保たれる");

        harness.Device->WaitIdle();
        uploader.Clear();
    }

    // 既存の VulkanTexture::Update（キューの完了待ちを含む経路）を、計数が検出できることの確認。
    // これが 0 のままなら「WaitIdle を呼ばない」ことの検査として意味を持たない。
    void TestWaitIdleCounterDetectsUpdate(Harness& harness)
    {
        std::cout << TestName << " --- 完了待ちの計数が既存の Update を検出する ---" << std::endl;
        TextureDesc desc;
        desc.Width = 4u;
        desc.Height = 4u;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.DebugName = "SparseTileUploadWaitProbe";
        TexturePtr probe = harness.Device->CreateTexture(desc);
        if (!probe)
        {
            Expect(false, "計数の確認用テクスチャを作れなければならない");
            return;
        }
        uint8_t pixels[4u * 4u * 4u] = {};
        const uint64_t before = RHI::Vulkan::GetVulkanDeviceWaitIdleCallCountForTesting();
        probe->Update(pixels, 4u * 4u, sizeof(pixels), 0u, 0u);
        const uint64_t after = RHI::Vulkan::GetVulkanDeviceWaitIdleCallCountForTesting();
        std::cout << TestName << " update_wait_calls=" << (after - before) << std::endl;
        Expect(after > before, "既存の Update 経路の完了待ちを、計数が検出できなければならない");
        harness.Device->WaitIdle();
    }

    // バッファの作成が（メモリの種類の選択で）失敗したとき、作成済みの VkBuffer を残さないことの確認。
    // CPUAccessible=false は DeviceLocal を要求し、bExcludeDeviceLocal=true はそれを除外するので、選択は必ず失敗する。
    void TestBufferCreateFailureCleansUp(Harness& harness)
    {
        std::cout << TestName << " --- バッファ作成の失敗後に VkBuffer が残らない ---" << std::endl;
        constexpr uint32_t Attempts = 16u;
        const int64_t buffersBefore = RHI::Vulkan::GetVulkanLiveBufferCountForTesting();
        const int64_t memoriesBefore = RHI::Vulkan::GetVulkanLiveBufferMemoryCountForTesting();

        // 正常な作成は計数を増やし、破棄で戻ることも確かめる（計数が動かないままの偽の合格を避ける）
        {
            BufferPtr normal = harness.Device->CreateBuffer(BufferDesc(256u, ResourceUsage::TransferSrc, true, "SparseTileUploadCountProbe"));
            Expect(normal != nullptr, "正常なバッファは作れなければならない");
            Expect(RHI::Vulkan::GetVulkanLiveBufferCountForTesting() == buffersBefore + 1, "作成したバッファは計数に入る");
            Expect(RHI::Vulkan::GetVulkanLiveBufferMemoryCountForTesting() == memoriesBefore + 1, "作成したメモリは計数に入る");
        }
        Expect(RHI::Vulkan::GetVulkanLiveBufferCountForTesting() == buffersBefore, "破棄したバッファは計数から出る");

        uint32_t failures = 0;
        for (uint32_t attempt = 0; attempt < Attempts; ++attempt)
        {
            BufferDesc desc(256u, ResourceUsage::TransferSrc, false, "SparseTileUploadFailingBuffer");
            desc.bExcludeDeviceLocal = true;
            BufferPtr buffer;
            try
            {
                buffer = harness.Device->CreateBuffer(desc);
            }
            catch (const std::exception&)
            {
                buffer.reset();
            }
            if (!buffer)
            {
                ++failures;
            }
        }
        const int64_t buffersAfter = RHI::Vulkan::GetVulkanLiveBufferCountForTesting();
        const int64_t memoriesAfter = RHI::Vulkan::GetVulkanLiveBufferMemoryCountForTesting();
        std::cout << TestName << " create_failures=" << failures << " live_buffers=" << buffersBefore << "->" << buffersAfter
                  << " live_memories=" << memoriesBefore << "->" << memoriesAfter << std::endl;
        Expect(failures == Attempts, "DeviceLocal を要求して除外したバッファの作成は必ず失敗する");
        Expect(buffersAfter == buffersBefore, "作成に失敗しても VkBuffer が残ってはならない");
        Expect(memoriesAfter == memoriesBefore, "作成に失敗してもバッファ用メモリが残ってはならない");
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

        {
            // 先に宣言したものが後に破棄される（ページを返す前にテクスチャを破棄する）。
            SparsePagePool pool(device, 16u * Page);
            VariableArray<SparsePagePool::PageLease> leases;
            Harness harness;
            harness.Device = device;

            TextureDesc desc;
            desc.Width = TextureSize;
            desc.Height = TextureSize;
            desc.MipLevels = 11;
            desc.TextureFormat = Format::BC7_UNORM;
            desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst | ResourceUsage::TransferSrc;
            desc.bSparse = true;
            desc.DebugName = "SparseTileUploadTarget";
            harness.Texture = device->CreateTexture(desc);
            SparseTextureInfo info;
            if (!harness.Texture || !harness.Texture->GetSparseInfo(info))
            {
                std::cerr << TestName << " sparse の BC7 テクスチャを作れませんでした\n";
                return 1;
            }
            Expect(info.TileWidth == 256 && info.TileHeight == 256 && info.TileSizeBytes == Page, "BC7 のタイルは 256x256・64 KiB");
            Expect(info.TilesX[0] == 4 && info.TilesY[0] == 4, "1024x1024 のミップ0 は 4x4 枚");
            Expect(info.MipTailFirstLevel >= 2, "ミップ1のタイルを使うので、ミップテイルはミップ2以降から");

            // 書く領域: ミップ0のタイル（原点・隣・奥）、ミップ1のタイル、ミップテイルの中のミップ
            struct TileSpec
            {
                const char* Name;
                uint32_t Mip;
                uint32_t TileX;
                uint32_t TileY;
            };
            const TileSpec tileSpecs[] = {
                {"mip0(0,0)", 0u, 0u, 0u},
                {"mip0(1,0)", 0u, 1u, 0u},
                {"mip0(3,2)", 0u, 3u, 2u},
                {"mip0(3,3)", 0u, 3u, 3u},
                {"mip1(1,1)", 1u, 1u, 1u},
            };
            SparseBindRequest bindRequest;
            for (const TileSpec& tileSpec : tileSpecs)
            {
                leases.push_back(pool.Acquire());
                if (!leases.back().IsValid())
                {
                    std::cerr << TestName << " ページを借りられませんでした\n";
                    return 1;
                }
                SparseTileBind tile;
                tile.Texture = harness.Texture.get();
                tile.MipLevel = tileSpec.Mip;
                tile.TileX = tileSpec.TileX;
                tile.TileY = tileSpec.TileY;
                tile.Page = leases.back().GetPage();
                bindRequest.Tiles.push_back(tile);

                RegionSpec spec;
                spec.Name = tileSpec.Name;
                spec.Region.MipLevel = tileSpec.Mip;
                spec.Region.OffsetX = tileSpec.TileX * info.TileWidth;
                spec.Region.OffsetY = tileSpec.TileY * info.TileHeight;
                spec.Region.Width = info.TileWidth;
                spec.Region.Height = info.TileHeight;
                spec.Bytes = BC7Bytes(spec.Region.Width, spec.Region.Height);
                Expect(spec.Bytes == Page, "BC7 の1タイルは 64 KiB");
                harness.Regions.push_back(spec);
            }
            const uint32_t tailPages = static_cast<uint32_t>((info.MipTailSize + Page - 1) / Page);
            for (uint32_t page = 0; page < tailPages; ++page)
            {
                leases.push_back(pool.Acquire());
                if (!leases.back().IsValid())
                {
                    std::cerr << TestName << " ミップテイル用のページを借りられませんでした\n";
                    return 1;
                }
                SparseMipTailBind tail;
                tail.Texture = harness.Texture.get();
                tail.PageIndex = page;
                tail.Page = leases.back().GetPage();
                bindRequest.MipTails.push_back(tail);
            }
            {
                RegionSpec spec;
                spec.Name = "mipテイル内";
                spec.Region.MipLevel = info.MipTailFirstLevel;
                spec.Region.Width = std::max(TextureSize >> info.MipTailFirstLevel, 4u);
                spec.Region.Height = spec.Region.Width;
                spec.Bytes = BC7Bytes(spec.Region.Width, spec.Region.Height);
                harness.Regions.push_back(spec);
            }
            harness.Final.resize(harness.Regions.size());
            Expect(device->BindSparse(bindRequest), "ページの結び付けが成功しなければならない");

            for (uint32_t slot = 0; slot < SlotCount; ++slot)
            {
                harness.Readback[slot] = device->CreateBuffer(
                    BufferDesc(SlotReadbackBytes, ResourceUsage::TransferDst, true, "SparseTileUploadReadback"));
                if (!harness.Readback[slot])
                {
                    std::cerr << TestName << " 読み戻しのバッファを作れませんでした\n";
                    return 1;
                }
            }

            TestContinuousFrames(harness);
            TestFinalContents(harness);
            TestRingContract(harness);
            TestDiscardAndAbandon(harness);
            TestOverlappingWrites(harness);
            TestWaitIdleCounterDetectsUpdate(harness);
            TestBufferCreateFailureCleansUp(harness);

            device->WaitIdle();
            for (BufferPtr& readback : harness.Readback)
            {
                readback.reset();
            }
            harness.Texture.reset();
        }
        device->WaitIdle();

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
