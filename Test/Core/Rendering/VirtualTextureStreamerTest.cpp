// VT のストリーマ（VirtualTextureStreamer）の契約テスト。
// 要求の集合から未常駐のタイルを優先度順に選んで読み、ページを結び、コピーを積んで常駐させること、
// 1 フレームの上限（読み・結び付け・コピーの数と量。ミップテイルも同じ予算）、同じタイルの二重の要求、
// 読み込みの失敗と再試行、結び付けの前にコピーを積むこと（結んだページが未コピーのまま残らない）、
// 結び付けの失敗・コピーを積めないとき・プールが尽きたときの扱い、解除したページの遅延返却と
// 解除したテクスチャ宛てのコピーの無効化、要求の件数による優先度を、読み込みも GPU も偽物にして確かめる。
#include "Asset/CookedTextureFormat.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/VirtualTextureRequestSet.h"
#include "Rendering/VirtualTextureStreamer.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"
#include "RHI/ITexture.h"

#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Container::TSharedPtr;
using Core::Container::VariableArray;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::IVirtualTextureGpu;
using Core::Rendering::IVirtualTextureTileSource;
using Core::Rendering::SparsePagePool;
using Core::Rendering::VirtualTextureFrameResult;
using Core::Rendering::VirtualTextureRegistration;
using Core::Rendering::VirtualTextureRequestSet;
using Core::Rendering::VirtualTextureStreamer;
using Core::Rendering::VirtualTextureStreamerConfig;
using Core::Rendering::VirtualTextureTileKey;
using Core::Rendering::VirtualTextureTileReadResult;
using Core::Rendering::VirtualTextureTileState;
namespace Asset = Core::Asset;

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "VirtualTextureStreamerTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

// ---- 偽物: テクスチャ・デバイス ----

constexpr Asset::CookedTexturePixelFormat TestFormat = Asset::CookedTexturePixelFormat::RGBA8UNorm;
// 1024x512 の RGBA8: タイルは 128x128。ミップ 0 から 2 がタイル（8x4・4x2・2x1）、ミップ 3 以降がミップテイル
constexpr uint32_t TestWidth = 1024;
constexpr uint32_t TestHeight = 512;
constexpr uint32_t TestTileBytes = 128 * 128 * 4;

class FakeSparseTexture final : public RHI::ITexture
{
public:
    explicit FakeSparseTexture(bool bSparseTexture = true, uint32_t tileWidth = 128, uint32_t tileHeight = 128)
        : bSparse(bSparseTexture)
    {
        Info.TileWidth = tileWidth;
        Info.TileHeight = tileHeight;
        Info.TileSizeBytes = 65536;
        Info.MipLevels = Asset::ComputeCookedTextureFullMipCount(TestWidth, TestHeight);
        Info.MipTailFirstLevel = Asset::ComputeCookedTextureFirstTailMip(TestFormat, TestWidth, TestHeight);
        Info.MipTailSize = 65536;
        for (uint32_t mip = 0; mip < Info.MipTailFirstLevel; ++mip)
        {
            const bool bGridOk = Asset::ComputeCookedTextureTileGrid(TestFormat, TestWidth >> mip, TestHeight >> mip,
                                                                     Info.TilesX[mip], Info.TilesY[mip]);
            (void)bGridOk;
        }
    }

    uint32_t GetWidth() const override { return TestWidth; }
    uint32_t GetHeight() const override { return TestHeight; }
    uint32_t GetDepth() const override { return 1; }
    uint32_t GetMipLevels() const override { return Info.MipLevels; }
    uint32_t GetArraySize() const override { return 1; }
    RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
    RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::ShaderRead; }
    bool IsCubemap() const override { return false; }
    void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
    bool IsSparse() const override { return bSparse; }
    bool GetSparseInfo(RHI::SparseTextureInfo& outInfo) const override
    {
        if (!bSparse)
        {
            return false;
        }
        outInfo = Info;
        return true;
    }

    bool bSparse = true;
    RHI::SparseTextureInfo Info;
};

class FakeSparseBlock final : public RHI::ISparseMemoryBlock
{
public:
    explicit FakeSparseBlock(uint64_t sizeBytes) : SizeBytes(sizeBytes) {}
    uint64_t GetSizeBytes() const override { return SizeBytes; }
    uint64_t SizeBytes;
};

class FakeDevice final : public RHI::IDevice
{
public:
    RHI::BufferPtr CreateBuffer(const RHI::BufferDesc&) override { return {}; }
    RHI::TexturePtr CreateTexture(const RHI::TextureDesc&) override { return {}; }
    RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc&) override { return {}; }
    RHI::SparseMemoryBlockPtr CreateSparseMemoryBlock(uint64_t sizeBytes, const char*) override
    {
        return MakeShared<FakeSparseBlock>(sizeBytes);
    }
    RHI::ShaderPtr CreateShader(const RHI::ShaderDesc&) override { return {}; }
    RHI::CommandListPtr CreateCommandList() override { return {}; }
    RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc&) override { return {}; }
    RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc&) override { return {}; }
    RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc&) override { return {}; }
    RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&) override { return {}; }
    RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc&) override { return {}; }
    RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc&) override { return {}; }
    RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
    RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
    void WaitIdle() override {}
    RHI::API GetAPI() const override { return RHI::API::None; }
    const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
    Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4& projection, bool) const override
    {
        return projection;
    }

    RHI::DeviceCapabilities Capabilities;
};

// ---- 偽物: 読み込みの窓口 ----

class FakeSource final : public IVirtualTextureTileSource
{
public:
    bool BeginRead(const VirtualTextureTileKey& key) override
    {
        if (bFailBegin)
        {
            return false;
        }
        Started.push_back(key);
        if (bAutoComplete)
        {
            Complete(key, !bAutoFail, TestTileBytes);
        }
        return true;
    }

    void CollectCompleted(VariableArray<VirtualTextureTileReadResult>& out) override
    {
        for (VirtualTextureTileReadResult& result : Completed)
        {
            out.push_back(std::move(result));
        }
        Completed.clear();
    }

    // 読み込みの完了を積む（dataBytes が 0 のときは空のデータ）
    void Complete(const VirtualTextureTileKey& key, bool bSucceeded, size_t dataBytes)
    {
        VirtualTextureTileReadResult result;
        result.Key = key;
        result.bSucceeded = bSucceeded;
        if (bSucceeded)
        {
            result.Data.assign(dataBytes, static_cast<uint8_t>(0x10u + key.Mip));
        }
        Completed.push_back(std::move(result));
    }

    VariableArray<VirtualTextureTileKey> Started;
    VariableArray<VirtualTextureTileReadResult> Completed;
    bool bAutoComplete = true;
    bool bAutoFail = false;
    bool bFailBegin = false;
};

// ---- 偽物: 結び付けとコピーの窓口 ----

class FakeGpu final : public IVirtualTextureGpu
{
public:
    enum class EventKind : uint8_t
    {
        Bind,
        Init,
        Copy,
    };

    struct Event
    {
        EventKind Kind = EventKind::Bind;
        uint32_t Mip = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint64_t Bytes = 0;
        size_t Tiles = 0;
        size_t Tails = 0;
    };

    bool BindSparse(const RHI::SparseBindRequest& request) override
    {
        ++BindCalls;
        if (bFailBind)
        {
            return false;
        }
        // 結ぶタイルは、結ぶ前にコピーが積まれていること（結んだページが未コピーのまま描画から読めてはいけない）
        for (const RHI::SparseTileBind& tile : request.Tiles)
        {
            bool bStaged = false;
            for (size_t index = LastBindEventCount; index < Events.size(); ++index)
            {
                const Event& event = Events[index];
                if (event.Kind == EventKind::Copy && event.Mip == tile.MipLevel && event.X == tile.TileX &&
                    event.Y == tile.TileY)
                {
                    bStaged = true;
                    break;
                }
            }
            if (!bStaged)
            {
                ++UncopiedBindViolations;
            }
        }
        if (!request.MipTails.empty())
        {
            bool bInitStaged = false;
            for (size_t index = LastBindEventCount; index < Events.size(); ++index)
            {
                bInitStaged = bInitStaged || Events[index].Kind == EventKind::Init;
            }
            if (!bInitStaged)
            {
                ++UncopiedBindViolations;
            }
        }
        Event event;
        event.Kind = EventKind::Bind;
        event.Tiles = request.Tiles.size();
        event.Tails = request.MipTails.size();
        Events.push_back(event);
        LastBindEventCount = Events.size();
        for (const RHI::SparseTileBind& tile : request.Tiles)
        {
            BoundTiles.push_back({tile.MipLevel, tile.TileX, tile.TileY});
        }
        return true;
    }

    bool EnqueueInitialize(const RHI::TexturePtr&) override
    {
        Event event;
        event.Kind = EventKind::Init;
        Events.push_back(event);
        return true;
    }

    bool EnqueueTile(const RHI::TexturePtr&, const RHI::TextureRegionCopy& region, const void*, uint64_t bytes) override
    {
        if (CopyBudget == 0)
        {
            return false;
        }
        if (CopyBudget > 0)
        {
            --CopyBudget;
        }
        Event event;
        event.Kind = EventKind::Copy;
        event.Mip = region.MipLevel;
        event.X = region.OffsetX / 128;
        event.Y = region.OffsetY / 128;
        event.Bytes = bytes;
        Events.push_back(event);
        return true;
    }

    uint64_t GetCopyBytesAvailable() const override { return CopyBytesAvailable; }

    // 積んだ依頼（初期化・コピー）を後ろから count 件取り消す
    void DiscardEnqueued(uint32_t count) override
    {
        for (uint32_t i = 0; i < count && !Events.empty() && Events.back().Kind != EventKind::Bind; ++i)
        {
            Events.pop_back();
            ++DiscardedOps;
        }
    }

    void AbandonTexture(const RHI::TexturePtr& texture) override
    {
        if (texture != nullptr)
        {
            ++AbandonedCount;
        }
    }

    size_t CountEvents(EventKind kind) const
    {
        size_t count = 0;
        for (const Event& event : Events)
        {
            if (event.Kind == kind)
            {
                ++count;
            }
        }
        return count;
    }

    // タイルのコピー（ミップテイルを除く）の数
    size_t CountTileCopies() const
    {
        size_t count = 0;
        for (const Event& event : Events)
        {
            if (event.Kind == EventKind::Copy && event.Bytes == TestTileBytes)
            {
                ++count;
            }
        }
        return count;
    }

    struct BoundTile
    {
        uint32_t Mip;
        uint32_t X;
        uint32_t Y;
    };

    VariableArray<Event> Events;
    VariableArray<BoundTile> BoundTiles;
    int BindCalls = 0;
    bool bFailBind = false;
    // 積めるコピーの残り数（負は無制限、0 でリングが満杯のように積めない）
    int CopyBudget = -1;
    // 次の記録で確実にコピーできる量（アップローダの残りの量）
    uint64_t CopyBytesAvailable = ~0ull;
    // 直前の結び付けの後ろの位置（結ぶ前に積まれたコピーを数える起点）
    size_t LastBindEventCount = 0;
    int UncopiedBindViolations = 0;
    int DiscardedOps = 0;
    int AbandonedCount = 0;
};

// ---- 道具 ----

VirtualTextureTileKey MakeKey(uint32_t texture, uint32_t mip, uint32_t x, uint32_t y)
{
    VirtualTextureTileKey key;
    key.TextureIndex = texture;
    key.Mip = mip;
    key.X = x;
    key.Y = y;
    return key;
}

// ミップテイル（ミップ 3 以降）を行優先で詰めたデータ
VariableArray<uint8_t> MakeTailData()
{
    uint64_t total = 0;
    const uint32_t firstTail = Asset::ComputeCookedTextureFirstTailMip(TestFormat, TestWidth, TestHeight);
    const uint32_t mipCount = Asset::ComputeCookedTextureFullMipCount(TestWidth, TestHeight);
    for (uint32_t mip = firstTail; mip < mipCount; ++mip)
    {
        const uint32_t width = (TestWidth >> mip) == 0 ? 1 : (TestWidth >> mip);
        const uint32_t height = (TestHeight >> mip) == 0 ? 1 : (TestHeight >> mip);
        uint64_t rowBytes = 0;
        uint64_t rowCount = 0;
        const bool bLayoutOk = Asset::ComputeCookedTextureMipLayout(TestFormat, width, height, rowBytes, rowCount);
        (void)bLayoutOk;
        total += rowBytes * rowCount;
    }
    return VariableArray<uint8_t>(static_cast<size_t>(total), 0x5Au);
}

// 道具一式。ストリーマより長く生きる物（プール・窓口）を先に宣言する。
struct Harness
{
    explicit Harness(const VirtualTextureStreamerConfig& config = VirtualTextureStreamerConfig(),
                     uint64_t poolBlockBytes = SparsePagePool::DefaultBlockBytes,
                     uint64_t poolLimitBytes = 0,
                     bool bUseRetireQueue = false)
        : Device(MakeShared<FakeDevice>()),
          Pool(Device, poolBlockBytes),
          Streamer(Pool, Gpu, bUseRetireQueue ? &Retire : nullptr, config)
    {
        Pool.SetCapacityLimitBytes(poolLimitBytes);
    }

    // テクスチャを登録する。TextureIndex を返す
    uint32_t Register()
    {
        VirtualTextureRegistration registration;
        registration.Texture = MakeShared<FakeSparseTexture>();
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.Source = Source;
        registration.TailData = MakeTailData();
        return Streamer.RegisterTexture(std::move(registration));
    }

    // 1 フレーム進める
    VirtualTextureFrameResult Step(const VirtualTextureRequestSet* requests = nullptr)
    {
        return Streamer.Update(++Frame, requests);
    }

    // ミップテイルが常駐するまで進める（要求は渡さない）
    uint32_t RegisterAndMakeTailResident()
    {
        const uint32_t index = Register();
        Step();
        return index;
    }

    TSharedPtr<FakeDevice> Device;
    SparsePagePool Pool;
    GpuRetireQueue Retire;
    FakeGpu Gpu;
    TSharedPtr<FakeSource> Source = MakeShared<FakeSource>();
    VirtualTextureStreamer Streamer;
    uint64_t Frame = 0;
};

void AddRequest(VirtualTextureRequestSet& set,
                uint32_t texture,
                uint32_t mip,
                uint32_t x,
                uint32_t y,
                uint64_t frame,
                uint32_t hits = 1)
{
    set.Add(MakeKey(texture, mip, x, y), frame, hits);
}

// ---- テスト ----

void TestRegistrationRejectsInvalid()
{
    Harness h;
    {
        VirtualTextureRegistration registration;
        registration.Texture = MakeShared<FakeSparseTexture>(false);
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.Source = h.Source;
        registration.TailData = MakeTailData();
        Expect(h.Streamer.RegisterTexture(std::move(registration)) == VirtualTextureStreamer::InvalidIndex,
               "sparse でないテクスチャは登録できない");
    }
    {
        VirtualTextureRegistration registration;
        registration.Texture = MakeShared<FakeSparseTexture>(true, 64, 64);
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.Source = h.Source;
        registration.TailData = MakeTailData();
        Expect(h.Streamer.RegisterTexture(std::move(registration)) == VirtualTextureStreamer::InvalidIndex,
               "形式の標準ブロック形状と合わないタイルの大きさは登録できない");
    }
    {
        VirtualTextureRegistration registration;
        registration.Texture = MakeShared<FakeSparseTexture>();
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.Source = h.Source;
        registration.TailData = VariableArray<uint8_t>(10, 0u);
        Expect(h.Streamer.RegisterTexture(std::move(registration)) == VirtualTextureStreamer::InvalidIndex,
               "ミップテイルのデータの大きさが合わなければ登録できない");
    }
    {
        VirtualTextureRegistration registration;
        registration.Texture = MakeShared<FakeSparseTexture>();
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.TailData = MakeTailData();
        Expect(h.Streamer.RegisterTexture(std::move(registration)) == VirtualTextureStreamer::InvalidIndex,
               "読み込みの窓口が無ければ登録できない");
    }
    Expect(h.Streamer.GetStats().TextureCount == 0, "登録に失敗したテクスチャは数えない");
}

void TestMipTailBoundOnFirstUpdate()
{
    Harness h;
    const uint32_t index = h.Register();
    Expect(index != VirtualTextureStreamer::InvalidIndex, "登録できる");
    Expect(!h.Streamer.IsMipTailResident(index), "登録しただけではミップテイルは常駐しない");
    Expect(h.Gpu.BindCalls == 0, "登録の呼び出しでは結び付けを出さない（RenderThread の直列化の外）");

    // ミップテイルの常駐前の要求は取り込まない
    VirtualTextureRequestSet early;
    AddRequest(early, index, 2, 0, 0, 1);
    h.Step(&early);
    Expect(h.Streamer.IsMipTailResident(index), "最初の Update でミップテイルが常駐する");
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::None,
           "ミップテイルが常駐する前の要求は取り込まない");

    Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1, "ミップテイルの結び付けは 1 回");
    Expect(h.Gpu.Events.size() >= 3 && h.Gpu.Events.back().Kind == FakeGpu::EventKind::Bind &&
               h.Gpu.Events.back().Tails == 1 && h.Gpu.Events.back().Tiles == 0,
           "結ぶのはミップテイルの 1 ページ");
    Expect(h.Gpu.Events[0].Kind == FakeGpu::EventKind::Init, "結ぶ前に、コピーより先に初期化の遷移を積む");
    Expect(h.Gpu.UncopiedBindViolations == 0, "ミップテイルは初期化とコピーを積んでから結ぶ");
    size_t tailCopies = 0;
    for (size_t i = 1; i < h.Gpu.Events.size(); ++i)
    {
        if (h.Gpu.Events[i].Kind == FakeGpu::EventKind::Copy)
        {
            ++tailCopies;
        }
    }
    Expect(tailCopies == 8, "ミップ 3 から 10 までの 8 段をコピーする");
    Expect(h.Pool.GetStats().UsedBytes == SparsePagePool::PageSizeBytes, "ミップテイルは 1 ページを借りる");

    h.Step();
    Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1, "ミップテイルを二度結ばない");
    Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Init) == 1, "初期化の遷移を二度積まない");
}

void TestPriorityCoarseMipFirst()
{
    VirtualTextureStreamerConfig config;
    config.MaxReadsStartedPerFrame = 2;
    Harness h(config);
    const uint32_t index = h.RegisterAndMakeTailResident();

    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 1, 1, 100); // 細かいミップは後
    AddRequest(requests, index, 0, 2, 2, 105);
    AddRequest(requests, index, 1, 0, 0, 90);
    AddRequest(requests, index, 2, 0, 0, 80); // 粗いミップは最近でなくても先
    AddRequest(requests, index, 2, 1, 0, 95);
    h.Step(&requests);

    Expect(h.Source->Started.size() == 2, "1 フレームに始める読み込みは上限の 2 件");
    Expect(h.Source->Started[0] == MakeKey(index, 2, 1, 0), "最初は最も粗いミップのうち最近要求されたもの");
    Expect(h.Source->Started[1] == MakeKey(index, 2, 0, 0), "次は同じ最も粗いミップのもう 1 つ");

    h.Step();
    Expect(h.Source->Started.size() == 4, "次のフレームで次の 2 件を始める");
    Expect(h.Source->Started[2] == MakeKey(index, 1, 0, 0), "次はミップ 1");
    Expect(h.Source->Started[3] == MakeKey(index, 0, 2, 2), "細かいミップでは最近要求されたものが先");

    h.Step();
    Expect(h.Source->Started.size() == 5 && h.Source->Started[4] == MakeKey(index, 0, 1, 1), "最後に残りの 1 件");
}

void TestReadAndBindLifecycle()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();
    const size_t eventsAfterTail = h.Gpu.Events.size();

    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 1, 2, 1, 7);
    h.Step(&requests);
    Expect(h.Streamer.GetTileState(MakeKey(index, 1, 2, 1)) == VirtualTextureTileState::Reading, "要求したタイルの読み込みを始める");
    Expect(h.Gpu.Events.size() == eventsAfterTail, "読み込みの完了前は結ばない");

    const VirtualTextureFrameResult result = h.Step();
    Expect(result.TilesBound == 1 && result.CopiesEnqueued == 1 && result.CopiedBytes == TestTileBytes,
           "読み込みが済んだ次のフレームで結んでコピーする");
    Expect(h.Streamer.GetTileState(MakeKey(index, 1, 2, 1)) == VirtualTextureTileState::Resident, "常駐する");
    Expect(h.Gpu.BoundTiles.size() == 1 && h.Gpu.BoundTiles[0].Mip == 1 && h.Gpu.BoundTiles[0].X == 2 &&
               h.Gpu.BoundTiles[0].Y == 1,
           "要求したタイルを結ぶ");
    const size_t eventCount = h.Gpu.Events.size();
    Expect(eventCount >= 2 && h.Gpu.Events[eventCount - 1].Kind == FakeGpu::EventKind::Bind &&
               h.Gpu.Events[eventCount - 2].Kind == FakeGpu::EventKind::Copy && h.Gpu.Events[eventCount - 2].Mip == 1 &&
               h.Gpu.Events[eventCount - 2].X == 2 && h.Gpu.Events[eventCount - 2].Y == 1,
           "結んだタイルの領域へのコピーを、結ぶ前に積む");
    Expect(h.Gpu.UncopiedBindViolations == 0, "結ぶタイルは、結ぶ前にコピーを積んである");
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "ミップテイルとタイルで 2 ページ");
    Expect(h.Streamer.GetStats().ResidentTiles == 1, "常駐のタイルの数");
}

void TestDuplicateRequestsReadOnce()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();

    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 2, 0, 0, 5);
    h.Step(&requests);
    // 読み込み中・読み込み済みの間に同じ要求が何度来ても、二重に読まない
    for (int i = 0; i < 3; ++i)
    {
        VirtualTextureRequestSet again;
        AddRequest(again, index, 2, 0, 0, 6 + i);
        h.Step(&again);
    }
    // 常駐した後の要求でも読み直さない・結び直さない
    VirtualTextureRequestSet resident;
    AddRequest(resident, index, 2, 0, 0, 20);
    h.Step(&resident);
    h.Step(&resident);

    Expect(h.Source->Started.size() == 1, "同じタイルは 1 回しか読まない");
    Expect(h.Gpu.BoundTiles.size() == 1, "同じタイルは 1 回しか結ばない");
    Expect(h.Gpu.CountTileCopies() == 1, "同じタイルは 1 回しかコピーしない");
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Resident, "常駐のまま");

    // 1 つの集合の中で同じタイルを何度足しても 1 つ
    VirtualTextureRequestSet single;
    AddRequest(single, index, 1, 0, 0, 1);
    AddRequest(single, index, 1, 0, 0, 2);
    h.Step(&single);
    Expect(h.Source->Started.size() == 2, "集合の中の重複は 1 つにまとまる");
}

void TestPerFrameLimits()
{
    // 読み込み中と読み込み済みの合計の上限
    {
        VirtualTextureStreamerConfig config;
        config.MaxReadsStartedPerFrame = 100;
        config.MaxReadsInFlight = 3;
        Harness h(config);
        const uint32_t index = h.RegisterAndMakeTailResident();
        h.Gpu.CopyBytesAvailable = 0; // コピーを積めない間は結ばないので、読み込み済みのタイルが溜まる
        VirtualTextureRequestSet requests;
        for (uint32_t x = 0; x < 6; ++x)
        {
            AddRequest(requests, index, 0, x, 0, 1);
        }
        h.Step(&requests);
        Expect(h.Source->Started.size() == 3, "読み込み中と読み込み済みの合計の上限で始める数が決まる");
        h.Step();
        h.Step();
        Expect(h.Source->Started.size() == 3, "上限に達している間は追加で始めない");
        Expect(h.Streamer.GetStats().ReadyTiles == 3, "読み込み済みで結ぶのを待つタイル");
    }
    // 1 フレームに結ぶタイルの数の上限
    {
        VirtualTextureStreamerConfig config;
        config.MaxBindsPerFrame = 2;
        Harness h(config);
        const uint32_t index = h.RegisterAndMakeTailResident();
        VirtualTextureRequestSet requests;
        for (uint32_t x = 0; x < 5; ++x)
        {
            AddRequest(requests, index, 0, x, 0, 1);
        }
        h.Step(&requests); // 読み込み開始（5 件）
        const VirtualTextureFrameResult first = h.Step();
        Expect(first.TilesBound == 2 && first.CopiesEnqueued == 2, "1 フレームに結ぶのは上限の 2 タイル");
        const VirtualTextureFrameResult second = h.Step();
        Expect(second.TilesBound == 2, "次のフレームでも上限の 2 タイル");
        const VirtualTextureFrameResult third = h.Step();
        Expect(third.TilesBound == 1, "残りの 1 タイル");
        Expect(h.Streamer.GetStats().ResidentTiles == 5, "最終的に全部が常駐する");
    }
    // 1 フレームにコピーする量の上限
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = 2ull * TestTileBytes + 100;
        Harness h(config);
        const uint32_t index = h.RegisterAndMakeTailResident();
        VirtualTextureRequestSet requests;
        for (uint32_t x = 0; x < 5; ++x)
        {
            AddRequest(requests, index, 0, x, 0, 1);
        }
        h.Step(&requests);
        const VirtualTextureFrameResult first = h.Step();
        Expect(first.TilesBound == 2 && first.CopiedBytes == 2ull * TestTileBytes, "コピーの量の上限に収まる 2 タイルだけ");
    }
}

void TestPoolExhaustedKeepsTilesReady()
{
    // 4 ページだけ持てるプール: ミップテイルで 1 ページ、タイルに 3 ページ
    Harness h(VirtualTextureStreamerConfig(), 4 * SparsePagePool::PageSizeBytes, 4 * SparsePagePool::PageSizeBytes);
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    for (uint32_t x = 0; x < 5; ++x)
    {
        AddRequest(requests, index, 0, x, 0, 1);
    }
    h.Step(&requests);
    const VirtualTextureFrameResult result = h.Step();
    Expect(result.TilesBound == 3, "プールの空きの数だけ結ぶ");
    Expect(h.Streamer.GetStats().ReadyTiles == 2, "結べなかったタイルは読み込み済みのまま待つ");
    Expect(h.Streamer.GetStats().PoolExhaustedFrames >= 1, "プールが尽きたフレームを数える");
    h.Step();
    Expect(h.Streamer.GetStats().ResidentTiles == 3, "空きが無い間は増えない");
}

void TestReadFailureAndRetry()
{
    VirtualTextureStreamerConfig config;
    config.MaxRetries = 2;
    config.RetryDelayFrames = 3;
    Harness h(config);
    const uint32_t index = h.RegisterAndMakeTailResident();
    h.Source->bAutoFail = true;

    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    h.Step(&requests);
    h.Step(); // 失敗が返る
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Failed, "読み込みの失敗は Failed になる");
    Expect(h.Gpu.BoundTiles.empty(), "失敗したタイルは結ばない");
    Expect(h.Streamer.GetStats().ReadsFailed == 1, "失敗を数える");

    // 待ちの間は、要求が来ても読み直さない
    h.Step(&requests);
    Expect(h.Source->Started.size() == 1, "再試行の待ちの間は読み直さない");

    // 待ちが過ぎた後の要求で読み直す。今度も失敗して、諦める
    for (int i = 0; i < 4; ++i)
    {
        h.Step();
    }
    h.Step(&requests);
    h.Step();
    Expect(h.Source->Started.size() == 2, "待ちが過ぎたら次の要求で読み直す");
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Failed, "2 回目も失敗する");
    Expect(h.Streamer.GetStats().PermanentFailures == 1, "上限の回数失敗したら諦めたと数える");
    for (int i = 0; i < 20; ++i)
    {
        h.Step(&requests);
    }
    Expect(h.Source->Started.size() == 2, "諦めたタイルは読み直さない");

    // 別のタイルは影響を受けず、成功すれば常駐する
    h.Source->bAutoFail = false;
    VirtualTextureRequestSet other;
    AddRequest(other, index, 0, 3, 3, 1);
    h.Step(&other);
    h.Step();
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 3, 3)) == VirtualTextureTileState::Resident, "失敗は他のタイルに影響しない");
}

void TestWrongSizeAndBeginFailureAreFailures()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();

    // 大きさが期待と違うデータは使わない（コピーが領域をはみ出すため）
    h.Source->bAutoComplete = false;
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    h.Step(&requests);
    h.Source->Complete(MakeKey(index, 0, 0, 0), true, 1000);
    h.Step();
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Failed, "大きさの違うデータは失敗として扱う");
    Expect(h.Gpu.BoundTiles.empty(), "大きさの違うデータのタイルは結ばない");

    // 読み込みを始められないときも失敗として扱う
    h.Source->bFailBegin = true;
    VirtualTextureRequestSet second;
    AddRequest(second, index, 0, 1, 0, 1);
    h.Step(&second);
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 1, 0)) == VirtualTextureTileState::Failed, "読み込みを始められなければ失敗");
}

void TestBindFailureReturnsPages()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    AddRequest(requests, index, 0, 1, 0, 1);
    h.Step(&requests);

    h.Gpu.bFailBind = true;
    const VirtualTextureFrameResult failed = h.Step();
    Expect(failed.TilesBound == 0, "結び付けに失敗したフレームは結べない");
    Expect(h.Gpu.CountTileCopies() == 0 && h.Gpu.DiscardedOps == 2,
           "結び付けに失敗したら、結ぶ前に積んだコピーを取り消す");
    Expect(h.Streamer.GetStats().BindFailures == 1, "結び付けの失敗を数える");
    Expect(h.Streamer.GetStats().ReadyTiles == 2, "タイルは読み込み済みのまま残る");
    Expect(h.Pool.GetStats().UsedBytes == SparsePagePool::PageSizeBytes, "借りたページはプールへ返る（ミップテイルの 1 ページだけ）");

    h.Gpu.bFailBind = false;
    const VirtualTextureFrameResult ok = h.Step();
    Expect(ok.TilesBound == 2, "次のフレームで結び直す");
    Expect(h.Streamer.GetStats().ResidentTiles == 2, "常駐する");
    Expect(h.Gpu.CountTileCopies() == 2 && h.Gpu.UncopiedBindViolations == 0, "取り消したコピーは積み直され、二重にならない");
}

void TestCopyNotEnqueuedKeepsPagesUnbound()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    AddRequest(requests, index, 0, 1, 0, 1);
    h.Step(&requests);

    // リングが満杯でコピーを積めない: コピーされないページを結ばない（結ぶと描画が未初期化のデータを読む）
    h.Gpu.CopyBudget = 0;
    const size_t boundBefore = h.Gpu.BoundTiles.size();
    const VirtualTextureFrameResult blocked = h.Step();
    Expect(blocked.TilesBound == 0 && blocked.CopiesEnqueued == 0, "コピーを積めないフレームは結ばない");
    Expect(h.Gpu.BoundTiles.size() == boundBefore, "コピーを積めないタイルのページは結ばない");
    Expect(h.Streamer.GetStats().ReadyTiles == 2, "読み込み済みのまま次のフレームを待つ");
    Expect(h.Streamer.GetStats().ResidentTiles == 0, "コピーが積まれるまで常駐とは数えない");
    Expect(h.Streamer.GetStats().CopyBlockedFrames == 1, "コピーを積めなかったフレームを数える");
    Expect(h.Pool.GetStats().UsedBytes == SparsePagePool::PageSizeBytes, "借りたページは返る（ミップテイルの 1 ページだけ）");

    // 新しい要求が来ても同じ
    VirtualTextureRequestSet more;
    AddRequest(more, index, 0, 2, 0, 1);
    h.Step(&more);
    h.Step();
    Expect(h.Gpu.BoundTiles.size() == boundBefore, "積めない間は新しいタイルも結ばない");

    // 積めるようになったら結んで常駐する
    h.Gpu.CopyBudget = -1;
    h.Step();
    h.Step();
    Expect(h.Streamer.GetStats().ResidentTiles == 3, "3 タイルとも常駐する");
    Expect(h.Gpu.CountTileCopies() == 3, "各タイルのコピーは 1 回ずつ");
    Expect(h.Gpu.UncopiedBindViolations == 0, "結んだタイルは、結ぶ前にコピーを積んである");
}

void TestPartialStagingBindsOnlyCopiedTiles()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 5);
    AddRequest(requests, index, 0, 1, 0, 4);
    AddRequest(requests, index, 0, 2, 0, 3);
    h.Step(&requests);

    // コピーを 1 件だけ積めるとき、積めたタイルだけを結び、残りは次のフレームへ
    h.Gpu.CopyBudget = 1;
    const VirtualTextureFrameResult first = h.Step();
    Expect(first.TilesBound == 1 && first.CopiesEnqueued == 1, "積めた 1 タイルだけ結ぶ");
    Expect(h.Gpu.BoundTiles.size() == 1 && h.Gpu.BoundTiles[0].X == 0, "優先度の高いタイルから結ぶ");
    Expect(h.Streamer.GetStats().ReadyTiles == 2, "残りは読み込み済みのまま待つ");

    h.Gpu.CopyBudget = -1;
    h.Step();
    Expect(h.Streamer.GetStats().ResidentTiles == 3, "積めるようになれば残りも常駐する");
    Expect(h.Gpu.UncopiedBindViolations == 0, "結んだタイルは、結ぶ前にコピーを積んである");
}

void TestInvalidRequestsIgnored()
{
    Harness h;
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 8, 0, 1);  // ミップ 0 は 8x4 タイルなので x=8 は範囲外
    AddRequest(requests, index, 0, 0, 4, 1);  // y=4 も範囲外
    AddRequest(requests, index, 2, 2, 0, 1);  // ミップ 2 は 2x1 なので x=2 は範囲外
    AddRequest(requests, index, 3, 0, 0, 1);  // ミップ 3 以降はミップテイル（常駐済み）
    AddRequest(requests, index, 15, 0, 0, 1); // ミップ数を超える
    AddRequest(requests, index + 5, 0, 0, 0, 1); // 登録の無いテクスチャ
    h.Step(&requests);
    h.Step();
    Expect(h.Source->Started.empty(), "範囲外・ミップテイル・未登録のテクスチャの要求は読み込まない");
    Expect(h.Streamer.GetStats().InvalidRequests == 5,
           "範囲外 3 件・ミップ数の超過 1 件・未登録 1 件を不正な要求に数える（ミップテイルは数えない）");
}

void TestStaleWantedDropped()
{
    VirtualTextureStreamerConfig config;
    config.MaxReadsStartedPerFrame = 0; // 読み込みを始めずに Wanted のまま置く
    config.WantedMaxAgeFrames = 5;
    Harness h(config);
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    h.Step(&requests);
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Wanted, "読み込めない間は Wanted のまま");
    for (int i = 0; i < 4; ++i)
    {
        h.Step();
    }
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Wanted, "期限内は覚えている");
    for (int i = 0; i < 3; ++i)
    {
        h.Step();
    }
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::None, "要求が途絶えたまま期限を過ぎたら忘れる");
    Expect(h.Streamer.GetStats().StaleDropped == 1, "忘れた数を数える");
}

void TestTailSharesFrameBudget()
{
    const uint64_t tailBytes = MakeTailData().size();

    // 結び付けの数: 1 フレームに結ぶのは 1 件で、ミップテイルも数える
    {
        VirtualTextureStreamerConfig config;
        config.MaxBindsPerFrame = 1;
        config.MaxCopyBytesPerFrame = 65536;
        Harness h(config);
        const uint32_t first = h.Register();
        const uint32_t second = h.Register();
        const VirtualTextureFrameResult one = h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1 && h.Gpu.Events.back().Tails == 1,
               "1 フレームに結ぶミップテイルは上限の 1 枚");
        Expect(one.CopiesEnqueued == 8 && one.CopiedBytes == tailBytes, "結んだ 1 枚の分だけコピーする");
        Expect(h.Streamer.IsMipTailResident(first) && !h.Streamer.IsMipTailResident(second), "後のテクスチャは次のフレームへ");
        h.Step();
        Expect(h.Streamer.IsMipTailResident(second), "次のフレームで 2 枚目のミップテイルを結ぶ");
        Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "2 枚で 2 ページ");
    }
    // コピーの量: 2 枚目を足すと上限を超えるので次のフレームへ
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = tailBytes + tailBytes / 2;
        Harness h(config);
        h.Register();
        h.Register();
        const VirtualTextureFrameResult one = h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1 && one.CopiedBytes == tailBytes,
               "コピーの量の上限を超えるミップテイルは次のフレームへ");
        h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 2, "次のフレームで結ぶ");
    }
    // コピーの数: ミップテイルは段ごとに 1 件で数える
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 8;
        Harness h(config);
        h.Register();
        h.Register();
        const VirtualTextureFrameResult one = h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1 && one.CopiesEnqueued == 8,
               "コピーの数の上限を超えるミップテイルは次のフレームへ");
    }
    // 1 件が上限より大きくても、そのフレームの最初の 1 件は通す（永久に進まなくなるのを防ぐ）
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = 100;
        config.MaxCopiesPerFrame = 1;
        Harness h(config);
        const uint32_t index = h.Register();
        h.Step();
        Expect(h.Streamer.IsMipTailResident(index), "上限より大きくても、最初の 1 件のミップテイルは結ぶ");
    }
    // 結び付けの数を 0 にすると、ミップテイルも結ばない
    {
        VirtualTextureStreamerConfig config;
        config.MaxBindsPerFrame = 0;
        Harness h(config);
        const uint32_t index = h.Register();
        h.Step();
        Expect(!h.Streamer.IsMipTailResident(index) && h.Gpu.BindCalls == 0, "結び付けの上限が 0 なら何も結ばない");
    }
}

void TestUploaderAvailabilityLimitsStaging()
{
    const uint64_t tailBytes = MakeTailData().size();

    // タイルのコピーは、アップローダが確実に記録できる量までしか積まない
    {
        Harness h;
        const uint32_t index = h.RegisterAndMakeTailResident();
        VirtualTextureRequestSet requests;
        for (uint32_t x = 0; x < 4; ++x)
        {
            AddRequest(requests, index, 0, x, 0, 1);
        }
        h.Step(&requests);

        h.Gpu.CopyBytesAvailable = 2ull * TestTileBytes;
        const VirtualTextureFrameResult first = h.Step();
        Expect(first.TilesBound == 2 && first.CopiedBytes == 2ull * TestTileBytes, "記録できる量の 2 タイルだけ積む");
        h.Gpu.CopyBytesAvailable = TestTileBytes - 1;
        Expect(h.Step().TilesBound == 0, "1 タイルに足りない量では積まない");
        Expect(h.Streamer.GetStats().ReadyTiles == 2, "積めなかったタイルは読み込み済みのまま待つ");
        h.Gpu.CopyBytesAvailable = ~0ull;
        h.Step();
        Expect(h.Streamer.GetStats().ResidentTiles == 4, "量が空いたら残りも常駐する");
    }
    // ミップテイルとタイルは同じ残りを分け合う
    {
        Harness h;
        const uint32_t first = h.RegisterAndMakeTailResident();
        VirtualTextureRequestSet requests;
        AddRequest(requests, first, 0, 0, 0, 1);
        AddRequest(requests, first, 0, 1, 0, 1);
        h.Step(&requests);
        const uint32_t second = h.Register();

        h.Gpu.CopyBytesAvailable = tailBytes + TestTileBytes;
        const VirtualTextureFrameResult result = h.Step();
        Expect(h.Streamer.IsMipTailResident(second) && result.TilesBound == 1, "2 枚目のミップテイルと 1 タイルを積む");
        Expect(result.CopiedBytes == tailBytes + TestTileBytes, "ミップテイルの分を引いた残りで、タイルを積む");
        Expect(h.Gpu.UncopiedBindViolations == 0, "結んだものは、結ぶ前にコピーを積んである");
    }
}

void TestUnregisterAbandonsPendingCopies()
{
    Harness h;
    const uint32_t first = h.RegisterAndMakeTailResident();
    const uint32_t second = h.Register();
    h.Step();
    Expect(h.Gpu.AbandonedCount == 0, "登録中は無効にしない");
    h.Streamer.UnregisterTexture(first);
    Expect(h.Gpu.AbandonedCount == 1, "解除したテクスチャ宛ての出していない依頼を無効にする");
    h.Streamer.UnregisterTexture(first);
    Expect(h.Gpu.AbandonedCount == 1, "解除済みの番号を再び解除しても何もしない");
    h.Streamer.Clear();
    Expect(h.Gpu.AbandonedCount == 2 && second != first, "全部の解除でも、残りのテクスチャ宛ての依頼を無効にする");
}

void TestPriorityPrefersMoreHitsInSameMip()
{
    VirtualTextureStreamerConfig config;
    config.MaxReadsStartedPerFrame = 1;
    Harness h(config);
    const uint32_t index = h.RegisterAndMakeTailResident();

    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 1, 0, 0, 90, 1);   // 最近だが、画面に占める量が少ない
    AddRequest(requests, index, 1, 1, 0, 10, 50);  // 古いが、画面で目立つ
    AddRequest(requests, index, 1, 0, 1, 20, 50);  // 同じ件数なら最近のものが先
    AddRequest(requests, index, 2, 0, 0, 1, 1);    // 粗いミップは件数が少なくても先
    h.Step(&requests);
    for (int i = 0; i < 4; ++i)
    {
        h.Step();
    }

    Expect(h.Source->Started.size() == 4, "4 件を 1 フレームに 1 件ずつ始める");
    Expect(h.Source->Started[0] == MakeKey(index, 2, 0, 0), "最も粗いミップが先");
    Expect(h.Source->Started[1] == MakeKey(index, 1, 0, 1), "同じミップでは件数が多いものが先、同数なら最近のものが先");
    Expect(h.Source->Started[2] == MakeKey(index, 1, 1, 0), "件数が同じなら最近のものが先");
    Expect(h.Source->Started[3] == MakeKey(index, 1, 0, 0), "件数が少ないものは最近でも後");
}

void TestUnregisterRetiresPages()
{
    Harness h(VirtualTextureStreamerConfig(), SparsePagePool::DefaultBlockBytes, 0, true);
    h.Retire.BeginFrame(0);
    const uint32_t index = h.RegisterAndMakeTailResident();
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 0, 0, 0, 1);
    h.Step(&requests);
    h.Step();
    h.Retire.CommitFrame(5);
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "ミップテイルとタイルの 2 ページを借りている");

    h.Streamer.UnregisterTexture(index);
    Expect(h.Streamer.GetStats().TextureCount == 0, "解除したテクスチャは数えない");
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes,
           "外したページは最後に使った提出の完了までプールへ戻らない");
    h.Retire.BeginFrame(4);
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "serial 4 の完了ではまだ戻らない");
    h.Retire.BeginFrame(5);
    Expect(h.Pool.GetStats().UsedBytes == 0, "serial 5 が完了したらプールへ戻る");

    // 解除済みのテクスチャの要求は不正として数え、番号は使い回される
    VirtualTextureRequestSet stale;
    AddRequest(stale, index, 0, 0, 0, 1);
    h.Step(&stale);
    Expect(h.Streamer.GetStats().InvalidRequests >= 1, "解除したテクスチャへの要求は不正な要求");
    const uint32_t second = h.Register();
    Expect(second != VirtualTextureStreamer::InvalidIndex, "解除後に再び登録できる");
    Expect(h.Streamer.GetTileState(MakeKey(second, 0, 0, 0)) == VirtualTextureTileState::None, "新しいテクスチャは空の状態から始まる");
}

void TestClearReleasesEverything()
{
    Harness h;
    const uint32_t first = h.RegisterAndMakeTailResident();
    const uint32_t second = h.Register();
    h.Step();
    Expect(first != second, "別々の番号を割り当てる");
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "2 枚のミップテイルで 2 ページ");
    h.Streamer.Clear();
    Expect(h.Streamer.GetStats().TextureCount == 0, "Clear で登録が全部消える");
    Expect(h.Pool.GetStats().UsedBytes == 0, "retireQueue が無ければ即座にプールへ戻る");
}

int RunTest()
{
    TestRegistrationRejectsInvalid();
    TestMipTailBoundOnFirstUpdate();
    TestPriorityCoarseMipFirst();
    TestReadAndBindLifecycle();
    TestDuplicateRequestsReadOnce();
    TestPerFrameLimits();
    TestPoolExhaustedKeepsTilesReady();
    TestReadFailureAndRetry();
    TestWrongSizeAndBeginFailureAreFailures();
    TestBindFailureReturnsPages();
    TestCopyNotEnqueuedKeepsPagesUnbound();
    TestPartialStagingBindsOnlyCopiedTiles();
    TestTailSharesFrameBudget();
    TestUploaderAvailabilityLimitsStaging();
    TestPriorityPrefersMoreHitsInSameMip();
    TestInvalidRequestsIgnored();
    TestStaleWantedDropped();
    TestUnregisterRetiresPages();
    TestUnregisterAbandonsPendingCopies();
    TestClearReleasesEverything();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VirtualTextureStreamerTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
