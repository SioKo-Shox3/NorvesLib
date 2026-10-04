// VT のストリーマ（VirtualTextureStreamer）の契約テスト。
// 要求の集合から未常駐のタイルを優先度順に選んで読み、ページを結び、コピーを積んで常駐させること、
// 1 フレームの上限（読み・結び付け・コピーの数と量。ミップテイルも最初の 1 件も同じ予算で、収まらないテイルは
// 公開しないまま複数フレームに分けて書く。処理できない上限の設定は登録で拒否する）、同じタイルの二重の要求、
// 読み込みの失敗と再試行、結び付けの前にコピーを積むこと（結んだページが未コピーのまま残らない）、
// 結び付けの失敗・コピーを積めないとき・プールが尽きたときの扱い、解除したページの遅延返却と
// 解除したテクスチャ宛てのコピーの無効化、優先度（粗いミップ → 要求の件数 → 最近）を、読み込みも GPU も偽物にして確かめる。
// 解除したテクスチャ宛てのコピーの無効化は、実物の TileUploader・GpuRetireQueue・本番の窓口でも確かめる
// （記録 → Abort → 登録解除 → ページの再取得 → 次のフレーム）。
// 追い出し（SetResidentBudget）は、目標を下げたときの外す順（使われていないタイルを LRU で、次に使われているタイルを
// 細かいミップから）と、そのたびに量とプールの使用量が目標以下に収まること、優先度の低い要求を結ばないこと、
// 外すタイルと結ぶタイルが同じ BindSparse に入ること、外したページが最後に提出したフレームの完了まで再利用されず
// 未記録のコピーも無効になること（実物のアップローダ・リトアキュー）、ミップ単位のテクスチャがミップごとに
// 結び・外されること、読み込みの枠を確保することを確かめる。
#include "Asset/CookedTextureFormat.h"
#include "Rendering/CookedVirtualTexture.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/TileUploader.h"
#include "Rendering/VirtualTextureRequestSet.h"
#include "Rendering/VirtualTextureStreamer.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
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

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Container::TSharedPtr;
using Core::Container::VariableArray;
using Core::Rendering::DeviceVirtualTextureGpu;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::IVirtualTextureGpu;
using Core::Rendering::IVirtualTextureTileSource;
using Core::Rendering::SparsePagePool;
using Core::Rendering::TileUploader;
using Core::Rendering::VirtualTextureFrameResult;
using Core::Rendering::VirtualTextureRegistration;
using Core::Rendering::VirtualTextureRequestSet;
using Core::Rendering::VirtualTextureStreamer;
using Core::Rendering::VirtualTextureStreamerConfig;
using Core::Rendering::VirtualTextureStreamerStats;
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

// ---- 偽物: バッファ（TileUploader のリング）----

class FakeBuffer final : public RHI::IBuffer
{
public:
    explicit FakeBuffer(const RHI::BufferDesc& desc) : Desc(desc), Bytes(static_cast<size_t>(desc.Size), 0u) {}
    uint64_t GetSize() const override { return Desc.Size; }
    void* Map(uint64_t offset = 0, uint64_t = 0) override
    {
        return offset < Bytes.size() ? Bytes.data() + static_cast<size_t>(offset) : nullptr;
    }
    void Unmap() override {}
    void Update(const void*, uint64_t, uint64_t = 0) override {}
    RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

    RHI::BufferDesc Desc;
    VariableArray<uint8_t> Bytes;
};

// ---- 偽物: コマンドリスト（TileUploader::RecordCopies の記録先）----

class FakeCommandList final : public RHI::ICommandList
{
public:
    struct CopyRecord
    {
        const RHI::ITexture* Texture = nullptr;
        RHI::TextureRegionCopy Region;
    };

    bool CopyBufferToTextureRegion(RHI::BufferPtr, RHI::TexturePtr dst, const RHI::TextureRegionCopy& region) override
    {
        CopyRecord record;
        record.Texture = dst.get();
        record.Region = region;
        Copies.push_back(record);
        return true;
    }

    // 記録されたコピーのうち、texture 宛ての数
    size_t CountCopiesTo(const RHI::ITexture* texture) const
    {
        size_t count = 0;
        for (const CopyRecord& record : Copies)
        {
            if (record.Texture == texture)
            {
                ++count;
            }
        }
        return count;
    }

    VariableArray<CopyRecord> Copies;

    void Begin() override {}
    void End() override {}
    void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
    void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
    {
        (void)renderPass;
        (void)framebuffer;
    }
    void EndRenderPass() override {}
    void SetViewport(const RHI::Viewport& viewport) override { (void)viewport; }
    void SetScissor(const RHI::ScissorRect& scissor) override { (void)scissor; }
    void SetPipeline(RHI::PipelinePtr pipeline) override { (void)pipeline; }
    void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
    {
        (void)buffer;
        (void)offset;
        (void)slot;
    }
    void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, RHI::IndexType = RHI::IndexType::Uint32) override
    {
        (void)buffer;
        (void)offset;
    }
    void SetConstantBuffer(RHI::BufferPtr buffer, uint32_t slot, RHI::ShaderStage stage) override
    {
        (void)buffer;
        (void)slot;
        (void)stage;
    }
    void SetTexture(RHI::TexturePtr texture, uint32_t slot, RHI::ShaderStage stage) override
    {
        (void)texture;
        (void)slot;
        (void)stage;
    }
    void SetSampler(RHI::SamplerPtr sampler, uint32_t slot, RHI::ShaderStage stage) override
    {
        (void)sampler;
        (void)slot;
        (void)stage;
    }
    void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
    {
        (void)descriptorSet;
        (void)slot;
    }
    void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) override
    {
        (void)indexCount;
        (void)startIndexLocation;
        (void)baseVertexLocation;
    }
    void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
    {
        (void)vertexCount;
        (void)startVertexLocation;
    }
    void DrawIndexedInstanced(uint32_t indexCount,
                              uint32_t instanceCount,
                              uint32_t startIndexLocation = 0,
                              int32_t baseVertexLocation = 0,
                              uint32_t startInstanceLocation = 0) override
    {
        (void)indexCount;
        (void)instanceCount;
        (void)startIndexLocation;
        (void)baseVertexLocation;
        (void)startInstanceLocation;
    }
    void DrawInstanced(uint32_t vertexCount,
                       uint32_t instanceCount,
                       uint32_t startVertexLocation = 0,
                       uint32_t startInstanceLocation = 0) override
    {
        (void)vertexCount;
        (void)instanceCount;
        (void)startVertexLocation;
        (void)startInstanceLocation;
    }
    void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer,
                             uint64_t offset,
                             uint32_t drawCount,
                             uint32_t stride) override
    {
        (void)indirectBuffer;
        (void)offset;
        (void)drawCount;
        (void)stride;
    }
    void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer,
                                  uint64_t indirectOffset,
                                  RHI::BufferPtr countBuffer,
                                  uint64_t countOffset,
                                  uint32_t maxDrawCount,
                                  uint32_t stride) override
    {
        (void)indirectBuffer;
        (void)indirectOffset;
        (void)countBuffer;
        (void)countOffset;
        (void)maxDrawCount;
        (void)stride;
    }
    void FillBuffer(RHI::BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override
    {
        (void)buffer;
        (void)offset;
        (void)size;
        (void)value;
    }
    void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override
    {
        (void)threadGroupCountX;
        (void)threadGroupCountY;
        (void)threadGroupCountZ;
    }
    void CopyBuffer(RHI::BufferPtr src,
                    RHI::BufferPtr dst,
                    uint64_t size = 0,
                    uint64_t srcOffset = 0,
                    uint64_t dstOffset = 0) override
    {
        (void)src;
        (void)dst;
        (void)size;
        (void)srcOffset;
        (void)dstOffset;
    }
    void CopyBufferToTexture(RHI::BufferPtr src,
                             RHI::TexturePtr dst,
                             uint32_t width,
                             uint32_t height,
                             uint64_t bufferOffset = 0,
                             uint32_t mipLevel = 0,
                             uint32_t arrayIndex = 0) override
    {
        (void)src;
        (void)dst;
        (void)width;
        (void)height;
        (void)bufferOffset;
        (void)mipLevel;
        (void)arrayIndex;
    }
    void CopyTextureToBuffer(RHI::TexturePtr src,
                             RHI::BufferPtr dst,
                             uint32_t width,
                             uint32_t height,
                             uint64_t bufferOffset = 0,
                             uint32_t mipLevel = 0,
                             uint32_t arrayIndex = 0) override
    {
        (void)src;
        (void)dst;
        (void)width;
        (void)height;
        (void)bufferOffset;
        (void)mipLevel;
        (void)arrayIndex;
    }
    void CopyTexture(RHI::TexturePtr src,
                     RHI::TexturePtr dst,
                     uint32_t width,
                     uint32_t height,
                     uint32_t srcMipLevel = 0,
                     uint32_t srcArrayIndex = 0,
                     uint32_t dstMipLevel = 0,
                     uint32_t dstArrayIndex = 0) override
    {
        (void)src;
        (void)dst;
        (void)width;
        (void)height;
        (void)srcMipLevel;
        (void)srcArrayIndex;
        (void)dstMipLevel;
        (void)dstArrayIndex;
    }
    void GenerateMipmaps(RHI::TexturePtr texture) override { (void)texture; }
    void BufferBarrier(RHI::BufferPtr buffer,
                       RHI::ResourceState beforeState,
                       RHI::ResourceState afterState,
                       uint64_t offset = 0,
                       uint64_t size = 0) override
    {
        (void)buffer;
        (void)beforeState;
        (void)afterState;
        (void)offset;
        (void)size;
    }
    void TextureBarrier(RHI::TexturePtr texture,
                        RHI::ResourceState beforeState,
                        RHI::ResourceState afterState,
                        uint32_t mipLevel = 0,
                        uint32_t arrayIndex = 0,
                        uint32_t mipCount = 0,
                        uint32_t arrayCount = 0) override
    {
        (void)texture;
        (void)beforeState;
        (void)afterState;
        (void)mipLevel;
        (void)arrayIndex;
        (void)mipCount;
        (void)arrayCount;
    }
};

class FakeDevice final : public RHI::IDevice
{
public:
    // BindSparse で結ばれたページ（どのテクスチャへどのページを結んだか）
    struct BoundPage
    {
        const RHI::ITexture* Texture = nullptr;
        const RHI::ISparseMemoryBlock* Block = nullptr;
        uint64_t OffsetBytes = 0;
        bool bTail = false;
    };

    RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override { return MakeShared<FakeBuffer>(desc); }
    bool BindSparse(const RHI::SparseBindRequest& request) override
    {
        ++BindSparseCalls;
        for (const RHI::SparseTileBind& tile : request.Tiles)
        {
            if (!tile.Page.IsValid())
            {
                ++UnboundTileCount;
                continue;
            }
            BoundPages.push_back({tile.Texture, tile.Page.Block, tile.Page.OffsetBytes, false});
        }
        for (const RHI::SparseMipTailBind& tail : request.MipTails)
        {
            BoundPages.push_back({tail.Texture, tail.Page.Block, tail.Page.OffsetBytes, true});
        }
        return true;
    }
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
    int BindSparseCalls = 0;
    // BindSparse で「外す」と指定されたタイルの数
    int UnboundTileCount = 0;
    VariableArray<BoundPage> BoundPages;
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
        // 結ぶタイルは、結ぶ前にコピーが積まれていること（結んだページが未コピーのまま描画から読めてはいけない）。
        // 外すタイルは対象外
        for (const RHI::SparseTileBind& tile : request.Tiles)
        {
            if (!tile.Page.IsValid())
            {
                continue;
            }
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
            if (tile.Page.IsValid())
            {
                BoundTiles.push_back({tile.MipLevel, tile.TileX, tile.TileY});
            }
            else
            {
                UnboundTiles.push_back({tile.MipLevel, tile.TileX, tile.TileY});
            }
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

    void GetPendingCopies(uint32_t& outCount, uint64_t& outBytes) const override
    {
        outCount = PendingCopyCount;
        outBytes = PendingCopyBytes;
    }

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

    void AbandonRegion(const RHI::TexturePtr&, const RHI::TextureRegionCopy& region) override
    {
        AbandonedRegions.push_back({region.MipLevel, region.OffsetX / 128, region.OffsetY / 128});
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
    // BindSparse で外されたタイル（外した順）
    VariableArray<BoundTile> UnboundTiles;
    // AbandonRegion で未記録のコピーを無効にした領域
    VariableArray<BoundTile> AbandonedRegions;
    int BindCalls = 0;
    bool bFailBind = false;
    // 積めるコピーの残り数（負は無制限、0 でリングが満杯のように積めない）
    int CopyBudget = -1;
    // 次の記録で確実にコピーできる量（アップローダの残りの量）
    uint64_t CopyBytesAvailable = ~0ull;
    // 積んだがまだ記録していないコピー（Abort で未記録へ戻ったものなど）
    uint32_t PendingCopyCount = 0;
    uint64_t PendingCopyBytes = 0;
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
    // ミップ単位の扱い（MipGranularMaxDimension）は、既存のテストがタイル単位を前提にしているので、
    // granularMaxDimension を指定したテストだけが有効にする。
    explicit Harness(const VirtualTextureStreamerConfig& config = VirtualTextureStreamerConfig(),
                     uint64_t poolBlockBytes = SparsePagePool::DefaultBlockBytes,
                     uint64_t poolLimitBytes = 0,
                     bool bUseRetireQueue = false,
                     uint32_t granularMaxDimension = 0)
        : Device(MakeShared<FakeDevice>()),
          Pool(Device, poolBlockBytes),
          Streamer(Pool, Gpu, bUseRetireQueue ? &Retire : nullptr, WithGranular(config, granularMaxDimension))
    {
        Pool.SetCapacityLimitBytes(poolLimitBytes);
    }

    static VirtualTextureStreamerConfig WithGranular(VirtualTextureStreamerConfig config, uint32_t granularMaxDimension)
    {
        config.MipGranularMaxDimension = granularMaxDimension;
        return config;
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

// 実物の TileUploader・GpuRetireQueue・本番の窓口（DeviceVirtualTextureGpu）につないだ道具一式。
// 結び付けとコピーの記録先だけが偽物（デバイスとコマンドリスト）。プールは 2 ページだけ持てて、解除したページを
// すぐ別のテクスチャが借り直す。ストリーマより長く生きる物を先に宣言する。
struct RealUploaderHarness
{
    // 既存のテストはタイル単位を前提にするので、ミップ単位の扱いは無効（poolPages が 2 のときの既定）
    explicit RealUploaderHarness(const VirtualTextureStreamerConfig& config = VirtualTextureStreamerConfig(),
                                 uint32_t poolPages = 2,
                                 uint32_t granularMaxDimension = 0)
        : Device(MakeShared<FakeDevice>()),
          Pool(Device, poolPages * SparsePagePool::PageSizeBytes),
          Uploader(Device, MakeUploaderConfig()),
          Gpu(Device, Uploader),
          Streamer(Pool, Gpu, &Retire, Harness::WithGranular(config, granularMaxDimension))
    {
        Pool.SetCapacityLimitBytes(poolPages * SparsePagePool::PageSizeBytes);
    }

    static TileUploader::Config MakeUploaderConfig()
    {
        TileUploader::Config config;
        config.RingBytes = 1ull * 1024ull * 1024ull;
        config.FrameCopyLimitBytes = 512ull * 1024ull;
        return config;
    }

    // テクスチャを作って登録する（番号と、こちらでも持つテクスチャを返す）
    uint32_t Register(RHI::TexturePtr& outTexture)
    {
        outTexture = MakeShared<FakeSparseTexture>();
        VirtualTextureRegistration registration;
        registration.Texture = outTexture;
        registration.Format = TestFormat;
        registration.Width = TestWidth;
        registration.Height = TestHeight;
        registration.Source = Source;
        registration.TailData = MakeTailData();
        return Streamer.RegisterTexture(std::move(registration));
    }

    // RenderThread のフレームの流れ: 開始 → Update → 記録 →（提出か Abort）
    void BeginFrame(uint64_t completedSerial)
    {
        Retire.BeginFrame(completedSerial);
        Uploader.BeginFrame(completedSerial);
    }

    VirtualTextureFrameResult Update(const VirtualTextureRequestSet* requests = nullptr)
    {
        return Streamer.Update(++Frame, requests);
    }

    FakeCommandList Record()
    {
        FakeCommandList commandList;
        Uploader.RecordCopies(commandList);
        return commandList;
    }

    void Commit(uint64_t serial)
    {
        Uploader.CommitFrame(serial);
        Retire.CommitFrame(serial);
    }

    void Abort()
    {
        Uploader.AbortFrame();
        Retire.AbortFrame();
    }

    // texture へ結んだページ（ブロックとオフセット）の数
    size_t CountBoundPages(const RHI::ITexture* texture) const
    {
        size_t count = 0;
        for (const FakeDevice::BoundPage& page : Device->BoundPages)
        {
            if (page.Texture == texture)
            {
                ++count;
            }
        }
        return count;
    }

    TSharedPtr<FakeDevice> Device;
    SparsePagePool Pool;
    GpuRetireQueue Retire;
    TileUploader Uploader;
    DeviceVirtualTextureGpu Gpu;
    TSharedPtr<FakeSource> Source = MakeShared<FakeSource>();
    VirtualTextureStreamer Streamer;
    uint64_t Frame = 0;
};

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
}

// 1 フレームの上限は最初の 1 件にも掛かる。上限を超えるミップテイルは、公開しないまま数フレームに分けて書く。
void TestTailSplitsAcrossFramesWithoutPublishing()
{
    const uint64_t tailBytes = MakeTailData().size();

    // コピーの数: ミップテイルは 8 段なので、1 フレームに 3 件までなら 3 + 3 + 2 に分かれる
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 3;
        Harness h(config);
        const uint32_t index = h.Register();

        VirtualTextureRequestSet requests;
        AddRequest(requests, index, 2, 0, 0, 1);

        const VirtualTextureFrameResult first = h.Step(&requests);
        Expect(first.CopiesEnqueued == 3, "最初の 1 フレームでも上限の 3 件までしかコピーを積まない");
        Expect(!h.Streamer.IsMipTailResident(index), "全部を積み終えるまで、ミップテイルは使えない");
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1 && h.Gpu.Events.back().Tails == 1,
               "最初の段階でページを結ぶ");
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Init) == 1, "初期化の遷移は最初の段階で 1 回");

        const VirtualTextureFrameResult second = h.Step(&requests);
        Expect(second.CopiesEnqueued == 3 && !h.Streamer.IsMipTailResident(index), "続きのフレームも上限の 3 件まで");
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1, "続きのフレームではページを結び直さない");
        Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::None,
               "公開前の要求は取り込まない");

        const VirtualTextureFrameResult third = h.Step(&requests);
        Expect(third.CopiesEnqueued == 2 && h.Streamer.IsMipTailResident(index), "残りの 2 件を積んだら使える");
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Bind) == 1 && h.Gpu.CountEvents(FakeGpu::EventKind::Init) == 1,
               "結び付けも初期化も 1 回だけ");
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Copy) == 8, "8 段をちょうど 1 回ずつコピーする");
        Expect(h.Pool.GetStats().UsedBytes == SparsePagePool::PageSizeBytes, "ミップテイルは 1 ページ");
        Expect(h.Gpu.UncopiedBindViolations == 0, "結ぶ前に初期化と最初のコピーを積んである");

        h.Step(&requests);
        Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Reading,
               "公開してからの要求は取り込む");
    }
    // コピーの数が 1 でも、1 フレーム 1 件ずつ進んで常駐する
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 1;
        Harness h(config);
        const uint32_t index = h.Register();
        for (int frame = 0; frame < 7; ++frame)
        {
            const VirtualTextureFrameResult step = h.Step();
            Expect(step.CopiesEnqueued == 1 && !h.Streamer.IsMipTailResident(index), "1 フレームに 1 件ずつ積む");
        }
        Expect(h.Step().CopiesEnqueued == 1 && h.Streamer.IsMipTailResident(index), "8 フレーム目で全部積み終える");
    }
    // コピーの量: 1 枚目の後の残りに 2 枚目の最初の 2 段（32768 + 8192）だけが入る
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = tailBytes + 32768 + 8192;
        Harness h(config);
        const uint32_t first = h.Register();
        const uint32_t second = h.Register();
        const VirtualTextureFrameResult one = h.Step();
        Expect(one.CopiedBytes == config.MaxCopyBytesPerFrame && one.CopiesEnqueued == 10,
               "2 枚目は上限に収まる最初の 2 段だけを積む");
        Expect(h.Streamer.IsMipTailResident(first) && !h.Streamer.IsMipTailResident(second), "2 枚目は残りを次のフレームで積む");
        const VirtualTextureFrameResult two = h.Step();
        Expect(two.CopiesEnqueued == 6 && h.Streamer.IsMipTailResident(second), "次のフレームで残りの 6 段を積んで使える");
        Expect(h.Gpu.BindCalls == 1 && h.Gpu.Events[h.Gpu.Events.size() - 1].Kind == FakeGpu::EventKind::Copy,
               "結び付けは最初のフレームの 1 回だけ（続きはコピーだけ）");
    }
    // 続きのコピーを積めないときは、積み直す（カーソルを進めない）
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 4;
        Harness h(config);
        const uint32_t index = h.Register();
        h.Step();
        h.Gpu.CopyBudget = 0;
        const VirtualTextureFrameResult blocked = h.Step();
        Expect(blocked.CopiesEnqueued == 0 && !h.Streamer.IsMipTailResident(index), "コピーを積めないフレームは進まない");
        h.Gpu.CopyBudget = -1;
        h.Step();
        Expect(h.Streamer.IsMipTailResident(index) && h.Gpu.CountEvents(FakeGpu::EventKind::Copy) == 8,
               "積めるようになったら残りを積み、二重にならない");
    }
    // 結び付けに失敗したら、最初の段階で積んだコピーも取り消す
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 4;
        Harness h(config);
        const uint32_t index = h.Register();
        h.Gpu.bFailBind = true;
        h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Copy) == 0 && h.Gpu.CountEvents(FakeGpu::EventKind::Init) == 0,
               "結び付けに失敗したら、積んだ初期化とコピーを取り消す");
        Expect(h.Pool.GetStats().UsedBytes == 0 && !h.Streamer.IsMipTailResident(index), "ページも返る");
        h.Gpu.bFailBind = false;
        h.Step();
        h.Step();
        Expect(h.Streamer.IsMipTailResident(index) && h.Gpu.CountEvents(FakeGpu::EventKind::Copy) == 8, "次から最初からやり直す");
    }
}

// 処理できない上限の設定は、登録で明示的に拒否する（黙って進まない、上限を越えて通す、のどちらにもしない）。
void TestUnprocessableLimitsRejectedAtRegistration()
{
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 0;
        Harness h(config);
        Expect(h.Register() == VirtualTextureStreamer::InvalidIndex, "コピーの数の上限が 0 なら登録を拒否する");
        h.Step();
        Expect(h.Gpu.BindCalls == 0 && h.Pool.GetStats().UsedBytes == 0 && h.Streamer.GetStats().TextureCount == 0,
               "拒否したテクスチャは何も結ばず、ページも借りない");
    }
    {
        VirtualTextureStreamerConfig config;
        config.MaxBindsPerFrame = 0;
        Harness h(config);
        Expect(h.Register() == VirtualTextureStreamer::InvalidIndex, "結び付けの数の上限が 0 なら登録を拒否する");
    }
    {
        // 1 タイル（64 KiB）が入らない量
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = TestTileBytes - 1;
        Harness h(config);
        Expect(h.Register() == VirtualTextureStreamer::InvalidIndex, "1 タイルが入らないコピー量の上限なら登録を拒否する");
    }
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = 100;
        config.MaxCopiesPerFrame = 1;
        Harness h(config);
        Expect(h.Register() == VirtualTextureStreamer::InvalidIndex, "1 件が上限を超える設定を、最初の 1 件だけ通すことはしない");
        h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Copy) == 0, "コピーを 1 件も積まない");
    }
    {
        // ちょうど 1 タイルが入る量なら登録できて、常駐する
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = TestTileBytes;
        Harness h(config);
        const uint32_t index = h.Register();
        Expect(index != VirtualTextureStreamer::InvalidIndex, "1 タイルが入る上限なら登録できる");
        h.Step();
        Expect(h.Streamer.IsMipTailResident(index), "ミップテイルも上限の中で常駐する");
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

// 優先度（粗いミップ → 要求の件数 → 最近）。要求の件数は、GPU が書いたバッファを復号した語の数で、
// 同じタイルの語が多いほど（画面に大きく映るほど）先に読む。
void TestPriorityUsesDecodedRequestCount()
{
    namespace Feedback = Core::Rendering::VirtualTextureFeedback;

    VirtualTextureStreamerConfig config;
    config.MaxReadsStartedPerFrame = 1;
    Harness h(config);
    const uint32_t index = h.RegisterAndMakeTailResident();

    // ヘッダ + 要求の語から成るバッファを作って集合へ足す（tile の語を count 個並べる）
    VirtualTextureRequestSet requests;
    auto addBuffer = [&](const VirtualTextureTileKey& tile, uint32_t count, uint64_t frame)
    {
        VariableArray<uint32_t> words(Feedback::HeaderWords + count, 0u);
        words[0] = count;
        for (uint32_t i = 0; i < count; ++i)
        {
            words[Feedback::HeaderWords + i] = Feedback::Pack(tile);
        }
        requests.AddFeedbackBuffer(words.data(), count, frame);
    };
    addBuffer(MakeKey(index, 1, 0, 0), 1, 90);  // 最近だが、1 画素ぶん
    addBuffer(MakeKey(index, 1, 1, 0), 3, 10);  // 古いが、3 画素ぶん
    addBuffer(MakeKey(index, 1, 0, 1), 3, 50);  // 3 画素ぶんで、B より新しい
    addBuffer(MakeKey(index, 2, 0, 0), 1, 1);   // 粗いミップは画素の数が少なくても先

    uint32_t hitsOfSmall = 0;
    uint32_t hitsOfLarge = 0;
    for (const Core::Rendering::VirtualTextureTileRequest& request : requests.GetRequests(index))
    {
        if (request.Mip == 1 && request.X == 0 && request.Y == 0)
        {
            hitsOfSmall = request.HitCount;
        }
        if (request.Mip == 1 && request.X == 1 && request.Y == 0)
        {
            hitsOfLarge = request.HitCount;
        }
    }
    Expect(hitsOfSmall == 1 && hitsOfLarge == 3, "復号した要求の語の数が、そのタイルの要求の件数になる");

    h.Step(&requests);
    for (int i = 0; i < 3; ++i)
    {
        h.Step();
    }
    Expect(h.Source->Started.size() == 4, "4 件を 1 フレームに 1 件ずつ始める");
    Expect(h.Source->Started[0] == MakeKey(index, 2, 0, 0), "最も粗いミップが先");
    Expect(h.Source->Started[1] == MakeKey(index, 1, 0, 1), "同じミップでは件数が多く、最近のものが先");
    Expect(h.Source->Started[2] == MakeKey(index, 1, 1, 0), "件数が同じなら、古いほうが後");
    Expect(h.Source->Started[3] == MakeKey(index, 1, 0, 0), "件数が少ないものは、最近でも最後");
}

// 実物の TileUploader・GpuRetireQueue・本番の窓口を使い、「コピーを記録 → Abort → 登録解除 → ページの再取得 → 次のフレーム」
// で、古いコピーが再利用されたページへ記録されないことを確かめる。
void TestAbortedCopiesNeverReachReusedPages()
{
    RealUploaderHarness h;
    RHI::TexturePtr textureA;

    // フレーム 1: 登録 → ミップテイルのコピーを積む → 記録 → 提出（serial 1）
    h.BeginFrame(0);
    const uint32_t indexA = h.Register(textureA);
    Expect(indexA != VirtualTextureStreamer::InvalidIndex, "登録できる");
    h.Update();
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 8, "ミップテイルの 8 段を記録する");
    }
    h.Commit(1);
    Expect(h.Streamer.IsMipTailResident(indexA), "ミップテイルが常駐する");

    // フレーム 2: タイルを要求して読み始める（serial 2）
    h.BeginFrame(1);
    VirtualTextureRequestSet requests;
    AddRequest(requests, indexA, 2, 0, 0, 1);
    h.Update(&requests);
    h.Record();
    h.Commit(2);

    // フレーム 3: タイルを結んでコピーを積み、記録する。ここでフレームを提出せずに捨て（Abort）、
    // その前にテクスチャの登録を解除する
    h.BeginFrame(2);
    h.Update();
    Expect(h.Streamer.GetTileState(MakeKey(indexA, 2, 0, 0)) == VirtualTextureTileState::Resident, "タイルを結んだ");
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 1, "タイルのコピーを記録した（まだ提出していない）");
    }
    Expect(h.Pool.GetStats().UsedBytes == 2 * SparsePagePool::PageSizeBytes, "ミップテイルとタイルでプールの 2 ページを使い切る");
    h.Streamer.UnregisterTexture(indexA);
    h.Abort();

    // フレーム 4: 直前までに提出済みの serial 2 が完了している。解除したページは戻り、別のテクスチャが借り直す
    h.BeginFrame(2);
    Expect(h.Pool.GetStats().UsedBytes == 0, "解除したページは、使った提出の完了でプールへ戻る");
    RHI::TexturePtr textureB;
    const uint32_t indexB = h.Register(textureB);
    Expect(indexB != VirtualTextureStreamer::InvalidIndex, "別のテクスチャを登録できる");
    h.Update();

    // B のミップテイルのページは、A が使っていたページを借り直したもの
    VariableArray<FakeDevice::BoundPage> pagesOfA;
    for (const FakeDevice::BoundPage& page : h.Device->BoundPages)
    {
        if (page.Texture == textureA.get())
        {
            pagesOfA.push_back(page);
        }
    }
    bool bReused = false;
    for (const FakeDevice::BoundPage& page : h.Device->BoundPages)
    {
        if (page.Texture != textureB.get())
        {
            continue;
        }
        for (const FakeDevice::BoundPage& old : pagesOfA)
        {
            bReused = bReused || (old.Block == page.Block && old.OffsetBytes == page.OffsetBytes);
        }
    }
    Expect(pagesOfA.size() == 2 && bReused, "B は A が使っていたページを再び結ぶ");

    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 0, "解除したテクスチャ宛ての古いコピーは、再利用されたページへ記録されない");
        Expect(commands.CountCopiesTo(textureB.get()) == 8 && commands.Copies.size() == 8,
               "記録されるのは B のミップテイルの 8 段だけ");
    }
    h.Commit(3);
    Expect(h.Uploader.GetStats().PendingCopies == 0 && h.Uploader.GetStats().InFlightCopies == 8,
           "提出したのは B の 8 件だけで、未記録のコピーが残らない");
    Expect(h.Streamer.IsMipTailResident(indexB), "B のミップテイルが常駐する");
}

// 上のテストの対照: 登録したままフレームを Abort すると、積んだコピーは出し直される（記録を観測できている証拠）。
void TestAbortedCopiesAreRerecordedWhileRegistered()
{
    RealUploaderHarness h;
    RHI::TexturePtr textureA;

    h.BeginFrame(0);
    const uint32_t indexA = h.Register(textureA);
    h.Update();
    h.Record();
    h.Commit(1);

    h.BeginFrame(1);
    VirtualTextureRequestSet requests;
    AddRequest(requests, indexA, 2, 0, 0, 1);
    h.Update(&requests);
    h.Record();
    h.Commit(2);

    h.BeginFrame(2);
    h.Update();
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 1, "タイルのコピーを記録した");
    }
    h.Abort();

    h.BeginFrame(2);
    h.Update();
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 1, "登録したままなら、捨てたフレームのコピーを次のフレームで出し直す");
    }
    h.Commit(3);
    Expect(h.Streamer.GetTileState(MakeKey(indexA, 2, 0, 0)) == VirtualTextureTileState::Resident, "常駐のまま");
}

// Abort で未記録へ戻ったコピーも、次のフレームの上限に算入する。偽の窓口で数の算入を、実物のアップローダで
// 「記録 → Abort → 次のフレーム」で上限を超えて記録されないことを確かめる。
void TestPendingCopiesCountAgainstFrameLimits()
{
    // 偽の窓口: 持ち越したコピーの分だけ、今フレームで積める数が減る
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 8;
        Harness h(config);
        const uint32_t index = h.Register();
        h.Gpu.PendingCopyCount = 1;
        const VirtualTextureFrameResult first = h.Step();
        Expect(first.CopiesEnqueued == 7 && !h.Streamer.IsMipTailResident(index),
               "持ち越したコピーが 1 件あると、積める段は 8 から 1 引いた 7 段で、積み切るまで使えない");
        h.Gpu.PendingCopyCount = 0;
        Expect(h.Step().CopiesEnqueued == 1 && h.Streamer.IsMipTailResident(index), "持ち越しが無くなれば、残りの 1 段を積んで使える");
    }
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopyBytesPerFrame = MakeTailData().size();
        Harness h(config);
        h.Register();
        h.Gpu.PendingCopyBytes = 1;
        h.Step();
        Expect(h.Gpu.CountEvents(FakeGpu::EventKind::Copy) < 8,
               "持ち越したコピーの量だけ、今フレームで積める量が減る（1 フレームで 8 段を積み切れない）");
    }

    // 実物のアップローダ: 1 フレームに 1 件の上限で、記録 → Abort → 次のフレームでも記録は 1 件を超えない
    {
        VirtualTextureStreamerConfig config;
        config.MaxCopiesPerFrame = 1;
        RealUploaderHarness h(config);
        RHI::TexturePtr texture;

        h.BeginFrame(0);
        const uint32_t index = h.Register(texture);
        Expect(index != VirtualTextureStreamer::InvalidIndex, "1 段が 1 件に入る設定は登録できる");
        h.Update();
        {
            const FakeCommandList commands = h.Record();
            Expect(commands.Copies.size() == 1, "最初のフレームは 1 段だけ記録する");
        }
        h.Abort();

        // 捨てたフレームの 1 段が未記録へ戻っている。次のフレームはそれを出し直すだけで、新しい段を足さない
        h.BeginFrame(0);
        h.Update();
        {
            const FakeCommandList commands = h.Record();
            Expect(commands.Copies.size() == 1, "Abort で戻った 1 段と新しい段を合わせて、上限の 1 件を超えて記録しない");
        }
        h.Commit(1);
        Expect(!h.Streamer.IsMipTailResident(index), "全部の段を積み終えるまで、ミップテイルは使えない");

        // 以降も 1 フレームに 1 段ずつ進み、8 段を積み終えたら使える
        uint64_t serial = 1;
        for (int frame = 0; frame < 16 && !h.Streamer.IsMipTailResident(index); ++frame)
        {
            h.BeginFrame(serial);
            h.Update();
            const FakeCommandList commands = h.Record();
            Expect(commands.Copies.size() <= 1, "どのフレームも記録は 1 件以内");
            h.Commit(++serial);
        }
        Expect(h.Streamer.IsMipTailResident(index), "段を 1 件ずつ積み終えると、ミップテイルが常駐する");
    }
}

// 「コピーを記録 → Abort → 登録解除 → ページの再取得 → 次のフレーム」の順（登録解除が Abort より後）でも、
// 古いコピーが再利用されたページへ記録されない。
void TestAbortThenUnregisterNeverReachesReusedPages()
{
    RealUploaderHarness h;
    RHI::TexturePtr textureA;

    h.BeginFrame(0);
    const uint32_t indexA = h.Register(textureA);
    h.Update();
    h.Record();
    h.Commit(1);

    h.BeginFrame(1);
    VirtualTextureRequestSet requests;
    AddRequest(requests, indexA, 2, 0, 0, 1);
    h.Update(&requests);
    h.Record();
    h.Commit(2);

    // フレーム 3: タイルのコピーを記録し、フレームを捨て（未記録へ戻る）、そのあとで登録を解除する
    h.BeginFrame(2);
    h.Update();
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 1, "タイルのコピーを記録した（まだ提出していない）");
    }
    h.Abort();
    Expect(h.Uploader.GetStats().PendingCopies == 1, "Abort で記録済みのコピーが未記録へ戻る");
    h.Streamer.UnregisterTexture(indexA);
    Expect(h.Uploader.GetStats().PendingCopies == 0, "解除すると、未記録へ戻ったコピーは無効になる");

    // フレーム 4: 解除したページが戻り、別のテクスチャが借り直す
    h.BeginFrame(2);
    Expect(h.Pool.GetStats().UsedBytes == 0, "解除したページは、使った提出の完了でプールへ戻る");
    RHI::TexturePtr textureB;
    const uint32_t indexB = h.Register(textureB);
    Expect(indexB != VirtualTextureStreamer::InvalidIndex, "別のテクスチャを登録できる");
    h.Update();

    VariableArray<FakeDevice::BoundPage> pagesOfA;
    for (const FakeDevice::BoundPage& page : h.Device->BoundPages)
    {
        if (page.Texture == textureA.get())
        {
            pagesOfA.push_back(page);
        }
    }
    bool bReused = false;
    for (const FakeDevice::BoundPage& page : h.Device->BoundPages)
    {
        if (page.Texture != textureB.get())
        {
            continue;
        }
        for (const FakeDevice::BoundPage& old : pagesOfA)
        {
            bReused = bReused || (old.Block == page.Block && old.OffsetBytes == page.OffsetBytes);
        }
    }
    Expect(pagesOfA.size() == 2 && bReused, "B は A が使っていたページを再び結ぶ");

    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(textureA.get()) == 0, "解除したテクスチャ宛ての古いコピーは、再利用されたページへ記録されない");
        Expect(commands.CountCopiesTo(textureB.get()) == 8 && commands.Copies.size() == 8,
               "記録されるのは B のミップテイルの 8 段だけ");
    }
    h.Commit(3);
    Expect(h.Uploader.GetStats().PendingCopies == 0 && h.Uploader.GetStats().InFlightCopies == 8,
           "提出したのは B の 8 件だけで、未記録のコピーが残らない");
    Expect(h.Streamer.IsMipTailResident(indexB), "B のミップテイルが常駐する");
}

// ---- 追い出し（LRU・目標・ミップ単位）----

constexpr uint64_t PageBytes = SparsePagePool::PageSizeBytes;

// 指定したタイルだけを、同じ要求のフレームで要求して 1 フレーム進める。tiles は {ミップ, x, y}。
struct TileRef
{
    uint32_t Mip;
    uint32_t X;
    uint32_t Y;
};

VirtualTextureFrameResult StepRequesting(Harness& h, uint32_t texture, std::initializer_list<TileRef> tiles, uint64_t frame)
{
    VirtualTextureRequestSet requests;
    for (const TileRef& tile : tiles)
    {
        AddRequest(requests, texture, tile.Mip, tile.X, tile.Y, frame);
    }
    return h.Step(&requests);
}

bool IsUnbound(const FakeGpu& gpu, size_t order, uint32_t mip, uint32_t x, uint32_t y)
{
    return order < gpu.UnboundTiles.size() && gpu.UnboundTiles[order].Mip == mip && gpu.UnboundTiles[order].X == x &&
           gpu.UnboundTiles[order].Y == y;
}

// 目標を下げたときの追い出しの順: 使われていないタイルが先（最後に要求したフレームが古い順 = LRU）、
// 次に使われているタイルを細かいミップから。そのたびに、ストリーマの量もプールの使用量も目標以下に収まる。
void TestShrinkBudgetEvictsIdleLruThenFineMips()
{
    VirtualTextureStreamerConfig config;
    config.EvictIdleFrames = 3;
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, true);
    const uint32_t index = h.RegisterAndMakeTailResident();

    // A・B はミップ 0（B の方が最後に要求したフレームが新しい）、C はミップ 1、D はミップ 2
    {
        VirtualTextureRequestSet requests;
        AddRequest(requests, index, 0, 0, 0, 10);
        AddRequest(requests, index, 0, 1, 0, 11);
        AddRequest(requests, index, 1, 0, 0, 12);
        AddRequest(requests, index, 2, 0, 0, 13);
        h.Step(&requests);
    }
    h.Step();
    // A・B は要求が途絶え、C・D は要求され続ける（途絶えた A・B が「使われていない」になるまで）
    for (uint64_t frame = 20; frame < 25; ++frame)
    {
        StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, frame);
    }
    Expect(h.Streamer.GetStats().ResidentTiles == 4, "4 タイルが常駐している");
    Expect(h.Streamer.GetResidentBytes() == 5 * PageBytes, "ミップテイル 1 ページ + タイル 4 ページ");
    Expect(h.Pool.GetStats().UsedBytes == 5 * PageBytes, "プールの使用量もそろっている");
    Expect(h.Gpu.UnboundTiles.empty(), "目標が無い間は外さない");

    const auto resident = [&](uint32_t mip, uint32_t x, uint32_t y)
    { return h.Streamer.GetTileState(MakeKey(index, mip, x, y)) == VirtualTextureTileState::Resident; };

    // 目標 4 ページ: 使われていない A・B のうち、最後に要求したフレームが古い A だけが外れる
    h.Streamer.SetResidentBudget(true, 4 * PageBytes);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 30);
    Expect(IsUnbound(h.Gpu, 0, 0, 0, 0) && h.Gpu.UnboundTiles.size() == 1, "最初に外れるのは、最後に要求したフレームが最も古い A");
    Expect(!resident(0, 0, 0) && resident(0, 1, 0) && resident(1, 0, 0) && resident(2, 0, 0), "B・C・D は残る");
    Expect(h.Streamer.GetResidentBytes() <= 4 * PageBytes && h.Pool.GetStats().UsedBytes <= 4 * PageBytes,
           "目標 4 ページ以下に収まる");
    Expect(h.Streamer.GetStats().EvictedTiles == 1, "外したタイルの数");

    // 目標 3 ページ: 次は B
    h.Streamer.SetResidentBudget(true, 3 * PageBytes);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 31);
    Expect(IsUnbound(h.Gpu, 1, 0, 1, 0) && h.Gpu.UnboundTiles.size() == 2, "次に外れるのは使われていない B");
    Expect(resident(1, 0, 0) && resident(2, 0, 0), "使われている C・D は残る");
    Expect(h.Streamer.GetResidentBytes() <= 3 * PageBytes && h.Pool.GetStats().UsedBytes <= 3 * PageBytes,
           "目標 3 ページ以下に収まる");

    // 目標 2 ページ: 残りは使われているタイルだけ。細かいミップの C（ミップ 1）が先に外れる
    h.Streamer.SetResidentBudget(true, 2 * PageBytes);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 32);
    Expect(IsUnbound(h.Gpu, 2, 1, 0, 0) && h.Gpu.UnboundTiles.size() == 3, "使われているタイルは細かいミップから外れる");
    Expect(!resident(1, 0, 0) && resident(2, 0, 0), "粗いミップの D は残る");
    Expect(h.Streamer.GetResidentBytes() <= 2 * PageBytes && h.Pool.GetStats().UsedBytes <= 2 * PageBytes,
           "目標 2 ページ以下に収まる");

    // 要求され続ける C は、目標に収まらない間は読まれも結ばれもしない（優先度の低い要求は結ばない）
    for (uint64_t frame = 33; frame < 38; ++frame)
    {
        StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, frame);
    }
    Expect(!resident(1, 0, 0) && resident(2, 0, 0), "目標が足りない間、より細かい C は結ばれず、粗い D も外れない");
    Expect(h.Gpu.UnboundTiles.size() == 3, "外れたのは 3 タイルのまま（結び直しと外しを繰り返さない）");

    // 目標 1 ページ（ミップテイルだけ）: D も外れる。ミップテイルは外さない
    h.Streamer.SetResidentBudget(true, 1 * PageBytes);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 40);
    Expect(IsUnbound(h.Gpu, 3, 2, 0, 0) && h.Gpu.UnboundTiles.size() == 4, "最後に D が外れる");
    Expect(h.Streamer.IsMipTailResident(index), "ミップテイルは外さない");
    Expect(h.Streamer.GetResidentBytes() == PageBytes && h.Pool.GetStats().UsedBytes == PageBytes,
           "ミップテイルの 1 ページだけが残る");
    Expect(h.Streamer.GetStats().EvictedTiles == 4, "外したタイルの累計");

    // 目標 0: ミップテイルは外せないので、タイルが無くてもミップテイルの分は残る
    h.Streamer.SetResidentBudget(true, 0);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 41);
    Expect(h.Streamer.IsMipTailResident(index) && h.Streamer.GetResidentBytes() == PageBytes,
           "目標がミップテイルより小さくても、ミップテイルは外れない");
    Expect(h.Gpu.UnboundTiles.size() == 4, "外すタイルが無いので何も外さない");

    // 目標を外すと、要求され続けるタイルが再び常駐できる
    h.Streamer.SetResidentBudget(false, 0);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 42);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 43);
    Expect(resident(1, 0, 0) && resident(2, 0, 0), "目標を外すと要求されているタイルが結ばれる");
}

// 結びたいタイルのために外せるのは、使われていないタイルか、結びたいタイルより細かいミップの使われているタイルだけ。
// 外すタイルと結ぶタイルは同じ BindSparse に入り、外したページは同じフレームでは別のタイルに渡らない。
void TestRoomOnlyFromLowerPriorityTiles()
{
    VirtualTextureStreamerConfig config;
    config.EvictIdleFrames = 1000; // このテストでは「使われていない」扱いにしない
    config.WantedMaxAgeFrames = 5;
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, true);
    const uint32_t index = h.RegisterAndMakeTailResident();
    h.Streamer.SetResidentBudget(true, 3 * PageBytes);

    // X・Y（ミップ 0）を同時に読み始める。目標はこの時点でタイル 2 枚ぶんある
    StepRequesting(h, index, {{0, 0, 0}, {0, 1, 0}}, 1);
    Expect(h.Source->Started.size() == 2, "目標に収まる 2 枚を読み始める");
    // 読み終わるまでに目標がタイル 1 枚ぶんへ下がる
    h.Streamer.SetResidentBudget(true, 2 * PageBytes);
    StepRequesting(h, index, {{0, 0, 0}, {0, 1, 0}}, 2);
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::Resident,
           "優先度の高い X（同順位では印が小さい方）が先に結ばれる");
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 1, 0)) == VirtualTextureTileState::Ready,
           "同じミップの使われている X を外してまで Y は結ばない");
    Expect(h.Streamer.GetStats().BudgetBlockedFrames >= 1, "目標が足りず結べなかったフレームを数える");
    Expect(h.Gpu.UnboundTiles.empty(), "同じ優先度のタイルは外さない");

    // ミップ 2 の Z が要求される。Z は X より粗いので、X を外して結ぶ。同じ BindSparse で
    const int callsBefore = h.Gpu.BindCalls;
    StepRequesting(h, index, {{0, 0, 0}, {0, 1, 0}, {2, 0, 0}}, 3);
    StepRequesting(h, index, {{0, 0, 0}, {0, 1, 0}, {2, 0, 0}}, 4);
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Resident, "粗いミップの Z が結ばれる");
    Expect(h.Gpu.UnboundTiles.size() == 1 && IsUnbound(h.Gpu, 0, 0, 0, 0), "Z のために、より細かい X が外れる");
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) != VirtualTextureTileState::Resident, "X は常駐していない");
    Expect(h.Gpu.BindCalls == callsBefore + 1, "外す X と結ぶ Z は同じ BindSparse");
    Expect(h.Streamer.GetResidentBytes() <= 2 * PageBytes && h.Pool.GetStats().UsedBytes <= 2 * PageBytes, "目標に収まる");

    // 結べないまま要求が途絶えた読み込み済みのタイルは、いつまでも枠を占めずに忘れる
    for (int i = 0; i < 8; ++i)
    {
        StepRequesting(h, index, {{2, 0, 0}}, 10 + static_cast<uint64_t>(i));
    }
    Expect(h.Streamer.GetStats().StaleDropped >= 1, "要求が途絶えた読み込み済みのタイルを忘れる");
}

// 外す BindSparse が失敗したら何も変えず、次のフレームでやり直す
void TestEvictionBindFailureKeepsTilesResident()
{
    VirtualTextureStreamerConfig config;
    config.EvictIdleFrames = 1;
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, true);
    const uint32_t index = h.RegisterAndMakeTailResident();
    StepRequesting(h, index, {{1, 0, 0}}, 1);
    h.Step();
    for (int i = 0; i < 3; ++i)
    {
        h.Step();
    }
    Expect(h.Streamer.GetTileState(MakeKey(index, 1, 0, 0)) == VirtualTextureTileState::Resident, "タイルが常駐している");

    h.Streamer.SetResidentBudget(true, 1 * PageBytes);
    h.Gpu.bFailBind = true;
    h.Step();
    Expect(h.Streamer.GetTileState(MakeKey(index, 1, 0, 0)) == VirtualTextureTileState::Resident,
           "外す BindSparse が失敗したタイルは常駐のまま");
    Expect(h.Streamer.GetStats().EvictedTiles == 0 && h.Pool.GetStats().UsedBytes == 2 * PageBytes,
           "失敗したときはページを返さない");
    h.Gpu.bFailBind = false;
    h.Step();
    Expect(h.Streamer.GetTileState(MakeKey(index, 1, 0, 0)) == VirtualTextureTileState::None, "次のフレームで外れる");
    Expect(h.Streamer.GetStats().EvictedTiles == 1 && h.Pool.GetStats().UsedBytes == PageBytes, "ページが戻る");
}

// 実物のアップローダとリトアキューで: 外したページは、外したフレームでは別のタイルに渡らず、最後に提出したフレームの完了まで
// 再利用されない。外すタイル宛ての未記録のコピーは無効になる。
void TestEvictedPageWaitsForRetireAndStaleCopyIsCancelled()
{
    VirtualTextureStreamerConfig config;
    config.EvictIdleFrames = 3;
    RealUploaderHarness h(config, 3);
    RHI::TexturePtr texture;

    h.BeginFrame(0);
    const uint32_t index = h.Register(texture);
    Expect(index != VirtualTextureStreamer::InvalidIndex, "登録できる");
    h.Streamer.SetResidentBudget(true, 3 * PageBytes);
    h.Update();
    h.Record();
    h.Commit(1);

    // A・B（ミップ 0）を要求して結ぶ。プール 3 ページ（ミップテイル 1 + A・B）が埋まる
    h.BeginFrame(1);
    {
        VirtualTextureRequestSet requests;
        AddRequest(requests, index, 0, 0, 0, 1);
        AddRequest(requests, index, 0, 1, 0, 1);
        h.Update(&requests);
    }
    h.Record();
    h.Commit(2);
    h.BeginFrame(2);
    h.Update();
    Expect(h.Streamer.GetStats().ResidentTiles == 2, "A・B を結んだ");
    Expect(h.Pool.GetStats().UsedBytes == 3 * PageBytes, "プールの 3 ページを使い切る");
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(texture.get()) == 2, "A・B のコピーを記録した（まだ提出していない）");
    }
    // フレームを提出できず捨てる（コピーは未記録へ戻る）。その直後に目標を下げる
    h.Abort();
    h.BeginFrame(2);
    h.Streamer.SetResidentBudget(true, 2 * PageBytes);
    h.Update();
    Expect(h.Streamer.GetTileState(MakeKey(index, 0, 0, 0)) == VirtualTextureTileState::None, "目標を超えたぶん A が外れる");
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(texture.get()) == 1 && commands.Copies.size() == 1 &&
                   commands.Copies[0].Region.OffsetX == 128,
               "外した A 宛ての未記録のコピーは無効になり、B のコピーだけが出る");
    }
    h.Commit(3);
    Expect(h.Device->UnboundTileCount == 1, "A を外す指定を BindSparse へ出した");
    Expect(h.Pool.GetStats().UsedBytes == 3 * PageBytes, "外したページは、最後に提出したフレームの完了まで戻らない");

    // B も目標から外れ、より粗い C が要求される。C は外したページを、完了するまでは借りられない
    h.BeginFrame(2);
    h.Streamer.SetResidentBudget(true, 3 * PageBytes);
    {
        VirtualTextureRequestSet requests;
        AddRequest(requests, index, 2, 0, 0, 9);
        h.Update(&requests);
    }
    h.Record();
    h.Commit(4);
    h.BeginFrame(2);
    h.Update();
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Ready,
           "外したページが戻るまで、C は読み込み済みのまま待つ");
    h.Record();
    h.Commit(5);

    h.BeginFrame(3);
    Expect(h.Pool.GetStats().UsedBytes == 2 * PageBytes, "serial 3 の完了で、外したページがプールへ戻る");
    h.Update();
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Resident, "戻ったページで C を結ぶ");

    // C が借りたのは、A が使っていたページ
    VariableArray<FakeDevice::BoundPage> pages;
    for (const FakeDevice::BoundPage& page : h.Device->BoundPages)
    {
        if (page.Texture == texture.get())
        {
            pages.push_back(page);
        }
    }
    // [0] ミップテイル、[1] A、[2] B、[3] C
    Expect(pages.size() == 4 && pages[3].Block == pages[1].Block && pages[3].OffsetBytes == pages[1].OffsetBytes,
           "C は外した A のページを、完了の後に借りる");
    {
        const FakeCommandList commands = h.Record();
        Expect(commands.CountCopiesTo(texture.get()) == 1 && commands.Copies[0].Region.MipLevel == 2,
               "記録されるのは C のコピーだけ");
    }
    h.Commit(6);
}

// ミップ単位のテクスチャ: ミップ全体が揃ってから 1 回の BindSparse で結び、外すときもミップ全体を一緒に外す
void TestMipUnitBindsAndEvictsTogether()
{
    VirtualTextureStreamerConfig config;
    config.EvictIdleFrames = 1000;
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, true, 1024);
    const uint32_t index = h.RegisterAndMakeTailResident();
    h.Source->bAutoComplete = false;

    const auto state = [&](uint32_t mip, uint32_t x, uint32_t y) { return h.Streamer.GetTileState(MakeKey(index, mip, x, y)); };

    // ミップ 2（2 タイル）のうち 1 タイルを要求すると、2 タイルとも読む
    StepRequesting(h, index, {{2, 0, 0}}, 1);
    Expect(h.Source->Started.size() == 2, "1 タイルの要求で、ミップの全タイルを読み始める");
    h.Source->Complete(MakeKey(index, 2, 0, 0), true, TestTileBytes);
    const int callsBefore = h.Gpu.BindCalls;
    h.Step();
    Expect(state(2, 0, 0) == VirtualTextureTileState::Ready && state(2, 1, 0) == VirtualTextureTileState::Reading,
           "ミップの一部だけ読み込み済みの間は結ばない");
    Expect(h.Gpu.BindCalls == callsBefore && h.Gpu.BoundTiles.empty(), "全部が揃うまで BindSparse を出さない");
    h.Source->Complete(MakeKey(index, 2, 1, 0), true, TestTileBytes);
    h.Step();
    Expect(state(2, 0, 0) == VirtualTextureTileState::Resident && state(2, 1, 0) == VirtualTextureTileState::Resident,
           "揃ったら全タイルを結ぶ");
    Expect(h.Gpu.BindCalls == callsBefore + 1 && h.Gpu.Events.back().Kind == FakeGpu::EventKind::Bind &&
               h.Gpu.Events.back().Tiles == 2,
           "ミップの 2 タイルは同じ BindSparse");
    Expect(h.Gpu.UncopiedBindViolations == 0, "結ぶ前に全タイルのコピーを積んである");

    // ミップ 1（8 タイル）
    h.Source->bAutoComplete = true;
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 2);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 3);
    Expect(h.Streamer.GetStats().ResidentTiles == 10, "ミップ 1 の 8 タイルとミップ 2 の 2 タイルが常駐する");
    Expect(h.Gpu.Events.back().Kind == FakeGpu::EventKind::Bind && h.Gpu.Events.back().Tiles == 8,
           "ミップ 1 の 8 タイルは同じ BindSparse");

    // 目標 3 ページ: 使われているミップ 1 とミップ 2 のうち、細かいミップ 1 を丸ごと外す
    h.Streamer.SetResidentBudget(true, 3 * PageBytes);
    StepRequesting(h, index, {{1, 0, 0}, {2, 0, 0}}, 4);
    Expect(h.Gpu.UnboundTiles.size() == 8, "ミップ 1 の 8 タイルを一緒に外す");
    bool bAllMip1 = true;
    for (const FakeGpu::BoundTile& tile : h.Gpu.UnboundTiles)
    {
        bAllMip1 = bAllMip1 && tile.Mip == 1;
    }
    Expect(bAllMip1, "外れたのはミップ 1 だけ");
    Expect(state(2, 0, 0) == VirtualTextureTileState::Resident && state(2, 1, 0) == VirtualTextureTileState::Resident,
           "ミップ 2 は残る");
    Expect(h.Streamer.GetResidentBytes() <= 3 * PageBytes && h.Pool.GetStats().UsedBytes <= 3 * PageBytes, "目標以下に収まる");
    Expect(h.Streamer.GetStats().EvictedTiles == 8, "外したタイルの数（ミップ単位でもタイルの数）");

    // 目標 1 ページ: ミップ 2 も 2 タイル一緒に外れる
    h.Streamer.SetResidentBudget(true, 1 * PageBytes);
    StepRequesting(h, index, {{2, 0, 0}}, 5);
    Expect(h.Gpu.UnboundTiles.size() == 10 && h.Streamer.GetStats().EvictedTiles == 10, "ミップ 2 の 2 タイルも一緒に外れる");
    Expect(h.Streamer.IsMipTailResident(index), "ミップテイルは外さない");
}

// ミップ全体が上限（コピーの数）に収まらないテクスチャは、タイル単位のまま
void TestMipUnitFallsBackToTilesWhenLimitsTooSmall()
{
    VirtualTextureStreamerConfig config;
    config.MaxCopiesPerFrame = 16; // ミップ 0 の 32 タイルが収まらない
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, false, 1024);
    const uint32_t index = h.RegisterAndMakeTailResident();
    StepRequesting(h, index, {{2, 0, 0}}, 1);
    Expect(h.Source->Started.size() == 1, "1 単位が上限に収まらないテクスチャは、要求されたタイルだけを読む");

    // 長辺が閾値を超えるテクスチャも、タイル単位
    Harness large(VirtualTextureStreamerConfig(), SparsePagePool::DefaultBlockBytes, 0, false, 512);
    const uint32_t largeIndex = large.RegisterAndMakeTailResident();
    StepRequesting(large, largeIndex, {{2, 0, 0}}, 1);
    Expect(large.Source->Started.size() == 1, "長辺が閾値を超えるテクスチャはタイル単位");
}

// ミップ単位の読み込みの枠: 始めた単位の残りの枠を確保し、読み込み中の数の上限を超えない
void TestMipUnitReservesReadSlots()
{
    VirtualTextureStreamerConfig config;
    config.MaxReadsInFlight = 32;
    config.MaxReadsStartedPerFrame = 8;
    config.EvictIdleFrames = 1000;
    Harness h(config, SparsePagePool::DefaultBlockBytes, 0, false, 1024);
    const uint32_t index = h.RegisterAndMakeTailResident();

    // ミップ 2（2 タイル）とミップ 0（32 タイル）を同時に要求する。上限は 32 なので、同時には収まらない
    VirtualTextureRequestSet requests;
    AddRequest(requests, index, 2, 0, 0, 1);
    AddRequest(requests, index, 0, 0, 0, 1);
    h.Step(&requests);
    Expect(h.Source->Started.size() == 2, "先に優先度の高いミップ 2 の 2 タイルだけを読み始める（ミップ 0 は収まらない）");
    h.Step();
    Expect(h.Streamer.GetTileState(MakeKey(index, 2, 0, 0)) == VirtualTextureTileState::Resident, "ミップ 2 が結ばれる");
    // ミップ 2 の枠が空き、ミップ 0 の単位が 1 フレーム 8 件ずつ始まる
    size_t maxInFlight = 0;
    for (int i = 0; i < 6; ++i)
    {
        h.Step();
        const VirtualTextureStreamerStats stats = h.Streamer.GetStats();
        maxInFlight = std::max<size_t>(maxInFlight, stats.ReadingTiles + stats.ReadyTiles);
    }
    Expect(maxInFlight <= 32, "読み込み中と読み込み済みの数が上限を超えない");
    Expect(h.Source->Started.size() == 34, "ミップ 0 の 32 タイルを全部読む");
    Expect(h.Streamer.GetStats().ResidentTiles == 34, "ミップ 0 は 32 タイル揃ってから結ばれ、常駐する");
    Expect(h.Gpu.Events.back().Kind == FakeGpu::EventKind::Bind && h.Gpu.Events.back().Tiles == 32,
           "ミップ 0 の 32 タイルは同じ BindSparse");
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
    TestTailSplitsAcrossFramesWithoutPublishing();
    TestUnprocessableLimitsRejectedAtRegistration();
    TestUploaderAvailabilityLimitsStaging();
    TestPriorityPrefersMoreHitsInSameMip();
    TestPriorityUsesDecodedRequestCount();
    TestInvalidRequestsIgnored();
    TestStaleWantedDropped();
    TestUnregisterRetiresPages();
    TestUnregisterAbandonsPendingCopies();
    TestAbortedCopiesNeverReachReusedPages();
    TestAbortedCopiesAreRerecordedWhileRegistered();
    TestPendingCopiesCountAgainstFrameLimits();
    TestAbortThenUnregisterNeverReachesReusedPages();
    TestClearReleasesEverything();
    TestShrinkBudgetEvictsIdleLruThenFineMips();
    TestRoomOnlyFromLowerPriorityTiles();
    TestEvictionBindFailureKeepsTilesResident();
    TestEvictedPageWaitsForRetireAndStaleCopyIsCancelled();
    TestMipUnitBindsAndEvictsTogether();
    TestMipUnitFallsBackToTilesWhenLimitsTooSmall();
    TestMipUnitReservesReadSlots();

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
