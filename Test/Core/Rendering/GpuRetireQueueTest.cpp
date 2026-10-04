// GPU 資源の遅延解放キュー（GpuRetireQueue）の契約テスト。
// sparse の物理メモリのページ（SparsePagePool）が、貸し出し・返却・増設・上限のとおりに動き、
// 外したページが最後に使った提出の serial の完了までプールへ戻らないことも確かめる。
// 提出の serial が完了するまで RHI 資源が破棄されないこと、完了したら破棄されること、
// 記録中のフレームで頼まれた解放はそのフレームの serial まで延びること、Shutdown で全部破棄されることを、
// GPU を使わない偽デバイスで確かめる。
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/RenderResources.h"
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
using Core::Rendering::BufferCreateInfo;
using Core::Rendering::BufferHandle;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::RenderResources;
using Core::Rendering::SparsePagePool;
using Core::Rendering::TextureCreateInfo;
using Core::Rendering::TextureHandle;

int g_failures = 0;
// 生きている偽資源の数（破棄されたかどうかの観測に使う）
int g_liveTextures = 0;
int g_liveBuffers = 0;
int g_liveSparseBlocks = 0;
// true の間、偽デバイスは sparse の塊を作れない
bool g_failSparseBlockCreation = false;

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
    void* Map(uint64_t, uint64_t) override { return nullptr; }
    void Unmap() override {}
    void Update(const void*, uint64_t, uint64_t) override {}
    RHI::ResourceUsage GetUsage() const override { return Usage; }

    uint64_t Size;
    RHI::ResourceUsage Usage;
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
        return MakeShared<RetireFakeBuffer>(desc.Size, desc.Usage);
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
