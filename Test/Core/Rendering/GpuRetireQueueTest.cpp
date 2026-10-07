// GPU 資源の遅延解放キュー（GpuRetireQueue）の契約テスト。
// sparse の物理メモリのページ（SparsePagePool）が、貸し出し・返却・増設・上限のとおりに動き、
// 外したページが最後に使った提出の serial の完了までプールへ戻らないことも確かめる。
// 提出の serial が完了するまで RHI 資源が破棄されないこと、完了したら破棄されること、
// 記録中のフレームで頼まれた解放はそのフレームの serial まで延びること、Shutdown で全部破棄されることを、
// GPU を使わない偽デバイスで確かめる。
// VT（仮想テクスチャ）が使えるかは、sparse の結び付け・2D の部分常駐・常駐の照会・フラグメントからの storage buffer の
// 書き込みの4つがそろうデバイスだけで、要求のバッファを確保して有効にできないときは VT を解放して失敗を返す（全常駐へ戻る）。
#include "Asset/AssetManifest.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Container/VariableArray.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/RenderResources.h"
#include "Rendering/VirtualTextureFeedbackRing.h"
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

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Rendering::BufferCreateInfo;
using Core::Rendering::BufferHandle;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::RenderResources;
using Core::Rendering::SparsePagePool;
using Core::Rendering::TextureCreateInfo;
using Core::Rendering::TextureHandle;
namespace Asset = Core::Asset;
namespace PackageV1 = Core::Asset::AssetPackageFormatV1;
namespace TextureV0 = Core::Asset::CookedTextureFormatV0;

int g_failures = 0;
// 生きている偽資源の数（破棄されたかどうかの観測に使う）
int g_liveTextures = 0;
int g_liveBuffers = 0;
int g_liveSparseBlocks = 0;
// true の間、偽デバイスは sparse の塊を作れない
bool g_failSparseBlockCreation = false;
// true の間、偽デバイスは VT の要求のバッファ（CPU から常時写像する storage buffer）を作れない
bool g_failFeedbackBufferCreation = false;
// true の間、偽デバイスの VT の要求のバッファは CPU から写像できる（false では Map が null を返す）
bool g_mapFeedbackBuffers = false;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "GpuRetireQueueTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

class RetireFakeTexture final : public RHI::ITexture
{
public:
    explicit RetireFakeTexture(const RHI::TextureDesc& desc)
        : Desc(desc)
    {
        ++g_liveTextures;
    }
    ~RetireFakeTexture() override { --g_liveTextures; }

    uint32_t GetWidth() const override { return Desc.Width; }
    uint32_t GetHeight() const override { return Desc.Height; }
    uint32_t GetDepth() const override { return Desc.Depth; }
    uint32_t GetMipLevels() const override { return Desc.MipLevels; }
    uint32_t GetArraySize() const override { return Desc.ArraySize; }
    RHI::Format GetFormat() const override { return Desc.TextureFormat; }
    RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
    bool IsCubemap() const override { return Desc.IsCubemap; }
    void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
    bool IsSparse() const override { return Desc.bSparse; }

    // sparse の偽テクスチャは R8 の標準ブロック形状（256x256 の 64 KiB）として答える
    bool GetSparseInfo(RHI::SparseTextureInfo& outInfo) const override
    {
        if (!Desc.bSparse)
        {
            return false;
        }
        constexpr Asset::CookedTexturePixelFormat Format = Asset::CookedTexturePixelFormat::R8UNorm;
        RHI::SparseTextureInfo info;
        info.TileWidth = 256;
        info.TileHeight = 256;
        info.TileSizeBytes = 65536;
        info.MipLevels = Desc.MipLevels;
        info.MipTailFirstLevel = Asset::ComputeCookedTextureFirstTailMip(Format, Desc.Width, Desc.Height);
        info.MipTailSize = 65536;
        for (uint32_t mip = 0; mip < info.MipTailFirstLevel; ++mip)
        {
            if (!Asset::ComputeCookedTextureTileGrid(Format, Desc.Width >> mip, Desc.Height >> mip, info.TilesX[mip],
                                                     info.TilesY[mip]))
            {
                return false;
            }
        }
        outInfo = info;
        return true;
    }

    RHI::TextureDesc Desc;
};

class RetireFakeBuffer final : public RHI::IBuffer
{
public:
    RetireFakeBuffer(uint64_t size, RHI::ResourceUsage usage)
        : Size(size),
          Usage(usage)
    {
        ++g_liveBuffers;
    }
    ~RetireFakeBuffer() override { --g_liveBuffers; }

    uint64_t GetSize() const override { return Size; }
    void* Map(uint64_t, uint64_t) override { return Backing.empty() ? nullptr : Backing.data(); }
    void Unmap() override {}
    void Update(const void*, uint64_t, uint64_t) override {}
    RHI::ResourceUsage GetUsage() const override { return Usage; }

    uint64_t Size;
    RHI::ResourceUsage Usage;
    // 写像できる偽バッファだけが持つ実体
    Core::Container::VariableArray<uint8_t> Backing;
};

class RetireFakeSampler final : public RHI::ISampler
{
public:
    explicit RetireFakeSampler(const RHI::SamplerDesc& desc)
        : Desc(desc)
    {
    }

    RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
    RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
    RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
    RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
    RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
    RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
    uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
    RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

    RHI::SamplerDesc Desc;
};

class RetireFakeSparseBlock final : public RHI::ISparseMemoryBlock
{
public:
    explicit RetireFakeSparseBlock(uint64_t sizeBytes)
        : SizeBytes(sizeBytes)
    {
        ++g_liveSparseBlocks;
    }
    ~RetireFakeSparseBlock() override { --g_liveSparseBlocks; }

    uint64_t GetSizeBytes() const override { return SizeBytes; }

    uint64_t SizeBytes;
};

class RetireFakeDevice final : public RHI::IDevice
{
public:
    RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
    {
        // VT の要求のバッファは、CPU から常時写像する storage buffer（代替のバッファは CPU から触らない）
        const bool bFeedbackBuffer = desc.Usage == RHI::ResourceUsage::StorageBuffer && desc.CPUAccessible;
        if (bFeedbackBuffer && g_failFeedbackBufferCreation)
        {
            return nullptr;
        }
        auto buffer = MakeShared<RetireFakeBuffer>(desc.Size, desc.Usage);
        if (bFeedbackBuffer && g_mapFeedbackBuffers)
        {
            buffer->Backing.assign(static_cast<size_t>(desc.Size), 0);
        }
        return buffer;
    }

    RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
    {
        return MakeShared<RetireFakeTexture>(desc);
    }

    RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
    {
        return MakeShared<RetireFakeSampler>(desc);
    }

    RHI::SparseMemoryBlockPtr CreateSparseMemoryBlock(uint64_t sizeBytes, const char*) override
    {
        if (g_failSparseBlockCreation)
        {
            return nullptr;
        }
        return MakeShared<RetireFakeSparseBlock>(sizeBytes);
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

RHI::TexturePtr MakeFakeTexture()
{
    RHI::TextureDesc desc;
    desc.Width = 4;
    desc.Height = 4;
    return MakeShared<RetireFakeTexture>(desc);
}

RHI::BufferPtr MakeFakeBuffer()
{
    return MakeShared<RetireFakeBuffer>(64, RHI::ResourceUsage::VertexBuffer);
}

void TestNothingSubmittedReleasesImmediately()
{
    // まだ何も提出していない（serial 0）なら、待つ相手がいないので即座に破棄する。
    GpuRetireQueue queue;
    queue.Retire(MakeFakeTexture());
    queue.Retire(MakeFakeBuffer());
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "未提出なら解放を頼んだ資源は即座に破棄される");
    Expect(queue.GetPendingCount() == 0, "未提出なら待ち行列は空のまま");

    // null は無視する。
    queue.Retire(RHI::TexturePtr{});
    queue.Retire(RHI::BufferPtr{});
    Expect(queue.GetPendingCount() == 0, "null は待ち行列に積まない");
}

void TestHeldUntilCompletedSerial()
{
    GpuRetireQueue queue;
    queue.BeginFrame(0);
    queue.CommitFrame(5);

    queue.Retire(MakeFakeTexture());
    queue.Retire(MakeFakeBuffer());
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "最後に提出した serial が完了するまで破棄されない");
    Expect(queue.GetPendingCount() == 2, "テクスチャとバッファの2件が待つ");

    queue.Collect(4);
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "完了の serial が届かない間は破棄されない");

    queue.Collect(5);
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "完了の serial が届いたら破棄される");
    Expect(queue.GetPendingCount() == 0, "破棄した分は待ち行列から消える");

    // 提出した serial がすべて完了済みなら、解放を頼んだ時点で即座に破棄される。
    queue.Retire(MakeFakeTexture());
    Expect(g_liveTextures == 0, "提出した serial がすべて完了済みなら即座に破棄される");
}

void TestRetiredDuringRecordingWaitsForThatFrame()
{
    GpuRetireQueue queue;
    queue.BeginFrame(0);
    queue.CommitFrame(5);
    queue.Collect(5);

    // 次のフレームの記録中に解放を頼む。記録済みのコマンドが参照しているかもしれないので、
    // 提出済みの 5 が完了していても、このフレームの serial まで待つ。
    queue.BeginFrame(5);
    queue.Retire(MakeFakeTexture());
    Expect(g_liveTextures == 1, "記録中に頼んだ解放は即座に破棄されない");

    queue.Collect(5);
    Expect(g_liveTextures == 1, "記録中のフレームの serial が決まる前は、Collect しても破棄されない");

    queue.CommitFrame(7);
    queue.Collect(6);
    Expect(g_liveTextures == 1, "記録していたフレームが完了するまで破棄されない");
    queue.Collect(7);
    Expect(g_liveTextures == 0, "記録していたフレームが完了したら破棄される");
}

void TestAbortedFrameFallsBackToLastSubmitted()
{
    GpuRetireQueue queue;
    queue.BeginFrame(0);
    queue.CommitFrame(3);

    queue.BeginFrame(0);
    queue.Retire(MakeFakeBuffer());
    queue.AbortFrame();
    Expect(g_liveBuffers == 1, "中止しただけでは破棄されない（直前の提出が完了するまで待つ）");

    queue.Collect(2);
    Expect(g_liveBuffers == 1, "直前に提出した serial が完了するまで破棄されない");
    queue.Collect(3);
    Expect(g_liveBuffers == 0, "直前に提出した serial が完了したら破棄される");
}

void TestClearReleasesEverything()
{
    GpuRetireQueue queue;
    queue.BeginFrame(0);
    queue.CommitFrame(9);
    queue.Retire(MakeFakeTexture());
    queue.Retire(MakeFakeBuffer());
    queue.BeginFrame(0);
    queue.Retire(MakeFakeTexture());
    Expect(g_liveTextures == 2 && g_liveBuffers == 1, "Clear の前は全部残っている");

    queue.Clear();
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "Clear は期限を問わず全部破棄する");
    Expect(queue.GetPendingCount() == 0, "Clear の後は待ち行列が空");
}

void TestClearResetsSerialsForReinitialization()
{
    GpuRetireQueue queue;
    queue.BeginFrame(0);
    queue.CommitFrame(5);
    // 記録中のまま（提出も中止もしないで）終わった状態から再初期化する。
    queue.BeginFrame(5);
    queue.Clear();

    // 新しい提出系列は serial が 1 から始まる。古い 5 が残っていると 1 が完了済みと誤判定される。
    queue.BeginFrame(0);
    queue.CommitFrame(1);
    queue.Retire(MakeFakeTexture());
    queue.Retire(MakeFakeBuffer());
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "再初期化後の提出が完了するまで破棄されない");

    queue.Collect(0);
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "新しい系列の serial が届く前は破棄されない");
    queue.Collect(1);
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "新しい系列の serial が届いたら破棄される");

    // 記録中の印も残らない: Clear の後に頼んだ解放は、提出前なら即座に破棄される。
    GpuRetireQueue open;
    open.BeginFrame(0);
    open.Clear();
    open.Retire(MakeFakeTexture());
    Expect(g_liveTextures == 0, "Clear の後は記録中の印が残らず、何も提出していなければ即座に破棄される");
}

void TestDestructorReleasesEverything()
{
    {
        GpuRetireQueue queue;
        queue.BeginFrame(0);
        queue.CommitFrame(9);
        queue.Retire(MakeFakeTexture());
    }
    Expect(g_liveTextures == 0, "キューの破棄で、待っていた資源も破棄される");
}

void TestRenderResourcesRetiresReleasedResources()
{
    RenderResources manager;
    auto device = MakeShared<RetireFakeDevice>();
    Expect(manager.Initialize(device), "偽デバイスで RenderResources が初期化できなければならない");

    TextureCreateInfo textureInfo;
    textureInfo.Width = 8;
    textureInfo.Height = 8;
    textureInfo.DebugName = "RetireTexture";
    const TextureHandle texture = manager.Textures().CreateTexture(textureInfo);
    Expect(texture.IsValid(), "テクスチャが作成できなければならない");

    BufferCreateInfo bufferInfo;
    bufferInfo.Size = 256;
    bufferInfo.UsageType = BufferCreateInfo::Usage::Vertex;
    bufferInfo.DebugName = "RetireBuffer";
    const BufferHandle buffer = manager.Gpu().CreateBuffer(bufferInfo);
    Expect(buffer.IsValid(), "バッファが作成できなければならない");
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "作成直後は RHI 資源が1つずつ生きている");

    // フレームを1つ提出した後に解放を頼む。
    manager.BeginRetireFrame(0);
    manager.CommitRetireFrame(3);

    manager.Textures().ReleaseTexture(texture);
    manager.Gpu().ReleaseBuffer(buffer);
    Expect(manager.Textures().GetRHITexture(texture) == nullptr, "ハンドルは解放の時点で引けなくなる");
    Expect(manager.GetResourceStats().TextureCount == 0, "台帳はハンドルが外れた時点で減る");
    Expect(g_liveTextures == 1 && g_liveBuffers == 1,
           "ReleaseTexture・ReleaseBuffer の後も、提出した serial が完了するまで RHI 資源は破棄されない");
    Expect(manager.GetPendingRetireCount() == 2, "解放を頼んだ2件が待つ");

    manager.BeginRetireFrame(2);
    manager.AbortRetireFrame();
    Expect(g_liveTextures == 1 && g_liveBuffers == 1, "完了の serial が届かない間は破棄されない");

    manager.BeginRetireFrame(3);
    manager.AbortRetireFrame();
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "完了の serial が届いたら破棄される");
    Expect(manager.GetPendingRetireCount() == 0, "破棄した後は待ち行列が空");

    // 無効なハンドル・二重の解放は何も起こさない。
    manager.Textures().ReleaseTexture(TextureHandle::Invalid());
    manager.Textures().ReleaseTexture(texture);
    manager.Gpu().ReleaseBuffer(buffer);
    Expect(manager.GetPendingRetireCount() == 0, "無効・解放済みのハンドルは待ち行列に積まない");

    // Shutdown は期限を問わず全部破棄する。
    const TextureHandle lateTexture = manager.Textures().CreateTexture(textureInfo);
    manager.BeginRetireFrame(3);
    manager.CommitRetireFrame(10);
    manager.Textures().ReleaseTexture(lateTexture);
    Expect(g_liveTextures == 1, "serial 10 が完了するまで残っている");

    manager.Shutdown();
    Expect(g_liveTextures == 0 && g_liveBuffers == 0, "Shutdown は待っていた RHI 資源を全部破棄する");
}

// 1つの塊が 4 ページ（256 KiB）の小さなプールで、貸し出し・返却・増設・上限・確保の失敗を確かめる。
void TestSparsePagePoolLeaseAndGrow()
{
    constexpr uint64_t Page = RHI::SparsePageSizeBytes;
    auto device = MakeShared<RetireFakeDevice>();
    {
        SparsePagePool pool(device, 4 * Page);
        Expect(pool.GetStats().BlockCount == 0 && g_liveSparseBlocks == 0, "作った直後は塊を持たない（必要になるまで確保しない）");

        Core::Container::VariableArray<SparsePagePool::PageLease> leases;
        for (int i = 0; i < 4; ++i)
        {
            leases.push_back(pool.Acquire());
            Expect(leases.back().IsValid(), "空きがあるページは借りられる");
        }
        SparsePagePool::Stats stats = pool.GetStats();
        Expect(stats.BlockCount == 1 && stats.CapacityBytes == 4 * Page && stats.UsedBytes == 4 * Page && stats.FreeBytes == 0,
               "4ページ借りた時点で、塊は1つ・全部貸し出し中");

        // ページは重ならず、64 KiB の倍数の位置で、塊の中にある。
        bool bDistinct = true;
        bool bAligned = true;
        for (size_t a = 0; a < leases.size(); ++a)
        {
            const RHI::SparsePageRef pageA = leases[a].GetPage();
            bAligned = bAligned && pageA.Block != nullptr && pageA.OffsetBytes % Page == 0 &&
                       pageA.OffsetBytes + Page <= pageA.Block->GetSizeBytes();
            for (size_t b = a + 1; b < leases.size(); ++b)
            {
                const RHI::SparsePageRef pageB = leases[b].GetPage();
                bDistinct = bDistinct && !(pageA.Block == pageB.Block && pageA.OffsetBytes == pageB.OffsetBytes);
            }
        }
        Expect(bDistinct, "貸し出し中のページは重ならない");
        Expect(bAligned, "ページは 64 KiB の倍数の位置で、塊の中にある");

        // 空きが無ければ塊を増やす。上限に達したら借りられない。
        leases.push_back(pool.Acquire());
        Expect(leases.back().IsValid() && pool.GetStats().BlockCount == 2, "空きが無ければ塊を1つ増やして貸す");
        pool.SetCapacityLimitBytes(8 * Page);
        for (int i = 0; i < 3; ++i)
        {
            leases.push_back(pool.Acquire());
        }
        SparsePagePool::PageLease overLimit = pool.Acquire();
        Expect(!overLimit.IsValid() && pool.GetStats().BlockCount == 2, "上限を超える塊は作らず、借りられない");

        // 返すとそのページを次に貸す。
        const RHI::SparsePageRef returned = leases[1].GetPage();
        leases[1].Reset();
        Expect(!leases[1].IsValid() && pool.GetStats().UsedBytes == 7 * Page, "Reset でページがプールへ戻る");
        SparsePagePool::PageLease reused = pool.Acquire();
        Expect(reused.IsValid() && reused.GetPage().Block == returned.Block && reused.GetPage().OffsetBytes == returned.OffsetBytes,
               "返したページは次に貸される");

        // 移動しても返却は1回だけ。
        SparsePagePool::PageLease moved = std::move(reused);
        Expect(!reused.IsValid() && moved.IsValid(), "移動元は無効になる");
        moved.Reset();
        moved.Reset();
        Expect(pool.GetStats().UsedBytes == 7 * Page, "二重に Reset しても返却は1回だけ");
    }
    Expect(g_liveSparseBlocks == 0, "プールと貸し出しがすべて無くなれば塊も破棄される");

    // 塊を作れないデバイスでは、借りられず、状態も変わらない。
    g_failSparseBlockCreation = true;
    {
        SparsePagePool pool(device, 4 * Page);
        Expect(!pool.Acquire().IsValid() && pool.GetStats().BlockCount == 0, "塊を作れなければ借りられない");
    }
    g_failSparseBlockCreation = false;

    // プールより長く生きる貸し出しは、プールを破棄した後に手放しても安全。
    SparsePagePool::PageLease survivor;
    {
        SparsePagePool pool(device, 4 * Page);
        survivor = pool.Acquire();
        Expect(survivor.IsValid(), "プールの寿命より長い貸し出しを作れる");
    }
    Expect(g_liveSparseBlocks == 1, "貸し出しが残っている間は塊が生きている");
    survivor.Reset();
    Expect(g_liveSparseBlocks == 0, "最後の貸し出しを手放すと塊が破棄される");
}

// 外したページは、最後に使った提出の serial が完了するまでプールへ戻らない。
void TestSparsePageReturnedAfterSerial()
{
    constexpr uint64_t Page = RHI::SparsePageSizeBytes;
    auto device = MakeShared<RetireFakeDevice>();
    SparsePagePool pool(device, 4 * Page);
    GpuRetireQueue queue;

    queue.Retire(SparsePagePool::PageLease());
    Expect(queue.GetPendingCount() == 0, "無効なページの返却は積まない");

    // 何も提出していなければ、すぐ戻る。
    queue.Retire(pool.Acquire());
    Expect(pool.GetStats().UsedBytes == 0 && queue.GetPendingCount() == 0, "何も提出していなければ外したページは即座に戻る");

    // 提出済みの serial が完了するまで保持される。
    queue.BeginFrame(0);
    queue.CommitFrame(7);
    queue.Retire(pool.Acquire());
    Expect(pool.GetStats().UsedBytes == Page && queue.GetPendingCount() == 1, "提出した serial が完了するまで戻らない");
    queue.Collect(6);
    Expect(pool.GetStats().UsedBytes == Page, "完了の serial が届かない間は戻らない");
    queue.Collect(7);
    Expect(pool.GetStats().UsedBytes == 0 && pool.GetStats().FreeBytes == 4 * Page && queue.GetPendingCount() == 0,
           "完了の serial が届いたらプールへ戻る");

    // 記録中のフレームで外したページは、そのフレームの serial まで延びる。
    queue.BeginFrame(7);
    queue.Retire(pool.Acquire());
    queue.CommitFrame(9);
    queue.Collect(8);
    Expect(pool.GetStats().UsedBytes == Page, "記録中に外したページは、そのフレームの serial まで戻らない");
    queue.Collect(9);
    Expect(pool.GetStats().UsedBytes == 0, "そのフレームの serial が完了したら戻る");

    // Clear は期限を問わず全部返す。
    queue.BeginFrame(9);
    queue.CommitFrame(20);
    queue.Retire(pool.Acquire());
    queue.Retire(pool.Acquire());
    Expect(pool.GetStats().UsedBytes == 2 * Page, "serial 20 が完了するまで2ページが待つ");
    queue.Clear();
    Expect(pool.GetStats().UsedBytes == 0, "Clear は待っているページを全部プールへ返す");
}

// RenderResources は sparse に対応するデバイスでだけプールを持ち、台帳へ量を出す。
void TestRenderResourcesOwnsSparsePool()
{
    {
        RenderResources manager;
        auto device = MakeShared<RetireFakeDevice>();
        Expect(manager.Initialize(device), "sparse に対応しない偽デバイスでも RenderResources が初期化できなければならない");
        Expect(manager.GetSparsePagePool() == nullptr, "sparse に対応しないデバイスではプールを持たない");
        Expect(manager.GetResourceStats().SparsePoolCapacityBytes == 0, "プールが無ければ台帳の量は 0");
    }

    RenderResources manager;
    auto device = MakeShared<RetireFakeDevice>();
    device->Capabilities.Sparse.bSparseBinding = true;
    device->Capabilities.Sparse.bResidencyImage2D = true;
    Expect(manager.Initialize(device), "sparse に対応する偽デバイスで RenderResources が初期化できなければならない");
    SparsePagePool* pool = manager.GetSparsePagePool();
    Expect(pool != nullptr, "sparse に対応するデバイスではプールを持つ");
    if (pool != nullptr)
    {
        Expect(manager.GetResourceStats().SparsePoolCapacityBytes == 0 && g_liveSparseBlocks == 0,
               "プールは必要になるまで塊を確保しない");
        {
            SparsePagePool::PageLease lease = pool->Acquire();
            const Core::Rendering::ResourceStats stats = manager.GetResourceStats();
            Expect(lease.IsValid() && stats.SparsePoolCapacityBytes == SparsePagePool::DefaultBlockBytes &&
                       stats.SparsePoolUsedBytes == RHI::SparsePageSizeBytes,
                   "既定の塊（64 MiB）の確保と、借りた1ページが台帳に出る");
        }
        Expect(manager.GetResourceStats().SparsePoolUsedBytes == 0, "返すと貸し出し中の量が 0 に戻る");
    }

    manager.Shutdown();
    Expect(g_liveSparseBlocks == 0 && manager.GetSparsePagePool() == nullptr, "Shutdown でプールの塊が破棄される");
}

// VT が使えるのは、sparse の結び付け・2D の部分常駐・常駐の照会・フラグメントからの storage buffer の書き込みの
// 4つがそろうデバイスだけ。1つでも欠けると、材質が要求を書けず VT はミップテイルのまま粗く描かれ続けるので、全常駐で描く。
void TestVirtualTextureNeedsFeedbackCapabilities()
{
    struct Case
    {
        bool bFragmentStoresAndAtomics;
        bool bSparseBinding;
        bool bResidencyImage2D;
        bool bShaderResourceResidency;
        bool bExpectSupported;
        const char* Message;
    };
    const Case cases[] = {
        {false, true, true, true, false, "sparse の2D部分常駐に対応しても、フラグメントの storage buffer 書き込みが無ければ VT は使えない"},
        {true, false, true, true, false, "sparse の結び付けが無ければ VT は使えない"},
        {true, true, false, true, false, "2D の部分常駐が無ければ VT は使えない"},
        {true, true, true, false, false, "常駐の照会が無ければ VT は使えない"},
        {true, true, true, true, true, "4つの機能がそろうデバイスでは VT が使える"},
    };

    for (const Case& testCase : cases)
    {
        RenderResources manager;
        auto device = MakeShared<RetireFakeDevice>();
        device->Capabilities.bFragmentStoresAndAtomics = testCase.bFragmentStoresAndAtomics;
        device->Capabilities.Sparse.bSparseBinding = testCase.bSparseBinding;
        device->Capabilities.Sparse.bResidencyImage2D = testCase.bResidencyImage2D;
        device->Capabilities.Sparse.bShaderResourceResidency = testCase.bShaderResourceResidency;
        Expect(manager.Initialize(device), "VT の対応を確かめる偽デバイスで RenderResources が初期化できなければならない");
        Expect(manager.Textures().SupportsVirtualTexture() == testCase.bExpectSupported, testCase.Message);
        manager.Shutdown();
    }
}

// ---- クック済みの VT（NVTEX v0.2）を偽デバイスへ作るための部品 ----

// R8 の 64x64 は、どの段もタイル（256x256）より小さいので、全ミップがミップテイルでタイルの表は空になる。
constexpr uint32_t VtTestSize = 64;
const char* const VtTestLogicalPath = "Textures/Cooked.tga";

void WriteLe16(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
}

void WriteLe32(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
    {
        bytes[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xffu);
    }
}

void WriteLe64(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint64_t value)
{
    WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
    WriteLe32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

Core::Container::VariableArray<uint8_t> BuildTailOnlyTiledTexture()
{
    const uint32_t mipCount = Asset::ComputeCookedTextureFullMipCount(VtTestSize, VtTestSize);
    const size_t mipTableSize = static_cast<size_t>(mipCount) * TextureV0::MipRecordSize;
    const size_t metadataSize = TextureV0::HeaderSizeTiled + mipTableSize;

    Core::Container::VariableArray<uint8_t> payload;
    Core::Container::VariableArray<uint64_t> mipOffsets;
    Core::Container::VariableArray<uint64_t> mipSizes;
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t dimension = (VtTestSize >> mip) == 0 ? 1 : (VtTestSize >> mip);
        const size_t bytes = static_cast<size_t>(dimension) * dimension;
        mipOffsets.push_back(metadataSize + payload.size());
        mipSizes.push_back(bytes);
        for (size_t i = 0; i < bytes; ++i)
        {
            payload.push_back(static_cast<uint8_t>(mip + i));
        }
    }

    const size_t fileSize = metadataSize + payload.size();
    Core::Container::VariableArray<uint8_t> bytes(fileSize, 0);
    std::memcpy(bytes.data() + TextureV0::HeaderOffset::Magic, TextureV0::Magic, TextureV0::MagicSize);
    WriteLe32(bytes, TextureV0::HeaderOffset::HeaderSize, static_cast<uint32_t>(TextureV0::HeaderSizeTiled));
    WriteLe16(bytes, TextureV0::HeaderOffset::VersionMajor, TextureV0::VersionMajor);
    WriteLe16(bytes, TextureV0::HeaderOffset::VersionMinor, TextureV0::VersionMinorTiled);
    WriteLe32(bytes, TextureV0::HeaderOffset::EndianMarker, TextureV0::EndianMarker);
    WriteLe32(bytes, TextureV0::HeaderOffset::MipRecordSize, static_cast<uint32_t>(TextureV0::MipRecordSize));
    WriteLe64(bytes, TextureV0::HeaderOffset::FileSize, fileSize);
    WriteLe64(bytes, TextureV0::HeaderOffset::MipTableOffset, TextureV0::HeaderSizeTiled);
    WriteLe64(bytes, TextureV0::HeaderOffset::MipTableSize, mipTableSize);
    WriteLe64(bytes, TextureV0::HeaderOffset::PayloadOffset, metadataSize);
    WriteLe64(bytes, TextureV0::HeaderOffset::PayloadSize, payload.size());
    WriteLe32(bytes, TextureV0::HeaderOffset::Width, VtTestSize);
    WriteLe32(bytes, TextureV0::HeaderOffset::Height, VtTestSize);
    WriteLe32(bytes, TextureV0::HeaderOffset::LayerCount, 1);
    WriteLe32(bytes, TextureV0::HeaderOffset::MipCount, mipCount);
    WriteLe32(bytes, TextureV0::HeaderOffset::PixelFormat, static_cast<uint32_t>(Asset::CookedTexturePixelFormat::R8UNorm));
    WriteLe32(bytes, TextureV0::HeaderOffset::ColorSpace, static_cast<uint32_t>(Asset::CookedTextureColorSpace::Linear));
    WriteLe32(bytes, TextureV0::TiledHeaderOffset::TileWidth, 256);
    WriteLe32(bytes, TextureV0::TiledHeaderOffset::TileHeight, 256);
    WriteLe32(bytes, TextureV0::TiledHeaderOffset::FirstTailMip, 0);
    WriteLe32(bytes, TextureV0::TiledHeaderOffset::TileDataBytes, 65536);
    WriteLe64(bytes, TextureV0::TiledHeaderOffset::TileTableOffset, metadataSize);
    WriteLe64(bytes, TextureV0::TiledHeaderOffset::TileTableSize, 0);
    WriteLe64(bytes, TextureV0::TiledHeaderOffset::TailOffset, metadataSize);
    WriteLe64(bytes, TextureV0::TiledHeaderOffset::TailSize, payload.size());
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const size_t recordOffset = TextureV0::HeaderSizeTiled + static_cast<size_t>(mip) * TextureV0::MipRecordSize;
        const uint32_t dimension = (VtTestSize >> mip) == 0 ? 1 : (VtTestSize >> mip);
        WriteLe64(bytes, recordOffset + TextureV0::MipRecordOffset::DataOffset, mipOffsets[mip]);
        WriteLe64(bytes, recordOffset + TextureV0::MipRecordOffset::DataSize, mipSizes[mip]);
        WriteLe32(bytes, recordOffset + TextureV0::MipRecordOffset::Width, dimension);
        WriteLe32(bytes, recordOffset + TextureV0::MipRecordOffset::Height, dimension);
    }
    std::memcpy(bytes.data() + metadataSize, payload.data(), payload.size());
    WriteLe64(bytes, TextureV0::HeaderOffset::PayloadHash,
              Asset::ComputeCookedTexturePayloadHash(payload.data(), payload.size()));
    return bytes;
}

// 1 件だけを入れた .nvpkg
Core::Container::VariableArray<uint8_t> BuildSingleEntryPackage(const Core::Container::AnsiString& name,
                                                                const Core::Container::VariableArray<uint8_t>& payload)
{
    const size_t alignment = PackageV1::MinimumAlignment;
    const size_t entryTableOffset = PackageV1::HeaderSize;
    const size_t entryTableSize = PackageV1::EntryRecordSize;
    const size_t nameTableOffset = AlignUp(entryTableOffset + entryTableSize, alignment);
    const size_t blobDataOffset = AlignUp(nameTableOffset + name.size(), alignment);
    const size_t dataOffset = AlignUp(blobDataOffset, alignment);
    const size_t packageSize = dataOffset + payload.size();

    Core::Container::VariableArray<uint8_t> bytes(packageSize, 0);
    std::memcpy(bytes.data() + PackageV1::HeaderOffset::Magic, PackageV1::Magic, PackageV1::MagicSize);
    WriteLe32(bytes, PackageV1::HeaderOffset::HeaderSize, static_cast<uint32_t>(PackageV1::HeaderSize));
    WriteLe16(bytes, PackageV1::HeaderOffset::VersionMajor, PackageV1::VersionMajor);
    WriteLe16(bytes, PackageV1::HeaderOffset::VersionMinor, PackageV1::VersionMinor);
    WriteLe32(bytes, PackageV1::HeaderOffset::EndianMarker, PackageV1::EndianMarker);
    WriteLe32(bytes, PackageV1::HeaderOffset::EntryRecordSize, static_cast<uint32_t>(PackageV1::EntryRecordSize));
    WriteLe64(bytes, PackageV1::HeaderOffset::PackageSize, packageSize);
    WriteLe32(bytes, PackageV1::HeaderOffset::EntryCount, 1);
    WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableOffset, entryTableOffset);
    WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableSize, entryTableSize);
    WriteLe64(bytes, PackageV1::HeaderOffset::NameTableOffset, nameTableOffset);
    WriteLe64(bytes, PackageV1::HeaderOffset::NameTableSize, name.size());
    WriteLe64(bytes, PackageV1::HeaderOffset::BlobDataOffset, blobDataOffset);
    WriteLe32(bytes, PackageV1::HeaderOffset::Alignment, static_cast<uint32_t>(alignment));

    std::memcpy(bytes.data() + nameTableOffset, name.data(), name.size());
    std::memcpy(bytes.data() + dataOffset, payload.data(), payload.size());

    const size_t record = entryTableOffset;
    WriteLe64(bytes, record + PackageV1::EntryOffset::NameOffset, nameTableOffset);
    WriteLe32(bytes, record + PackageV1::EntryOffset::NameSize, static_cast<uint32_t>(name.size()));
    WriteLe32(bytes, record + PackageV1::EntryOffset::Type, Asset::MakeAssetPackageFourCC('T', 'e', 'x', '0'));
    WriteLe32(bytes, record + PackageV1::EntryOffset::Compression, static_cast<uint32_t>(Asset::AssetPackageCompression::None));
    WriteLe64(bytes, record + PackageV1::EntryOffset::DataOffset, dataOffset);
    WriteLe64(bytes, record + PackageV1::EntryOffset::StoredSize, payload.size());
    WriteLe64(bytes, record + PackageV1::EntryOffset::UncompressedSize, payload.size());
    WriteLe64(bytes, record + PackageV1::EntryOffset::PayloadHash,
              Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size()));
    return bytes;
}

Core::Container::String ToCoreString(const Core::Container::AnsiString& text)
{
#if defined(UNICODE)
    Core::Container::String wide;
    wide.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(text[i])));
    }
    return wide;
#else
    return Core::Container::String(text.c_str());
#endif
}

// クック済みの VT を1つ置いた一時のアセットの根。デストラクタで消す。
class VtAssetRoot
{
public:
    VtAssetRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        m_Root = std::filesystem::temp_directory_path() / ("NorvesLibGpuRetireQueueTest_Vt_" + std::to_string(now));
        std::filesystem::remove_all(m_Root);
        std::filesystem::create_directories(m_Root / "Cooked");

        m_Texture = BuildTailOnlyTiledTexture();
        const Core::Container::VariableArray<uint8_t> package = BuildSingleEntryPackage("Textures/Cooked.nvtex", m_Texture);
        std::ofstream output(m_Root / "Cooked" / "Textures.nvpkg", std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
    }
    ~VtAssetRoot() { std::filesystem::remove_all(m_Root); }

    VtAssetRoot(const VtAssetRoot&) = delete;
    VtAssetRoot& operator=(const VtAssetRoot&) = delete;

    // 根とマニフェストを RenderResources に設定する。設定できたら true。
    bool Configure(RenderResources& manager) const
    {
        const Core::Container::AnsiString hash =
            Asset::FormatAssetHashHex(Asset::ComputeAssetPackagePayloadHash(m_Texture.data(), m_Texture.size()));
        const Core::Container::AnsiString entryType =
            Asset::FormatAssetPackageFourCCText(Asset::MakeAssetPackageFourCC('T', 'e', 'x', '0'));
        const Core::Container::AnsiString manifest = Core::Container::AnsiString("{\"version\":1,\"assets\":[{\"logical_path\":\"") + VtTestLogicalPath +
                                     "\",\"kind\":\"texture\",\"source_hash\":\"0000000000000001\","
                                     "\"variant\":\"default\",\"format\":\"nvtex.v0.r8.linear\","
                                     "\"cooked_package\":\"Cooked/Textures.nvpkg\","
                                     "\"entry_name\":\"Textures/Cooked.nvtex\",\"entry_type\":\"" +
                                     entryType + "\",\"cooked_hash\":\"" + hash + "\",\"cooked_version\":0}]}";
        return manager.Textures().SetTextureAssetRoot(ToCoreString(m_Root.generic_string().c_str())) &&
               manager.Textures().LoadTextureAssetManifestFromJsonText(ToCoreString(manifest));
    }

private:
    std::filesystem::path m_Root;
    Core::Container::VariableArray<uint8_t> m_Texture;
};

// VT に必要な4つの機能と R8 の sparse の標準ブロック形状がそろう偽デバイス
Core::Container::TSharedPtr<RetireFakeDevice> MakeVirtualTextureDevice()
{
    auto device = MakeShared<RetireFakeDevice>();
    device->Capabilities.bFragmentStoresAndAtomics = true;
    RHI::SparseCapabilities& sparse = device->Capabilities.Sparse;
    sparse.bSparseBinding = true;
    sparse.bResidencyImage2D = true;
    sparse.bShaderResourceResidency = true;
    sparse.FormatCount = 1;
    sparse.Formats[0].TextureFormat = RHI::Format::R8_UNORM;
    sparse.Formats[0].bSupported = true;
    sparse.Formats[0].GranularityWidth = 256;
    sparse.Formats[0].GranularityHeight = 256;
    sparse.Formats[0].bStandardBlockShape = true;
    return device;
}

// 要求のバッファを確保して有効にできなければ、VT を解放して無効なハンドルを返す（呼び出し側が全常駐へ戻す）。
void TestCreateVirtualTextureReleasesWhenFeedbackFails()
{
    const VtAssetRoot assetRoot;
    const Core::Container::String path = ToCoreString(VtTestLogicalPath);

    // 対照: 要求のバッファを作って写像できるデバイスなら、同じクック済みのテクスチャが VT として作れる。
    {
        g_mapFeedbackBuffers = true;
        RenderResources manager;
        auto device = MakeVirtualTextureDevice();
        Expect(manager.Initialize(device), "VT に対応する偽デバイスで RenderResources が初期化できなければならない");
        Expect(assetRoot.Configure(manager), "クック済みのテクスチャのアセットの根とマニフェストを設定できなければならない");
        Expect(manager.Textures().SupportsVirtualTexture(), "VT に必要な4つの機能がそろえば VT が使える");

        const TextureHandle handle = manager.Textures().CreateVirtualTexture(path);
        Expect(handle.IsValid(), "要求のバッファを作れるデバイスでは VT を作れる");
        Core::Rendering::VirtualTextureStreamer* streamer = manager.GetVirtualTextureStreamer();
        Core::Rendering::VirtualTextureFeedbackRing* feedback = manager.GetVirtualTextureFeedback();
        Expect(streamer != nullptr && streamer->GetStats().TextureCount == 1, "作った VT はストリーマに1件登録される");
        Expect(feedback != nullptr && feedback->GetStats().bEnabled, "作った VT の要求のバッファは有効になる");
        manager.Shutdown();
        g_mapFeedbackBuffers = false;
        Expect(g_liveTextures == 0, "対照のデバイスの Shutdown でテクスチャが全部破棄される");
    }

    // 要求のバッファを作れないデバイスでは、VT を解放して無効なハンドルを返す。
    {
        g_failFeedbackBufferCreation = true;
        RenderResources manager;
        auto device = MakeVirtualTextureDevice();
        Expect(manager.Initialize(device), "VT に対応する偽デバイスで RenderResources が初期化できなければならない");
        Expect(assetRoot.Configure(manager), "クック済みのテクスチャのアセットの根とマニフェストを設定できなければならない");
        Expect(manager.Textures().SupportsVirtualTexture(), "4つの機能がそろえば、要求のバッファを作る前の段階では VT が使える");

        const TextureHandle handle = manager.Textures().CreateVirtualTexture(path);
        Expect(!handle.IsValid(), "要求のバッファを作れなければ VT は無効なハンドルを返す");
        Core::Rendering::VirtualTextureStreamer* streamer = manager.GetVirtualTextureStreamer();
        Core::Rendering::VirtualTextureFeedbackRing* feedback = manager.GetVirtualTextureFeedback();
        Expect(streamer != nullptr && streamer->GetStats().TextureCount == 0, "失敗した VT はストリーマの登録から外れる");
        Expect(feedback != nullptr && !feedback->GetStats().bEnabled, "要求のバッファを作れなければフィードバックは無効のまま");
        Expect(manager.GetResourceStats().TextureCount == 0, "失敗した VT のテクスチャは台帳に残らない");
        Expect(g_liveTextures == 0, "失敗した VT の RHI テクスチャは破棄される");

        Expect(!manager.Textures().CreateVirtualTextureAsync(path, Core::Delegate<void, TextureHandle>()),
               "非同期の作成も、要求のバッファを作れなければ false を返す（全常駐へ戻す）");
        Expect(streamer->GetStats().TextureCount == 0 && g_liveTextures == 0, "非同期の作成の失敗も何も残さない");

        manager.Shutdown();
        g_failFeedbackBufferCreation = false;
    }
}

int RunTest()
{
    TestNothingSubmittedReleasesImmediately();
    TestHeldUntilCompletedSerial();
    TestRetiredDuringRecordingWaitsForThatFrame();
    TestAbortedFrameFallsBackToLastSubmitted();
    TestClearReleasesEverything();
    TestClearResetsSerialsForReinitialization();
    TestDestructorReleasesEverything();
    TestRenderResourcesRetiresReleasedResources();
    TestSparsePagePoolLeaseAndGrow();
    TestSparsePageReturnedAfterSerial();
    TestRenderResourcesOwnsSparsePool();
    TestVirtualTextureNeedsFeedbackCapabilities();
    TestCreateVirtualTextureReleasesWhenFeedbackFails();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "GpuRetireQueueTest 成功" << std::endl;
    return 0;
}

} // 無名名前空間の終わり
} // NorvesLib 名前空間の終わり

int main()
{
    return NorvesLib::RunTest();
}
