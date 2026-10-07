// ステージングのリング経由で、ジオメトリのプール（DeviceLocal の大きなバッファ）の区画へ GPU を待たずに書く経路
// （TileUploader::EnqueueBufferCopy）の GPU テスト。
// 連続する多数のフレームで、プールの区画の途中へ書き、同じフレームのコマンドで計算シェーダー
// （Test/Core/Rendering/Shaders/gpu_upload_ring_probe.comp）が区画を storage buffer として読んで結果を写し、
// 書いたバイト列と一致することを確かめる。その間に vkDeviceWaitIdle を一度も呼ばない（デバイスの計数で確かめる）。
// 1フレームのコピー量の上限と、リングの大きさはテクスチャとバッファで共有する（混在のフレームで確かめる）。
// 同じバッファでバイト範囲が重なるコピーは次のフレームへ送られ、解放した区画宛てのコピーは無効になる。
// Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Rendering/GeometryPool.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/TileUploader.h"

#include "Container/Containers.h"
#include "Container/Deque.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
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
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "GpuUploadRingVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t SlotCount = 3u;
    constexpr uint32_t RegionCount = 6u;
    constexpr uint64_t RegionBytes = 64u * 1024u;
    // プールの塊は 1 MiB（既定は 256 MiB）。区画 6 本は 1 つの塊に収まる
    constexpr uint64_t PoolBlockBytes = 1024u * 1024u;
    constexpr uint64_t TestRingBytes = 192u * 1024u;
    constexpr uint64_t TestFrameLimitBytes = 96u * 1024u;
    // 1フレームの読み戻しの置き場（上限 + 256 バイト境界への切り上げの余白）
    constexpr uint64_t SlotResultBytes = 256u * 1024u;
    constexpr uint32_t NewCopiesPerFrame = 6u;
    constexpr uint32_t GeneratingFrames = 40u;
    constexpr uint32_t MaxFrames = 200u;

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

    DescriptorSetDesc MakeProbeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        for (uint32_t bindingIndex = 0; bindingIndex < 2u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = ResourceBindType::RWBuffer;
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    // プールの区画の範囲を、計算シェーダーで読んで結果のバッファの dstOffset から写す。コピーの記録より後に呼ぶ。
    bool RecordProbe(const DevicePtr& device, const PipelinePtr& pipeline, ICommandList& commandList,
                     const BufferPtr& source, uint64_t srcOffset, uint64_t bytes, const BufferPtr& result,
                     uint64_t dstOffset, VariableArray<DescriptorSetPtr>& keepAlive)
    {
        DescriptorSetPtr descriptorSet = device->CreateDescriptorSet(MakeProbeDescriptorSetDesc());
        if (!descriptorSet)
        {
            return false;
        }
        descriptorSet->BindStorageBuffer(0u, source, static_cast<uint32_t>(srcOffset), static_cast<uint32_t>(bytes));
        descriptorSet->BindStorageBuffer(1u, result, static_cast<uint32_t>(dstOffset), static_cast<uint32_t>(bytes));
        descriptorSet->Update();
        commandList.SetPipeline(pipeline);
        commandList.SetDescriptorSet(descriptorSet);
        commandList.Dispatch(static_cast<uint32_t>((bytes / 4u + 63u) / 64u), 1u, 1u);
        keepAlive.push_back(std::move(descriptorSet));
        return true;
    }

    struct Harness
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        // プールは区画（RegionLease）より後に破棄する（宣言順の逆に破棄される）ので、先に宣言する
        TUniquePtr<GeometryPool> Pool;
        VariableArray<GeometryPool::RegionLease> Regions;
        BufferPtr PoolBuffer;
        BufferPtr Result[SlotCount];
        BufferPtr Readback[SlotCount];
        VariableArray<DescriptorSetPtr> SlotSets[SlotCount];
    };

    struct VerifyItem
    {
        uint64_t Offset = 0;
        VariableArray<uint8_t> Expected;
        uint32_t Region = 0;
    };

    struct Planned
    {
        uint32_t Region = 0;
        // 区画の先頭からの位置（256 の倍数）と大きさ（4 の倍数）
        uint64_t LocalOffset = 0;
        VariableArray<uint8_t> Data;
    };

    // 区画ごとの、最後に書いたはずの内容（書いていないバイトは Written が 0）
    struct Shadow
    {
        VariableArray<uint8_t> Bytes[RegionCount];
        VariableArray<uint8_t> Written[RegionCount];

        Shadow()
        {
            for (uint32_t region = 0; region < RegionCount; ++region)
            {
                Bytes[region].resize(static_cast<size_t>(RegionBytes));
                Written[region].resize(static_cast<size_t>(RegionBytes));
                std::memset(Written[region].data(), 0, Written[region].size());
            }
        }

        void Apply(const Planned& planned)
        {
            std::memcpy(Bytes[planned.Region].data() + planned.LocalOffset, planned.Data.data(), planned.Data.size());
            std::memset(Written[planned.Region].data() + planned.LocalOffset, 1, planned.Data.size());
        }
    };

    void VerifySlot(Harness& harness, uint32_t slot, VariableArray<VerifyItem>& items, uint32_t& verified)
    {
        if (items.empty())
        {
            return;
        }
        const uint8_t* mapped = static_cast<const uint8_t*>(harness.Readback[slot]->Map(0u, SlotResultBytes));
        Expect(mapped != nullptr, "読み戻しのバッファを写像できなければならない");
        if (mapped != nullptr)
        {
            for (const VerifyItem& item : items)
            {
                const bool bSame = std::memcmp(mapped + item.Offset, item.Expected.data(), item.Expected.size()) == 0;
                if (!bSame)
                {
                    std::cerr << TestName << " 不一致: region=" << item.Region << " offset=" << item.Offset << std::endl;
                }
                Expect(bSame, "プールの区画へ書いた内容を、計算シェーダーで読み戻した内容が一致しなければならない");
                ++verified;
            }
            harness.Readback[slot]->Unmap();
        }
        items.clear();
    }

    // 区画の範囲を、別のコマンドで読み戻す（デバイスを止めてよい場面でだけ使う）
    bool ReadRegion(Harness& harness, uint32_t region, uint64_t localOffset, uint64_t bytes, VariableArray<uint8_t>& out)
    {
        CommandListPtr commandList = harness.Device->CreateCommandList();
        if (!commandList)
        {
            return false;
        }
        VariableArray<DescriptorSetPtr> keepAlive;
        commandList->Begin();
        commandList->BufferBarrier(harness.Result[0], ResourceState::Undefined, ResourceState::UnorderedAccess, 0u,
                                   SlotResultBytes);
        if (!RecordProbe(harness.Device, harness.Pipeline, *commandList, harness.PoolBuffer,
                         harness.Regions[region].GetOffsetBytes() + localOffset, bytes, harness.Result[0], 0u, keepAlive))
        {
            return false;
        }
        commandList->BufferBarrier(harness.Result[0], ResourceState::UnorderedAccess, ResourceState::CopySource, 0u,
                                   bytes);
        commandList->CopyBuffer(harness.Result[0], harness.Readback[0], bytes);
        commandList->BufferBarrier(harness.Readback[0], ResourceState::CopyDest, ResourceState::HostRead, 0u, bytes);
        commandList->End();
        commandList->Submit(true);
        harness.Device->WaitIdle();

        const uint8_t* mapped = static_cast<const uint8_t*>(harness.Readback[0]->Map(0u, bytes));
        if (mapped == nullptr)
        {
            return false;
        }
        out.resize(static_cast<size_t>(bytes));
        std::memcpy(out.data(), mapped, static_cast<size_t>(bytes));
        harness.Readback[0]->Unmap();
        return true;
    }

    // 1フレームを記録して提出する（読み戻しは付けない）。serial は 1 から連番。
    uint32_t SubmitFrame(Harness& harness, TileUploader& uploader, uint64_t& serial)
    {
        CommandListPtr commandList = harness.Device->CreateCommandList();
        if (!commandList)
        {
            Expect(false, "コマンドリストを作れなければならない");
            return 0;
        }
        commandList->Begin();
        // 完了は呼び出し側が WaitIdle で保証する（この道具は小さなケース用）
        uploader.BeginFrame(serial);
        const uint32_t recorded = uploader.RecordCopies(*commandList);
        commandList->End();
        commandList->Submit(true);
        harness.Device->WaitIdle();
        ++serial;
        uploader.CommitFrame(serial);
        return recorded;
    }

    // 連続フレームで、プールの区画の途中へ書いて読み戻す。WaitIdle は呼ばない。
    void TestContinuousFrames(Harness& harness, Shadow& shadow)
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

        VariableArray<VerifyItem> slotVerify[SlotCount];
        Deque<Planned> waiting;
        Deque<Planned> queued;
        uint64_t generated = 0;
        uint32_t ringFullCount = 0;
        uint32_t deferredFrames = 0;
        uint32_t maxFrameCopyCount = 0;
        uint64_t totalCopiedBytes = 0;
        uint32_t verified = 0;
        uint32_t frame = 0;

        const uint64_t idleBefore = RHI::Vulkan::GetVulkanDeviceWaitIdleCallCountForTesting();
        for (frame = 1; frame <= MaxFrames; ++frame)
        {
            const uint32_t slot = (frame - 1u) % SlotCount;
            CommandListPtr& commandList = lists[slot];
            // 3つ前のフレームの提出の完了をコマンドリストのフェンスで待つ（デバイス全体は待たない）
            commandList->Begin();
            VerifySlot(harness, slot, slotVerify[slot], verified);
            harness.SlotSets[slot].clear();
            const uint64_t completed = frame > SlotCount ? frame - SlotCount : 0u;
            uploader.BeginFrame(completed);

            if (frame <= GeneratingFrames)
            {
                for (uint32_t copy = 0; copy < NewCopiesPerFrame; ++copy)
                {
                    Planned planned;
                    planned.Region = static_cast<uint32_t>(generated % RegionCount);
                    planned.LocalOffset = (generated % 4u) * 4096u;
                    const uint64_t bytes = 16384u + (generated * 52u % 16384u) / 4u * 4u;
                    planned.Data = MakeData(generated + 1u, bytes);
                    ++generated;
                    waiting.push_back(std::move(planned));
                }
            }
            while (!waiting.empty())
            {
                Planned& planned = waiting.front();
                if (!uploader.EnqueueBufferCopy(harness.PoolBuffer,
                                                harness.Regions[planned.Region].GetOffsetBytes() + planned.LocalOffset,
                                                planned.Data.data(), planned.Data.size()))
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

            if (recorded > 0)
            {
                commandList->BufferBarrier(harness.Result[slot], ResourceState::Undefined, ResourceState::UnorderedAccess,
                                           0u, SlotResultBytes);
                uint64_t offset = 0;
                for (uint32_t index = 0; index < recorded; ++index)
                {
                    Planned& planned = queued.front();
                    const uint64_t srcOffset = harness.Regions[planned.Region].GetOffsetBytes() + planned.LocalOffset;
                    Expect(RecordProbe(harness.Device, harness.Pipeline, *commandList, harness.PoolBuffer, srcOffset,
                                       planned.Data.size(), harness.Result[slot], offset, harness.SlotSets[slot]),
                           "読み戻しの dispatch を記録できなければならない");
                    VerifyItem item;
                    item.Offset = offset;
                    item.Region = planned.Region;
                    item.Expected = planned.Data;
                    slotVerify[slot].push_back(std::move(item));
                    offset += AlignUp256(planned.Data.size());
                    shadow.Apply(planned);
                    queued.pop_front();
                }
                Expect(offset <= SlotResultBytes, "1フレームの読み戻しが結果のバッファに収まらなければならない");
                commandList->BufferBarrier(harness.Result[slot], ResourceState::UnorderedAccess, ResourceState::CopySource,
                                           0u, offset);
                commandList->CopyBuffer(harness.Result[slot], harness.Readback[slot], offset);
                commandList->BufferBarrier(harness.Readback[slot], ResourceState::CopyDest, ResourceState::HostRead, 0u,
                                           offset);
            }
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
            VerifySlot(harness, slot, slotVerify[slot], verified);
        }
        Expect(verified == generated, "書いたコピーはすべて読み戻して確かめられたはず");
        uploader.Clear();
    }

    // 最後に書いた内容が、区画ごとに残っていること（書いたバイトだけを比べる）
    void TestFinalContents(Harness& harness, const Shadow& shadow)
    {
        std::cout << TestName << " --- 最後に書いた内容 ---" << std::endl;
        for (uint32_t region = 0; region < RegionCount; ++region)
        {
            VariableArray<uint8_t> actual;
            if (!ReadRegion(harness, region, 0u, RegionBytes, actual))
            {
                Expect(false, "区画の全体を読み戻せなければならない");
                continue;
            }
            uint64_t mismatches = 0;
            uint64_t checked = 0;
            for (uint64_t index = 0; index < RegionBytes; ++index)
            {
                if (shadow.Written[region][static_cast<size_t>(index)] == 0)
                {
                    continue;
                }
                ++checked;
                if (actual[static_cast<size_t>(index)] != shadow.Bytes[region][static_cast<size_t>(index)])
                {
                    ++mismatches;
                }
            }
            if (mismatches != 0)
            {
                std::cerr << TestName << " 区画 " << region << " の不一致バイト数: " << mismatches << std::endl;
            }
            Expect(checked > 0, "どの区画にも書いたバイトがあるはず");
            Expect(mismatches == 0, "区画の最後に書いた内容が残っていなければならない");
        }
    }

    // テクスチャとバッファが 1 つのリングと 1 フレームの上限を共有し、順序を保って持ち越される
    void TestSharedLimitWithTexture(Harness& harness)
    {
        std::cout << TestName << " --- テクスチャとバッファでリングと上限を共有 ---" << std::endl;
        constexpr uint32_t TileSize = 64u;
        constexpr uint64_t TileBytes = static_cast<uint64_t>(TileSize) * TileSize * 4u;
        constexpr uint64_t BufferCopyBytes = 40u * 1024u;

        TextureDesc desc;
        desc.Width = TileSize;
        desc.Height = TileSize;
        desc.MipLevels = 1;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst | ResourceUsage::TransferSrc;
        desc.DebugName = "GpuUploadRingSharedTexture";
        TexturePtr texture = harness.Device->CreateTexture(desc);
        Expect(texture != nullptr, "テクスチャを作れなければならない");
        if (!texture)
        {
            return;
        }

        TileUploader::Config config;
        config.RingBytes = 256u * 1024u;
        config.FrameCopyLimitBytes = 64u * 1024u;
        TileUploader uploader(harness.Device, config);

        const VariableArray<uint8_t> dataA = MakeData(101u, BufferCopyBytes);
        const VariableArray<uint8_t> dataB = MakeData(102u, BufferCopyBytes);
        const VariableArray<uint8_t> dataTile = MakeData(103u, TileBytes);
        TextureRegionCopy region;
        region.Width = TileSize;
        region.Height = TileSize;

        // 順序は A(40) → タイル(16) → B(40)。上限 64 KiB なので 1 フレーム目は A とタイル、B は次のフレーム。
        Expect(uploader.EnqueueInitialize(texture), "初期化の依頼を積めなければならない");
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, harness.Regions[0].GetOffsetBytes(), dataA.data(),
                                          dataA.size()),
               "バッファのコピー A を積めなければならない");
        Expect(uploader.EnqueueTile(texture, region, dataTile.data(), dataTile.size()), "タイルを積めなければならない");
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, harness.Regions[1].GetOffsetBytes(), dataB.data(),
                                          dataB.size()),
               "バッファのコピー B を積めなければならない");

        uint32_t pendingCount = 0;
        uint64_t pendingBytes = 0;
        uploader.GetPendingCopyLoad(pendingCount, pendingBytes);
        Expect(pendingCount == 3u && pendingBytes == BufferCopyBytes * 2u + TileBytes,
               "積んだバッファのコピーとタイルが、同じ未記録の量として数えられなければならない");
        Expect(uploader.GetRecordableCopyBytes() == 0u, "積んだ量が上限を超えているので、記録できる残りは 0");

        uint64_t serial = 0;
        const uint32_t recorded1 = SubmitFrame(harness, uploader, serial);
        const TileUploader::Stats stats1 = uploader.GetStats();
        Expect(recorded1 == 2u, "1フレーム目は A とタイルの 2 件だけを記録するはず");
        Expect(stats1.FrameCopiedBytes == BufferCopyBytes + TileBytes, "1フレーム目のコピー量はバッファとテクスチャの合計");
        Expect(stats1.FrameCopiedBytes <= config.FrameCopyLimitBytes, "上限を超えてはならない");
        const uint32_t recorded2 = SubmitFrame(harness, uploader, serial);
        Expect(recorded2 == 1u, "2フレーム目に持ち越した B を記録するはず");
        uploader.BeginFrame(serial);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "全コピーの提出が完了すれば、リングの区画はすべて空くはず");

        // 内容の確認
        VariableArray<uint8_t> actual;
        Expect(ReadRegion(harness, 0u, 0u, dataA.size(), actual) && std::memcmp(actual.data(), dataA.data(), dataA.size()) == 0,
               "A を書いた区画の内容が一致しなければならない");
        Expect(ReadRegion(harness, 1u, 0u, dataB.size(), actual) && std::memcmp(actual.data(), dataB.data(), dataB.size()) == 0,
               "B を書いた区画の内容が一致しなければならない");

        BufferPtr readback = harness.Device->CreateBuffer(
            BufferDesc(AlignUp256(TileBytes), ResourceUsage::TransferDst, true, "GpuUploadRingTextureReadback"));
        Expect(readback != nullptr, "テクスチャの読み戻しのバッファを作れなければならない");
        if (readback)
        {
            CommandListPtr commandList = harness.Device->CreateCommandList();
            commandList->Begin();
            commandList->TextureBarrier(texture, ResourceState::ShaderResource, ResourceState::CopySource);
            Expect(commandList->CopyTextureRegionToBuffer(texture, readback, region),
                   "テクスチャの読み戻しを記録できなければならない");
            commandList->TextureBarrier(texture, ResourceState::CopySource, ResourceState::ShaderResource);
            commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, TileBytes);
            commandList->End();
            commandList->Submit(true);
            harness.Device->WaitIdle();
            const uint8_t* mapped = static_cast<const uint8_t*>(readback->Map(0u, TileBytes));
            Expect(mapped != nullptr && std::memcmp(mapped, dataTile.data(), dataTile.size()) == 0,
                   "テクスチャのタイルの内容が一致しなければならない");
            if (mapped != nullptr)
            {
                readback->Unmap();
            }
        }
        uploader.Clear();
    }

    // 同じバッファでバイト範囲が重なるコピーは同じフレームに記録せず、積んだ順に次のフレームで書く
    void TestOverlappingBufferWrites(Harness& harness)
    {
        std::cout << TestName << " --- 重なるバイト範囲は次のフレームへ ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = TestFrameLimitBytes;
        TileUploader uploader(harness.Device, config);

        const VariableArray<uint8_t> first = MakeData(201u, 8192u);
        const VariableArray<uint8_t> second = MakeData(202u, 8192u);
        const VariableArray<uint8_t> third = MakeData(203u, 4096u);
        const uint64_t base = harness.Regions[2].GetOffsetBytes();
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base, first.data(), first.size()), "1件目を積めなければならない");
        // 範囲 [4096, 12288) は 1 件目の [0, 8192) と重なる
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base + 4096u, second.data(), second.size()),
               "2件目（1件目と重なる）を積めなければならない");
        // 範囲 [32768, 36864) は誰とも重ならないが、2 件目の後ろに並ぶので順序を保って同じフレームには出ない
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base + 32768u, third.data(), third.size()),
               "3件目を積めなければならない");

        uint64_t serial = 0;
        const uint32_t recorded1 = SubmitFrame(harness, uploader, serial);
        Expect(recorded1 == 1u, "1フレーム目は重ならない 1 件目だけを記録するはず");
        const uint32_t recorded2 = SubmitFrame(harness, uploader, serial);
        Expect(recorded2 == 2u, "2フレーム目は 2 件目と、重ならない 3 件目を記録するはず");

        VariableArray<uint8_t> actual;
        Expect(ReadRegion(harness, 2u, 0u, 12288u, actual), "区画を読み戻せなければならない");
        if (actual.size() == 12288u)
        {
            Expect(std::memcmp(actual.data(), first.data(), 4096u) == 0, "重ならない先頭は 1 件目の内容");
            Expect(std::memcmp(actual.data() + 4096u, second.data(), second.size()) == 0,
                   "重なった範囲は後から積んだ 2 件目の内容でなければならない");
        }
        Expect(ReadRegion(harness, 2u, 32768u, third.size(), actual) &&
                   std::memcmp(actual.data(), third.data(), third.size()) == 0,
               "3件目の内容が一致しなければならない");
        uploader.Clear();
    }

    // 解放した区画宛てのコピーを無効にすると、書かれず、リングの区画も戻る
    void TestAbandonBufferRange(Harness& harness)
    {
        std::cout << TestName << " --- 区画の解放でコピーを無効にする ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = TestFrameLimitBytes;
        TileUploader uploader(harness.Device, config);

        const VariableArray<uint8_t> kept = MakeData(301u, 4096u);
        const VariableArray<uint8_t> stale = MakeData(302u, 4096u);
        const VariableArray<uint8_t> other = MakeData(303u, 4096u);
        const uint64_t base = harness.Regions[3].GetOffsetBytes();

        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base, kept.data(), kept.size()), "元の内容を積めなければならない");
        uint64_t serial = 0;
        Expect(SubmitFrame(harness, uploader, serial) == 1u, "元の内容を記録するはず");
        uploader.BeginFrame(serial);

        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base, stale.data(), stale.size()), "古いコピーを積めなければならない");
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, base + 8192u, other.data(), other.size()),
               "別の範囲のコピーを積めなければならない");
        Expect(uploader.AbandonBufferRange(harness.PoolBuffer, base, 4096u) == 1u,
               "範囲に重なる未記録のコピー 1 件だけを無効にするはず");
        Expect(uploader.AbandonBufferRange(harness.PoolBuffer, base + 4096u, 4096u) == 0u,
               "何も書かない範囲では無効にするものがないはず");
        Expect(SubmitFrame(harness, uploader, serial) == 1u, "無効にしなかった別の範囲のコピーだけを記録するはず");

        VariableArray<uint8_t> actual;
        Expect(ReadRegion(harness, 3u, 0u, kept.size(), actual) && std::memcmp(actual.data(), kept.data(), kept.size()) == 0,
               "無効にしたコピーは書かれず、元の内容が残らなければならない");
        Expect(ReadRegion(harness, 3u, 8192u, other.size(), actual) &&
                   std::memcmp(actual.data(), other.data(), other.size()) == 0,
               "別の範囲のコピーは書かれなければならない");
        uploader.BeginFrame(serial);
        Expect(uploader.GetStats().RingUsedBytes == 0u, "無効にしたコピーの区画も、完了後にリングへ戻るはず");
        uploader.Clear();
    }

    // 引数が不正なときは false を返し、何も積まない
    void TestRejectedArguments(Harness& harness)
    {
        std::cout << TestName << " --- 不正な引数 ---" << std::endl;
        TileUploader::Config config;
        config.RingBytes = TestRingBytes;
        config.FrameCopyLimitBytes = TestFrameLimitBytes;
        TileUploader uploader(harness.Device, config);
        const VariableArray<uint8_t> data = MakeData(401u, 4096u);

        Expect(!uploader.EnqueueBufferCopy(nullptr, 0u, data.data(), data.size()), "バッファが null なら積まない");
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, 0u, nullptr, data.size()), "データが null なら積まない");
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, 0u, data.data(), 0u), "大きさ 0 は積まない");
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, harness.PoolBuffer->GetSize(), data.data(), data.size()),
               "バッファの外は積まない");
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, harness.PoolBuffer->GetSize() - 100u, data.data(),
                                           data.size()),
               "末尾をはみ出す範囲は積まない");
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, ~0ull - 10u, data.data(), data.size()),
               "オフセットのあふれは積まない");
        const VariableArray<uint8_t> overLimit = MakeData(402u, TestFrameLimitBytes + 4u);
        Expect(!uploader.EnqueueBufferCopy(harness.PoolBuffer, 0u, overLimit.data(), overLimit.size()),
               "1フレームの上限より大きい 1 件は積まない");

        // TransferDst の用途が無いバッファは、コピー先にできない
        BufferPtr noTransfer = harness.Device->CreateBuffer(
            BufferDesc(4096u, ResourceUsage::StorageBuffer, false, "GpuUploadRingNoTransferDst"));
        Expect(noTransfer != nullptr, "TransferDst の無いバッファを作れなければならない");
        if (noTransfer)
        {
            Expect(!uploader.EnqueueBufferCopy(noTransfer, 0u, data.data(), data.size()),
                   "TransferDst の用途が無いバッファは積まない");
        }

        uint32_t pendingCount = 0;
        uint64_t pendingBytes = 0;
        uploader.GetPendingCopyLoad(pendingCount, pendingBytes);
        Expect(pendingCount == 0u && pendingBytes == 0u && uploader.GetStats().RingUsedBytes == 0u,
               "拒否した引数では、何も積まれずリングも使われないはず");

        // 積んだ直後に取り消すと、リングの区画が戻る
        Expect(uploader.EnqueueBufferCopy(harness.PoolBuffer, 0u, data.data(), data.size()), "正しい引数なら積めるはず");
        Expect(uploader.GetStats().RingUsedBytes > 0u, "積むとリングを使うはず");
        Expect(uploader.DiscardLastEnqueued(1u) == 1u, "積んだ直後なら取り消せるはず");
        Expect(uploader.GetStats().RingUsedBytes == 0u, "取り消すとリングの区画が戻るはず");
        uploader.Clear();
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

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Test/Core/Rendering/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }

        {
            Harness harness;
            harness.Device = device;
            ShaderPtr shader = shaderManager.LoadShader("gpu_upload_ring_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << "gpu_upload_ring_probe.comp を読み込めませんでした\n";
                return 1;
            }
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeProbeDescriptorSetDesc());
            harness.Pipeline = device->CreateComputePipeline(pipelineDesc);
            if (!harness.Pipeline)
            {
                std::cerr << "確認用のパイプラインを作れませんでした\n";
                return 1;
            }

            // 本番と同じ DeviceLocal の塊（頂点・インデックス・storage・転送先・BDA）から区画を借りる
            harness.Pool = MakeUnique<GeometryPool>(device, PoolBlockBytes);
            for (uint32_t region = 0; region < RegionCount; ++region)
            {
                harness.Regions.push_back(harness.Pool->Allocate(RegionBytes, 256u));
                if (!harness.Regions.back().IsValid())
                {
                    std::cerr << "プールの区画を借りられませんでした\n";
                    return 1;
                }
            }
            for (uint32_t region = 1; region < RegionCount; ++region)
            {
                Expect(harness.Regions[region].GetBuffer() == harness.Regions[0].GetBuffer(),
                       "区画は同じ塊のバッファの中に取れるはず");
            }
            harness.PoolBuffer = harness.Regions[0].GetBufferHandle();

            for (uint32_t slot = 0; slot < SlotCount; ++slot)
            {
                BufferDesc resultDesc;
                resultDesc.Size = SlotResultBytes;
                resultDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
                resultDesc.DebugName = "GpuUploadRingResult";
                harness.Result[slot] = device->CreateBuffer(resultDesc);
                harness.Readback[slot] = device->CreateBuffer(
                    BufferDesc(SlotResultBytes, ResourceUsage::TransferDst, true, "GpuUploadRingReadback"));
                if (!harness.Result[slot] || !harness.Readback[slot])
                {
                    std::cerr << "読み戻しの資源を作れませんでした\n";
                    return 1;
                }
            }

            Shadow shadow;
            TestContinuousFrames(harness, shadow);
            TestFinalContents(harness, shadow);
            TestSharedLimitWithTexture(harness);
            TestOverlappingBufferWrites(harness);
            TestAbandonBufferRange(harness);
            TestRejectedArguments(harness);

            device->WaitIdle();
            for (uint32_t slot = 0; slot < SlotCount; ++slot)
            {
                harness.SlotSets[slot].clear();
                harness.Readback[slot].reset();
                harness.Result[slot].reset();
            }
            harness.PoolBuffer.reset();
            harness.Regions.clear();
            harness.Pool.reset();
            harness.Pipeline.reset();
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
