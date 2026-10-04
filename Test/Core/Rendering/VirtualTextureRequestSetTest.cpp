// VT のフィードバック（タイルの要求）の契約テスト。
// 1 語へ詰めた要求の復号・同じタイルの重複の除去・溢れた件数の数え方（VirtualTextureRequestSet）と、
// 3つのバッファのリング（VirtualTextureFeedbackRing）が、完了した提出のものだけを GPU を待たずに読み戻して
// 集計へ渡すことを、GPU を使わない偽デバイスで確かめる。
#include "Rendering/VirtualTextureFeedbackRing.h"
#include "Rendering/VirtualTextureRequestSet.h"
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
using Core::Container::VariableArray;
using Core::Rendering::VirtualTextureFeedbackDecodeResult;
using Core::Rendering::VirtualTextureFeedbackRing;
using Core::Rendering::VirtualTextureRequestSet;
using Core::Rendering::VirtualTextureTileKey;
using Core::Rendering::VirtualTextureTileRequest;
namespace Feedback = Core::Rendering::VirtualTextureFeedback;

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "VirtualTextureRequestSetTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

VirtualTextureTileKey MakeKey(uint32_t texture, uint32_t mip, uint32_t x, uint32_t y)
{
    VirtualTextureTileKey key;
    key.TextureIndex = texture;
    key.Mip = mip;
    key.X = x;
    key.Y = y;
    return key;
}

// ヘッダの件数と要求の語から、GPU が書いたバッファの中身を作る（capacity 語を超える分は切る）
VariableArray<uint32_t> MakeBuffer(uint32_t capacity, uint32_t headerCount, const VariableArray<uint32_t>& words)
{
    VariableArray<uint32_t> buffer(Feedback::HeaderWords + capacity, 0u);
    buffer[0] = headerCount;
    for (size_t i = 0; i < words.size() && i < capacity; ++i)
    {
        buffer[Feedback::HeaderWords + i] = words[i];
    }
    return buffer;
}

void TestPackUnpack()
{
    // 範囲の端まで往復で戻る
    const VirtualTextureTileKey keys[] = {
        MakeKey(0, 0, 0, 0),
        MakeKey(1, 2, 3, 4),
        MakeKey(Feedback::MaxTextureIndex, Feedback::MaxMip, Feedback::MaxTileCoord, Feedback::MaxTileCoord),
        MakeKey(Feedback::MaxTextureIndex, 0, Feedback::MaxTileCoord, 0),
        MakeKey(7, Feedback::MaxMip, 0, Feedback::MaxTileCoord),
    };
    for (const VirtualTextureTileKey& key : keys)
    {
        const uint32_t word = Feedback::Pack(key);
        VirtualTextureTileKey decoded;
        Expect(word != 0, "収まる印は 0（書かれていない語）にならない");
        Expect(Feedback::Unpack(word, decoded), "詰めた語は復号できる");
        Expect(decoded == key, "詰めて戻した印が元と一致する");
    }

    // 番号 0・ミップ 0・(0,0) も 0 にならない（テクスチャの欄が番号 + 1 のため）
    Expect(Feedback::Pack(MakeKey(0, 0, 0, 0)) == (1u << 20), "番号 0 のタイル(0,0)はテクスチャの欄が 1");

    // 収まらない印は 0 を返し、足しても無視される
    Expect(!Feedback::CanPack(MakeKey(Feedback::MaxTextureIndex + 1, 0, 0, 0)), "テクスチャの番号が大きすぎる");
    Expect(!Feedback::CanPack(MakeKey(0, Feedback::MaxMip + 1, 0, 0)), "ミップが大きすぎる");
    Expect(!Feedback::CanPack(MakeKey(0, 0, Feedback::MaxTileCoord + 1, 0)), "x が大きすぎる");
    Expect(!Feedback::CanPack(MakeKey(0, 0, 0, Feedback::MaxTileCoord + 1)), "y が大きすぎる");
    Expect(Feedback::Pack(MakeKey(0, 0, 256, 0)) == 0, "収まらない印は 0 を返す");
    VirtualTextureRequestSet set;
    Expect(!set.Add(MakeKey(0, 0, 256, 0), 1), "収まらない印は集合へ足さない");
    Expect(set.IsEmpty(), "収まらない印だけでは集合は空のまま");

    // 書かれていない語は復号しない
    VirtualTextureTileKey ignored;
    Expect(!Feedback::Unpack(0, ignored), "0 は書かれていない語");
    Expect(!Feedback::Unpack(0x000FFFFFu, ignored), "テクスチャの欄が 0 の語は書かれていない語");

    Expect(Feedback::GetBufferBytes(Feedback::DefaultCapacity) == (4u + 65536u) * 4u, "既定のバッファの大きさ");
}

void TestDecodeAndDeduplicate()
{
    const uint32_t capacity = 16;
    VariableArray<uint32_t> words;
    words.push_back(Feedback::Pack(MakeKey(0, 1, 2, 3)));
    words.push_back(Feedback::Pack(MakeKey(0, 1, 2, 3))); // 同じタイルの重複
    words.push_back(Feedback::Pack(MakeKey(0, 1, 2, 3))); // もう 1 件
    words.push_back(Feedback::Pack(MakeKey(0, 0, 2, 3))); // ミップだけ違う別のタイル
    words.push_back(Feedback::Pack(MakeKey(5, 1, 2, 3))); // テクスチャだけ違う別のタイル
    words.push_back(0u);                                  // 書かれていない語（競合で取りこぼした枠）
    words.push_back(Feedback::Pack(MakeKey(5, 4, 9, 8)));

    const VariableArray<uint32_t> buffer = MakeBuffer(capacity, static_cast<uint32_t>(words.size()), words);
    VirtualTextureRequestSet set;
    const VirtualTextureFeedbackDecodeResult result = set.AddFeedbackBuffer(buffer.data(), capacity, 42);

    Expect(result.Attempted == 7 && result.Stored == 7, "ヘッダの件数がそのまま入っていた件数");
    Expect(result.Overflow == 0, "溢れていない");
    Expect(result.Decoded == 6, "復号できたのは書かれていない語を除く 6 件");
    Expect(result.Invalid == 1, "書かれていない語が 1 件");
    Expect(result.Duplicates == 2, "同じタイルの 2 件目以降が重複");
    Expect(set.GetRequestCount() == 4, "同じタイルは 1 つにまとまって 4 タイル");
    Expect(set.GetTextureCount() == 2, "テクスチャは 2 枚");
    Expect(set.GetOverflowCount() == 0, "溢れた件数は 0");

    uint64_t frame = 0;
    Expect(set.Find(MakeKey(0, 1, 2, 3), frame) && frame == 42, "要求したフレームが残る");
    Expect(set.Find(MakeKey(5, 4, 9, 8), frame), "別のテクスチャのタイルも見つかる");
    Expect(!set.Find(MakeKey(5, 0, 0, 0), frame), "要求していないタイルは見つからない");
    Expect(!set.Find(MakeKey(9, 0, 0, 0), frame), "要求の無いテクスチャは見つからない");

    // テクスチャごとに分かれて取り出せる
    const VariableArray<VirtualTextureTileRequest> texture0 = set.GetRequests(0);
    const VariableArray<VirtualTextureTileRequest> texture5 = set.GetRequests(5);
    Expect(texture0.size() == 2, "テクスチャ 0 は 2 タイル");
    Expect(texture5.size() == 2, "テクスチャ 5 は 2 タイル");
    Expect(set.GetRequests(3).empty(), "要求の無いテクスチャは空");
    bool foundDecoded = false;
    for (const VirtualTextureTileRequest& request : texture5)
    {
        if (request.Mip == 4 && request.X == 9 && request.Y == 8 && request.LastRequestedFrame == 42)
        {
            foundDecoded = true;
        }
    }
    Expect(foundDecoded, "取り出した要求にミップ・x・y・フレームが入っている");
    Expect(set.GetTextureIndices().size() == 2, "テクスチャの番号の一覧は 2 枚");
}

void TestLastRequestedFrame()
{
    const uint32_t capacity = 8;
    VariableArray<uint32_t> words;
    words.push_back(Feedback::Pack(MakeKey(2, 0, 1, 1)));
    const VariableArray<uint32_t> buffer = MakeBuffer(capacity, 1, words);

    VirtualTextureRequestSet set;
    set.AddFeedbackBuffer(buffer.data(), capacity, 10);
    set.AddFeedbackBuffer(buffer.data(), capacity, 12);
    // 古いフレームのバッファが後から取り込まれても、巻き戻さない
    const VirtualTextureFeedbackDecodeResult late = set.AddFeedbackBuffer(buffer.data(), capacity, 11);

    uint64_t frame = 0;
    Expect(set.Find(MakeKey(2, 0, 1, 1), frame) && frame == 12, "最後に要求したフレームは大きい方");
    Expect(set.GetRequestCount() == 1, "同じタイルは 1 つのまま");
    Expect(late.Duplicates == 1 && late.Decoded == 1, "既にあるタイルは重複として数える");
}

void TestOverflowCount()
{
    const uint32_t capacity = 4;
    VariableArray<uint32_t> words;
    for (uint32_t i = 0; i < 4; ++i)
    {
        words.push_back(Feedback::Pack(MakeKey(1, 0, i, 0)));
    }

    // 書こうとした件数が 10 件で、入るのは 4 件。残りの 6 件が溢れ
    VariableArray<uint32_t> buffer = MakeBuffer(capacity, 10, words);
    VirtualTextureRequestSet set;
    const VirtualTextureFeedbackDecodeResult result = set.AddFeedbackBuffer(buffer.data(), capacity, 1);
    Expect(result.Attempted == 10, "ヘッダの件数は溢れた分を含む");
    Expect(result.Stored == 4, "読むのは capacity 件まで");
    Expect(result.Overflow == 6, "溢れた件数 = 件数 - capacity");
    Expect(result.Decoded == 4 && set.GetRequestCount() == 4, "入っていた 4 件だけ復号する");
    Expect(set.GetOverflowCount() == 6, "集合が溢れた件数を持つ");
    Expect(!set.IsEmpty(), "溢れた件数があれば空ではない");

    // ちょうど capacity 件は溢れていない
    buffer[0] = capacity;
    VirtualTextureRequestSet exact;
    const VirtualTextureFeedbackDecodeResult exactResult = exact.AddFeedbackBuffer(buffer.data(), capacity, 1);
    Expect(exactResult.Overflow == 0 && exact.GetOverflowCount() == 0, "capacity 件ちょうどは溢れない");

    // 件数が非常に大きくても、読む範囲は capacity で止まり、溢れは 64bit で数える
    buffer[0] = 0xFFFFFFF0u;
    VirtualTextureRequestSet huge;
    const VirtualTextureFeedbackDecodeResult hugeResult = huge.AddFeedbackBuffer(buffer.data(), capacity, 1);
    Expect(hugeResult.Stored == capacity, "巨大な件数でも読むのは capacity 件まで");
    Expect(hugeResult.Overflow == 0xFFFFFFF0ull - capacity, "巨大な件数の溢れを正しく数える");

    // 溢れた件数は取り込みのたびに足し上がる
    huge.AddFeedbackBuffer(buffer.data(), capacity, 2);
    Expect(huge.GetOverflowCount() == 2ull * (0xFFFFFFF0ull - capacity), "溢れた件数は取り込みごとに足される");

    // 0 件のバッファ・null は何も足さない
    VirtualTextureRequestSet empty;
    buffer[0] = 0;
    empty.AddFeedbackBuffer(buffer.data(), capacity, 1);
    empty.AddFeedbackBuffer(nullptr, capacity, 1);
    Expect(empty.IsEmpty(), "0 件のバッファと null では何も足さない");
}

void TestMerge()
{
    VirtualTextureRequestSet a;
    a.Add(MakeKey(1, 0, 1, 1), 5);
    a.Add(MakeKey(1, 0, 2, 2), 5);

    VirtualTextureRequestSet b;
    b.Add(MakeKey(1, 0, 1, 1), 9);  // a と重なるタイルで、b の方が新しい
    b.Add(MakeKey(3, 2, 0, 0), 9);

    a.Merge(b);
    uint64_t frame = 0;
    Expect(a.GetRequestCount() == 3, "重なるタイルは 1 つにまとまる");
    Expect(a.Find(MakeKey(1, 0, 1, 1), frame) && frame == 9, "重なるタイルは新しいフレームになる");
    Expect(a.Find(MakeKey(3, 2, 0, 0), frame) && frame == 9, "b だけのタイルが入る");
    Expect(a.GetTextureCount() == 2, "テクスチャは 2 枚");

    // 溢れた件数も足し合わせる
    const uint32_t capacity = 2;
    VariableArray<uint32_t> buffer = MakeBuffer(capacity, 5, VariableArray<uint32_t>());
    VirtualTextureRequestSet overflowed;
    overflowed.AddFeedbackBuffer(buffer.data(), capacity, 1);
    a.Merge(overflowed);
    a.Merge(overflowed);
    Expect(a.GetOverflowCount() == 6, "取り込んだ集合の溢れた件数が足される");

    a.Clear();
    Expect(a.IsEmpty() && a.GetRequestCount() == 0 && a.GetOverflowCount() == 0 && a.GetTextureCount() == 0,
           "Clear で全部消える");
}

// ---- リング: 偽デバイス ----

// 写像できる偽バッファ（常に同じメモリを返す）
class FakeBuffer final : public RHI::IBuffer
{
public:
    FakeBuffer(uint64_t size, RHI::ResourceUsage usage) : Data(static_cast<size_t>(size), 0xCDu), Usage(usage) {}

    uint64_t GetSize() const override { return Data.size(); }
    void* Map(uint64_t, uint64_t) override { return Data.data(); }
    void Unmap() override {}
    void Update(const void*, uint64_t, uint64_t) override {}
    RHI::ResourceUsage GetUsage() const override { return Usage; }

    VariableArray<uint8_t> Data;
    RHI::ResourceUsage Usage;
};

class FakeDevice final : public RHI::IDevice
{
public:
    RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
    {
        ++CreateBufferCalls;
        if (bFailBufferCreation)
        {
            return nullptr;
        }
        CreatedDescs.push_back(desc);
        return MakeShared<FakeBuffer>(desc.Size, desc.Usage);
    }

    RHI::TexturePtr CreateTexture(const RHI::TextureDesc&) override { return {}; }
    RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc&) override { return {}; }
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
    // リングは GPU の完了を待たない。呼ばれたら数える。
    void WaitIdle() override { ++WaitIdleCalls; }
    RHI::API GetAPI() const override { return RHI::API::None; }
    const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
    Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4& projection, bool) const override
    {
        return projection;
    }

    RHI::DeviceCapabilities Capabilities;
    VariableArray<RHI::BufferDesc> CreatedDescs;
    int CreateBufferCalls = 0;
    int WaitIdleCalls = 0;
    bool bFailBufferCreation = false;
};

constexpr uint32_t RingCapacity = 8;

VirtualTextureFeedbackRing::Config MakeRingConfig()
{
    VirtualTextureFeedbackRing::Config config;
    config.Capacity = RingCapacity;
    return config;
}

// フレームのシェーダーが書いた結果を、獲得したバッファへ置く（ヘッダの件数 + 要求の語）
void WriteFrame(const RHI::BufferPtr& buffer, uint32_t headerCount, const VariableArray<VirtualTextureTileKey>& tiles)
{
    uint32_t* words = static_cast<uint32_t*>(buffer->Map(0, 0));
    words[0] = headerCount;
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        words[Feedback::HeaderWords + i] = Feedback::Pack(tiles[i]);
    }
}

VariableArray<VirtualTextureTileKey> Tiles(const VirtualTextureTileKey& a)
{
    VariableArray<VirtualTextureTileKey> tiles;
    tiles.push_back(a);
    return tiles;
}

void TestRingDisabledByDefault()
{
    auto device = MakeShared<FakeDevice>();
    VirtualTextureFeedbackRing ring(device, MakeRingConfig());

    Expect(!ring.GetStats().bEnabled, "初期状態は無効");
    ring.BeginFrame(0);
    Expect(ring.GetCurrentBuffer() == nullptr, "無効の間はバッファを持たない");
    ring.CommitFrame(1);
    ring.BeginFrame(1);
    VirtualTextureRequestSet taken;
    Expect(!ring.TakeRequests(taken), "無効の間は要求が溜まらない");
    Expect(device->CreateBufferCalls == 0, "無効の間はバッファを確保しない");
    Expect(ring.GetStats().FramesSkipped == 0 && ring.GetStats().Frames == 2, "無効のフレームは取れなかったフレームに数えない");
}

void TestRingEnableFailureLeavesNoBuffers()
{
    auto device = MakeShared<FakeDevice>();
    device->bFailBufferCreation = true;
    VirtualTextureFeedbackRing ring(device, MakeRingConfig());
    Expect(!ring.SetEnabled(true), "バッファを作れなければ有効にならない");
    Expect(!ring.GetStats().bEnabled, "有効にできなかったので無効のまま");
    ring.BeginFrame(0);
    Expect(ring.GetCurrentBuffer() == nullptr, "作れなかったときはバッファが無い");

    device->bFailBufferCreation = false;
    Expect(ring.SetEnabled(true), "作れるようになれば有効にできる");
    Expect(device->CreatedDescs.size() == VirtualTextureFeedbackRing::SlotCount, "バッファは 3 つ作る");
    const RHI::BufferDesc& desc = device->CreatedDescs[0];
    Expect(desc.Size == Feedback::GetBufferBytes(RingCapacity), "バッファの大きさはヘッダ + capacity 語");
    Expect(desc.CPUAccessible, "バッファは CPU から読める");
    Expect((desc.Usage & RHI::ResourceUsage::StorageBuffer) == RHI::ResourceUsage::StorageBuffer,
           "バッファは storage buffer");
}

void TestRingReadsBackWithoutWaiting()
{
    auto device = MakeShared<FakeDevice>();
    VirtualTextureFeedbackRing ring(device, MakeRingConfig());
    Expect(ring.SetEnabled(true), "有効にできる");

    // フレーム 1: 獲得したバッファは 0 で埋まっている（偽バッファは 0xCD で作られる）
    ring.BeginFrame(0);
    RHI::BufferPtr frame1 = ring.GetCurrentBuffer();
    Expect(frame1 != nullptr, "有効ならバッファを獲得する");
    const uint32_t* frame1Words = static_cast<const uint32_t*>(frame1->Map(0, 0));
    Expect(frame1Words[0] == 0 && frame1Words[Feedback::HeaderWords + RingCapacity - 1] == 0,
           "獲得したバッファは前の内容が消えている");
    WriteFrame(frame1, 1, Tiles(MakeKey(1, 0, 4, 5)));
    ring.CommitFrame(1);

    // フレーム 2・3 も serial 2・3 で提出する。完了は 0 のまま（GPU が終わっていない）。
    ring.BeginFrame(0);
    RHI::BufferPtr frame2 = ring.GetCurrentBuffer();
    Expect(frame2 != nullptr && frame2 != frame1, "次のフレームは別のスロット");
    WriteFrame(frame2, 1, Tiles(MakeKey(1, 0, 4, 5)));
    ring.CommitFrame(2);

    ring.BeginFrame(0);
    RHI::BufferPtr frame3 = ring.GetCurrentBuffer();
    Expect(frame3 != nullptr && frame3 != frame1 && frame3 != frame2, "3つめも別のスロット");
    WriteFrame(frame3, 2, [] {
        VariableArray<VirtualTextureTileKey> tiles;
        tiles.push_back(MakeKey(2, 1, 0, 0));
        tiles.push_back(MakeKey(2, 1, 0, 0));
        return tiles;
    }());
    ring.CommitFrame(3);

    // フレーム 4: 3つとも GPU が使っている。待たずに、このフレームはバッファ無しで進む。
    ring.BeginFrame(0);
    Expect(ring.GetCurrentBuffer() == nullptr, "空きスロットが無いフレームはバッファが無い");
    VirtualTextureRequestSet none;
    Expect(!ring.TakeRequests(none), "完了していない間は何も読み戻さない");
    Expect(ring.GetStats().FramesSkipped == 1, "取れなかったフレームを数える");
    ring.CommitFrame(4); // バッファの無いフレームの提出は何も起こさない
    Expect(ring.GetStats().BusySlots == 3, "3つとも読み戻しを待っている");

    // フレーム 5: serial 1 まで完了。フレーム 1 の分だけ読み戻され、空いたスロットをこのフレームが使う。
    ring.BeginFrame(1);
    RHI::BufferPtr frame5 = ring.GetCurrentBuffer();
    Expect(frame5 == frame1, "読み戻したスロットが空いて、次のフレームが使う");
    VirtualTextureRequestSet first;
    Expect(ring.TakeRequests(first), "完了したフレームの要求が渡る");
    uint64_t frame = 0;
    Expect(first.GetRequestCount() == 1 && first.Find(MakeKey(1, 0, 4, 5), frame), "フレーム 1 のタイルが入る");
    Expect(frame == 1, "最後に要求したフレームは、書いたフレームの番号（1 つめの BeginFrame = 1）");
    Expect(!first.Find(MakeKey(2, 1, 0, 0), frame), "まだ完了していないフレーム 3 の分は入らない");
    VirtualTextureRequestSet again;
    Expect(!ring.TakeRequests(again), "渡した後は空に戻る");
    WriteFrame(frame5, 0, VariableArray<VirtualTextureTileKey>());
    ring.CommitFrame(5);

    // フレーム 6: serial 3 まで完了。フレーム 2・3 が一度に読み戻される。同じタイルの最後のフレームは新しい方（2）。
    ring.BeginFrame(3);
    VirtualTextureRequestSet second;
    Expect(ring.TakeRequests(second), "完了した 2 フレーム分が渡る");
    Expect(second.GetRequestCount() == 2, "重複を除いて 2 タイル（(1,0,4,5) と (2,1,0,0)）");
    Expect(second.Find(MakeKey(1, 0, 4, 5), frame) && frame == 2, "同じタイルを要求したフレームの大きい方（2）が残る");
    Expect(second.Find(MakeKey(2, 1, 0, 0), frame) && frame == 3, "フレーム 3 のタイル");

    Expect(device->WaitIdleCalls == 0, "読み戻しの間に GPU の完了待ちを呼ばない");
    const VirtualTextureFeedbackRing::Stats stats = ring.GetStats();
    Expect(stats.BuffersRead == 3, "読み戻したバッファは 3 つ");
    Expect(stats.FramesRecorded == 5 && stats.Frames == 6, "獲得できたフレームは 5、数えたフレームは 6");
}

void TestRingOverflowAndAbort()
{
    auto device = MakeShared<FakeDevice>();
    VirtualTextureFeedbackRing ring(device, MakeRingConfig());
    Expect(ring.SetEnabled(true), "有効にできる");

    // 溢れたバッファ: 件数が capacity を超えた分が、集合へ渡る
    ring.BeginFrame(0);
    RHI::BufferPtr overflow = ring.GetCurrentBuffer();
    VariableArray<VirtualTextureTileKey> tiles;
    for (uint32_t i = 0; i < RingCapacity; ++i)
    {
        tiles.push_back(MakeKey(0, 0, i, 0));
    }
    WriteFrame(overflow, RingCapacity + 5, tiles);
    ring.CommitFrame(1);

    // 中止したフレーム: GPU は書いていないので、内容は読まれず、スロットは空きへ戻る
    ring.BeginFrame(0);
    RHI::BufferPtr aborted = ring.GetCurrentBuffer();
    WriteFrame(aborted, 1, Tiles(MakeKey(9, 0, 9, 9)));
    ring.AbortFrame();
    Expect(ring.GetStats().BusySlots == 1, "中止したスロットは空きへ戻る（残りは提出済みの 1 つ）");

    // 提出も中止もされずに次のフレームが始まったときも、中止として扱う
    ring.BeginFrame(0);
    RHI::BufferPtr dropped = ring.GetCurrentBuffer();
    WriteFrame(dropped, 1, Tiles(MakeKey(9, 0, 8, 8)));
    ring.BeginFrame(1);
    Expect(ring.GetStats().BusySlots == 1, "記録中のまま次が始まると前のスロットは空き、新しいフレームが 1 つ使う");

    VirtualTextureRequestSet set;
    Expect(ring.TakeRequests(set), "完了した提出の要求が渡る");
    uint64_t frame = 0;
    Expect(set.GetRequestCount() == RingCapacity, "溢れたバッファからも capacity 件は読む");
    Expect(set.GetOverflowCount() == 5, "溢れた件数が集合へ渡る");
    Expect(!set.Find(MakeKey(9, 0, 9, 9), frame) && !set.Find(MakeKey(9, 0, 8, 8), frame),
           "提出しなかったフレームの要求は読まれない");
    Expect(ring.GetStats().BuffersOverflowed == 1 && ring.GetStats().OverflowTotal == 5, "溢れの統計");

    // 提出済みで空のバッファを読み戻したあと、Clear で初期状態へ戻る
    ring.CommitFrame(2);
    ring.BeginFrame(2);
    ring.Clear();
    Expect(!ring.GetStats().bEnabled && ring.GetCurrentBuffer() == nullptr, "Clear で無効に戻りバッファを手放す");
    VirtualTextureRequestSet afterClear;
    Expect(!ring.TakeRequests(afterClear), "Clear で溜まった集計も消える");
}

void TestRingReadsOnlyAfterMinAge()
{
    auto device = MakeShared<FakeDevice>();
    VirtualTextureFeedbackRing ring(device, MakeRingConfig());
    Expect(ring.SetEnabled(true), "有効にできる");

    // フレーム 1 を serial 1 で提出する。飛行数 1 のエンジンのように、次のフレームの開始時には完了している。
    ring.BeginFrame(0);
    WriteFrame(ring.GetCurrentBuffer(), 1, Tiles(MakeKey(3, 0, 1, 2)));
    ring.CommitFrame(1);

    // フレーム 2: serial 1 は完了済みでも、1 フレームしか経っていないので渡さない
    ring.BeginFrame(1);
    VirtualTextureRequestSet early;
    Expect(!ring.TakeRequests(early), "完了が早くても、翌フレームには読み戻さない");
    WriteFrame(ring.GetCurrentBuffer(), 0, VariableArray<VirtualTextureTileKey>());
    ring.CommitFrame(2);
    Expect(ring.GetStats().BuffersRead == 0, "2 フレーム経つまで読み戻したバッファは無い");

    // フレーム 3: 書いたフレーム 1 から 2 フレーム経った。完了済みなので読み戻す。
    ring.BeginFrame(2);
    VirtualTextureRequestSet ready;
    uint64_t frame = 0;
    Expect(ring.TakeRequests(ready) && ready.Find(MakeKey(3, 0, 1, 2), frame) && frame == 1,
           "2 フレーム経って完了していれば、フレーム 1 の要求が渡る");
    Expect(ring.GetStats().BuffersRead == 1, "フレーム 1 だけ読み戻した（フレーム 2 は完了していても 1 フレームしか経っていない）");
    Expect(device->WaitIdleCalls == 0, "待たずに読み戻す");

    // 2 フレーム経っていても、完了していなければ待たずに残す
    ring.CommitFrame(3);
    ring.BeginFrame(2);
    ring.CommitFrame(4);
    ring.BeginFrame(2);
    VirtualTextureRequestSet pending;
    Expect(!ring.TakeRequests(pending), "serial が未完了なら 2 フレーム経っても読み戻さない");
}

int RunTest()
{
    TestPackUnpack();
    TestDecodeAndDeduplicate();
    TestLastRequestedFrame();
    TestOverflowCount();
    TestMerge();
    TestRingDisabledByDefault();
    TestRingEnableFailureLeavesNoBuffers();
    TestRingReadsBackWithoutWaiting();
    TestRingOverflowAndAbort();
    TestRingReadsOnlyAfterMinAge();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VirtualTextureRequestSetTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
