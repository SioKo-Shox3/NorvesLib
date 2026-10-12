#include "Rendering/FrameResourceOwner.h"
#include "Rendering/FrameUploadArena.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/Containers.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    // GPU を使わないページ。CPU のメモリを持ち、Map は作成時の 1 回だけ、Update は通らないことを数える
    class FakeUploadBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeUploadBuffer(uint64_t size) : m_Bytes(static_cast<size_t>(size), 0) {}

        uint64_t GetSize() const override { return m_Bytes.size(); }
        void* Map(uint64_t offset, uint64_t) override
        {
            ++m_MapCount;
            return m_Bytes.data() + offset;
        }
        void Unmap() override { ++m_UnmapCount; }
        void Update(const void*, uint64_t, uint64_t) override { ++m_UpdateCount; }
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::VertexBuffer |
                   RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer;
        }

        uint32_t GetMapCount() const { return m_MapCount; }
        uint32_t GetUnmapCount() const { return m_UnmapCount; }
        uint32_t GetUpdateCount() const { return m_UpdateCount; }

    private:
        Container::VariableArray<uint8_t> m_Bytes;
        uint32_t m_MapCount = 0;
        uint32_t m_UnmapCount = 0;
        uint32_t m_UpdateCount = 0;
    };

    // ページの口の代わり。作ったページを覚えておき、Map・Update の回数を後で確かめる
    class FakePageSource final : public IFrameUploadPageSource
    {
    public:
        explicit FakePageSource(const FrameUploadLimits& limits) : m_Limits(limits) {}

        bool CreatePage(uint64_t sizeBytes, FrameUploadPage& outPage) override
        {
            auto buffer = Container::MakeShared<FakeUploadBuffer>(sizeBytes);
            outPage.Buffer = buffer;
            outPage.CPUAddress = static_cast<uint8_t*>(buffer->Map(0, 0));
            outPage.Size = sizeBytes;
            m_Created.push_back(buffer);
            return true;
        }

        FrameUploadLimits GetLimits() const override { return m_Limits; }

        size_t GetCreatedCount() const { return m_Created.size(); }
        const FakeUploadBuffer& GetCreated(size_t index) const { return *m_Created[index]; }

    private:
        FrameUploadLimits m_Limits;
        Container::VariableArray<Container::TSharedPtr<FakeUploadBuffer>> m_Created;
    };

    // 4 GiB を超える大きさを名乗るだけのページ。中身は持たず、借りた切れ端へは書かない検査でだけ使う
    class VirtualUploadBuffer final : public RHI::IBuffer
    {
    public:
        explicit VirtualUploadBuffer(uint64_t size) : m_Size(size) {}

        uint64_t GetSize() const override { return m_Size; }
        void* Map(uint64_t, uint64_t) override { return m_Bytes; }
        void Unmap() override {}
        void Update(const void*, uint64_t, uint64_t) override {}
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::VertexBuffer |
                   RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer;
        }

    private:
        uint64_t m_Size = 0;
        uint8_t m_Bytes[16] = {};
    };

    class VirtualPageSource final : public IFrameUploadPageSource
    {
    public:
        explicit VirtualPageSource(const FrameUploadLimits& limits) : m_Limits(limits) {}

        bool CreatePage(uint64_t sizeBytes, FrameUploadPage& outPage) override
        {
            auto buffer = Container::MakeShared<VirtualUploadBuffer>(sizeBytes);
            outPage.Buffer = buffer;
            outPage.CPUAddress = static_cast<uint8_t*>(buffer->Map(0, 0));
            outPage.Size = sizeBytes;
            return true;
        }

        FrameUploadLimits GetLimits() const override { return m_Limits; }

    private:
        FrameUploadLimits m_Limits;
    };

    FrameUploadLimits MakeTestLimits()
    {
        // 定数とストレージの整列を頂点・インデックスと違う値にして、使い道ごとに整列が効くことを見る
        FrameUploadLimits limits;
        limits.ConstantsAlignment = 256;
        limits.StorageAlignment = 64;
        limits.VertexAlignment = 16;
        limits.IndexAlignment = 16;
        limits.MaxConstantsSize = 4096;
        limits.MaxStorageSize = 8192;
        return limits;
    }

    struct TestConstants
    {
        float Values[4];
        uint32_t Id;
    };

    bool RangesOverlap(const FrameUploadAllocation& a, const FrameUploadAllocation& b)
    {
        if (a.Buffer != b.Buffer)
        {
            return false;
        }
        return a.Offset < b.Offset + b.Size && b.Offset < a.Offset + a.Size;
    }

    void TestFrameResourceUploadAlignment()
    {
        FakePageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 64 * 1024);

        ViewRenderContext context;
        context.FrameResources = owners.BeginFrame(0, 0);
        FrameResourceOwner& owner = *context.FrameResources;

        // 端数の大きさを挟んで、次の切れ端の開始がその使い道の整列にそろうこと
        const FrameUploadAllocation vertex = owner.AllocateUpload(12, FrameUploadUsage::Vertex);
        const FrameUploadAllocation constants = owner.AllocateUpload(4, FrameUploadUsage::Constants);
        const FrameUploadAllocation index = owner.AllocateUpload(6, FrameUploadUsage::Index);
        const FrameUploadAllocation storage = owner.AllocateUpload(20, FrameUploadUsage::Storage);
        const FrameUploadAllocation constants2 = owner.AllocateUpload(1, FrameUploadUsage::Constants);
        assert(vertex.IsValid() && constants.IsValid() && index.IsValid() && storage.IsValid() && constants2.IsValid());
        assert(vertex.Offset % 16 == 0);
        assert(constants.Offset % 256 == 0 && constants.Offset == 256);
        assert(index.Offset % 16 == 0 && index.Offset >= constants.Offset + constants.Size);
        assert(storage.Offset % 64 == 0 && storage.Offset >= index.Offset + index.Size);
        assert(constants2.Offset % 256 == 0 && constants2.Offset >= storage.Offset + storage.Size);
        // 返す大きさは求めた大きさのまま（束縛の範囲に渡す）
        assert(vertex.Size == 12 && constants.Size == 4 && storage.Size == 20);
        assert(constants.GetOffset32() == 256 && constants.GetSize32() == 4);
        // CPUAddress はページの先頭 + オフセット
        assert(static_cast<uint8_t*>(constants.CPUAddress) - static_cast<uint8_t*>(vertex.CPUAddress) ==
               static_cast<ptrdiff_t>(constants.Offset - vertex.Offset));

        // 記述子の範囲の上限を超える定数・ストレージは借りられない（失敗を数え、前の切れ端は動かない）
        const FrameUploadAllocation tooLargeConstants = owner.AllocateUpload(4097, FrameUploadUsage::Constants);
        const FrameUploadAllocation tooLargeStorage = owner.AllocateUpload(8193, FrameUploadUsage::Storage);
        assert(!tooLargeConstants.IsValid());
        assert(!tooLargeStorage.IsValid());
        assert(owner.GetUploadArena().GetFrameStats().Failures == 2);
        // 上限ちょうどは借りられる
        const FrameUploadAllocation maxConstants = owner.AllocateUpload(4096, FrameUploadUsage::Constants);
        assert(maxConstants.IsValid() && maxConstants.Offset % 256 == 0);

        const FrameUploadStats stats = owner.GetUploadArena().GetFrameStats();
        assert(stats.Allocations == 6);
        assert(stats.Bytes == 12 + 4 + 6 + 20 + 1 + 4096);
        assert(stats.PagesAdded == 1);
        owners.CommitSubmitted(1);
        owners.Reset();
        owners.ReleaseUploadPages();
    }

    void TestFrameResourceUploadMultipleBorrowsKeepContents()
    {
        FakePageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 64 * 1024);
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);

        // 同じフレームで何回借りても、前の切れ端と重ならず、書いた中身は後の借りで上書きされない
        Container::VariableArray<FrameUploadAllocation> allocations;
        for (uint32_t index = 0; index < 32; ++index)
        {
            TestConstants constants{};
            constants.Values[0] = static_cast<float>(index);
            constants.Id = index;
            allocations.push_back(owner->AllocateConstants(constants));
            assert(allocations.back().IsValid());
            assert(allocations.back().Usage == FrameUploadUsage::Constants);
            assert(allocations.back().Size == sizeof(TestConstants));
        }
        const uint32_t vertices[3] = {7, 8, 9};
        const FrameUploadAllocation vertexAllocation =
            owner->AllocateUploadData(vertices, sizeof(vertices), FrameUploadUsage::Vertex);
        assert(vertexAllocation.IsValid());
        allocations.push_back(vertexAllocation);

        for (size_t a = 0; a < allocations.size(); ++a)
        {
            for (size_t b = a + 1; b < allocations.size(); ++b)
            {
                assert(!RangesOverlap(allocations[a], allocations[b]));
            }
        }
        for (uint32_t index = 0; index < 32; ++index)
        {
            TestConstants read{};
            std::memcpy(&read, allocations[index].CPUAddress, sizeof(read));
            assert(read.Id == index && read.Values[0] == static_cast<float>(index));
        }
        assert(std::memcmp(vertexAllocation.CPUAddress, vertices, sizeof(vertices)) == 0);

        // 書き込みは Buffer::Update を通さず、ページは作成時に 1 回だけマップしたまま
        assert(source.GetCreatedCount() == 1);
        assert(source.GetCreated(0).GetUpdateCount() == 0);
        assert(source.GetCreated(0).GetMapCount() == 1);
        assert(source.GetCreated(0).GetUnmapCount() == 0);

        // 借りた切れ端は記録中のフレームの世代を持つ
        assert(owner->ValidateBorrowed(allocations[0], "Constants0"));
        owners.CommitSubmitted(1);
        owners.Reset();
        owners.ReleaseUploadPages();
    }

    void TestFrameResourceUploadPageGrowthKeepsEarlierSlices()
    {
        FakePageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        // --frame-arena-initial-kb を小さくしたときと同じく、1 ページを小さくしてページを足す道を通す
        owners.ConfigureUpload(&source, 1024);
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);

        uint8_t first[600];
        std::memset(first, 0xA5, sizeof(first));
        const FrameUploadAllocation a = owner->AllocateUploadData(first, sizeof(first), FrameUploadUsage::Storage);
        assert(a.IsValid() && a.Offset == 0);
        const RHI::IBuffer* const firstPage = a.Buffer.get();

        // 今のページの残り（424 バイト）に入らないので、ページを足す。前の切れ端は動かず中身も残る
        uint8_t second[600];
        std::memset(second, 0x5A, sizeof(second));
        const FrameUploadAllocation b = owner->AllocateUploadData(second, sizeof(second), FrameUploadUsage::Storage);
        assert(b.IsValid());
        assert(b.Buffer.get() != firstPage);
        assert(b.Offset == 0);
        assert(a.Buffer.get() == firstPage && a.Offset == 0 && a.Size == 600);
        assert(std::memcmp(a.CPUAddress, first, sizeof(first)) == 0);
        assert(std::memcmp(b.CPUAddress, second, sizeof(second)) == 0);

        // ページより大きな切れ端は、その大きさのページを足して返す（記述子の範囲の上限は守る）
        const FrameUploadAllocation large = owner->AllocateUpload(5000, FrameUploadUsage::Storage);
        assert(large.IsValid() && large.Offset == 0);
        assert(large.Buffer->GetSize() >= 5000);

        // 大きなページの後で小さな切れ端を借りても、前のページへは戻らない（前へ進むだけ）
        const FrameUploadAllocation tinySlice = owner->AllocateUpload(16, FrameUploadUsage::Vertex);
        assert(tinySlice.IsValid());
        assert(tinySlice.Buffer.get() == large.Buffer.get() && tinySlice.Offset >= 5000);
        assert(std::memcmp(a.CPUAddress, first, sizeof(first)) == 0);
        assert(std::memcmp(b.CPUAddress, second, sizeof(second)) == 0);

        const FrameUploadStats stats = owner->GetUploadArena().GetFrameStats();
        assert(stats.PagesAdded == 3);
        assert(stats.Allocations == 4);
        assert(source.GetCreatedCount() == 3);

        owners.CommitSubmitted(1);
        assert(owners.GetLastFrameUploadStats().PagesAdded == 3);
        assert(owners.GetTotalUploadPagesAdded() == 3);
        owners.Reset();
        owners.ReleaseUploadPages();
    }

    void TestFrameResourceUploadDescriptorOffsetFitsUint32()
    {
        VirtualPageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 64 * 1024);
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);

        // 4 GiB を超える頂点の切れ端で、その大きさのページを足す
        const FrameUploadAllocation vertex = owner->AllocateUpload(4294967297ull, FrameUploadUsage::Vertex);
        assert(vertex.IsValid() && vertex.Offset == 0);
        assert(vertex.Buffer->GetSize() > FrameUploadMaxDescriptorValue);

        // 同じページの続きはオフセットが uint32 を超えるので、定数・ストレージは新しいページの先頭から返す
        // （GetOffset32 が切り詰めて先の頂点の範囲を束縛しない）
        const FrameUploadAllocation constants = owner->AllocateUpload(16, FrameUploadUsage::Constants);
        assert(constants.IsValid());
        assert(constants.Buffer != vertex.Buffer);
        assert(constants.Offset <= FrameUploadMaxDescriptorValue);
        assert(static_cast<uint64_t>(constants.GetOffset32()) == constants.Offset);
        assert(static_cast<uint64_t>(constants.GetSize32()) == constants.Size);
        const FrameUploadAllocation storage = owner->AllocateUpload(16, FrameUploadUsage::Storage);
        assert(storage.IsValid() && storage.Buffer == constants.Buffer);
        assert(static_cast<uint64_t>(storage.GetOffset32()) == storage.Offset);
        assert(!RangesOverlap(constants, storage));

        // 前へ進むだけなので、飛ばした大きなページへは戻らない
        const FrameUploadAllocation vertexAfter = owner->AllocateUpload(16, FrameUploadUsage::Vertex);
        assert(vertexAfter.IsValid() && vertexAfter.Buffer == constants.Buffer);
        assert(owner->GetUploadArena().GetFrameStats().PagesAdded == 2);

        owners.CommitSubmitted(1);
        owners.Reset();
        owners.ReleaseUploadPages();
    }

    void TestFrameResourceUploadReclaimReturnsSpace()
    {
        FakePageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 4096);

        // スロット 0 のフレームを提出する
        FrameResourceOwner* owner0 = owners.BeginFrame(0, 0);
        const FrameUploadAllocation slot0First = owner0->AllocateUpload(1000, FrameUploadUsage::Storage);
        const FrameUploadAllocation slot0Second = owner0->AllocateUpload(1000, FrameUploadUsage::Storage);
        assert(slot0First.IsValid() && slot0Second.IsValid());
        std::memset(slot0First.CPUAddress, 0x11, 1000);
        assert(owners.CommitSubmitted(1));

        // 提出が完了するまでは、別のスロットのフレームが借りてもスロット 0 の切れ端の範囲を使わない
        FrameResourceOwner* owner1 = owners.BeginFrame(1, 0);
        assert(owner1 != owner0);
        const FrameUploadAllocation slot1First = owner1->AllocateUpload(1000, FrameUploadUsage::Storage);
        assert(slot1First.IsValid());
        assert(!RangesOverlap(slot1First, slot0First) && !RangesOverlap(slot1First, slot0Second));
        std::memset(slot1First.CPUAddress, 0x22, 1000);
        const uint8_t* const slot0Bytes = static_cast<const uint8_t*>(slot0First.CPUAddress);
        assert(slot0Bytes[0] == 0x11 && slot0Bytes[999] == 0x11);
        assert(owners.CommitSubmitted(2));

        // スロット 0 の提出（1）が完了してから次に開始すると、スロット 0 の領域は先頭へ戻り、同じページを使い回す
        FrameResourceOwner* again = owners.BeginFrame(0, 1);
        assert(again == owner0);
        assert(owner0->GetReclaimCount() == 1);
        assert(owner0->GetUploadArena().GetFrameStats().Allocations == 0);
        const FrameUploadAllocation reused = owner0->AllocateUpload(1000, FrameUploadUsage::Storage);
        assert(reused.IsValid());
        assert(reused.Buffer == slot0First.Buffer && reused.Offset == slot0First.Offset);
        assert(owner0->GetUploadArena().GetFrameStats().PagesAdded == 0);
        // 前のフレームで借りた切れ端は世代が違うので拒む（開発ビルド）
#if NORVES_BUILD_DEVELOPMENT
        assert(!owner0->ValidateBorrowed(slot0First, "Slot0First"));
#endif
        assert(owner0->ValidateBorrowed(reused, "Reused"));
        // スロット 1 はまだ完了していないので回収されていない
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Submitted);
        assert(owners.GetOwner(1).GetUploadArena().GetFrameStats().Allocations == 1);
        owners.AbortFrame();

        // 中止したフレームの分は次の開始で戻る
        FrameResourceOwner* afterAbort = owners.BeginFrame(0, 2);
        assert(afterAbort == owner0);
        assert(owner0->GetUploadArena().GetFrameStats().Allocations == 0);
        const FrameUploadAllocation afterAbortSlice = owner0->AllocateUpload(8, FrameUploadUsage::Storage);
        assert(afterAbortSlice.Buffer == slot0First.Buffer && afterAbortSlice.Offset == 0);
        // スロット 1 の提出（2）も完了したので戻っている
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Idle);
        assert(owners.GetOwner(1).GetUploadArena().GetFrameStats().Allocations == 0);
        assert(owners.GetOwner(1).GetUploadArena().GetPageCount() == 1);
        owners.CommitSubmitted(3);

        // ページを作る口は 2 スロット分の 2 枚だけ（使い回しでは足さない）
        assert(source.GetCreatedCount() == 2);
        owners.Reset();
        owners.ReleaseUploadPages();
        assert(owners.GetOwner(0).GetUploadArena().GetPageCount() == 0);
    }

    void TestFrameResourceUploadRejectedOutsideRecording()
    {
        FakePageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 4096);

        // 開始の前・提出の後は借りられない
        assert(!owners.GetOwner(0).AllocateUpload(16, FrameUploadUsage::Constants).IsValid());
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);
        assert(owner->AllocateUpload(16, FrameUploadUsage::Constants).IsValid());
        owners.CommitSubmitted(1);
        assert(!owner->AllocateUpload(16, FrameUploadUsage::Constants).IsValid());
        // 大きさ 0 も借りられない
        owner = owners.BeginFrame(0, 1);
        assert(!owner->AllocateUpload(0, FrameUploadUsage::Vertex).IsValid());
        owners.AbortFrame();

        // ページの口を設定していなければ借りられない
        FrameResourceOwners unconfigured;
        FrameResourceOwner* unconfiguredOwner = unconfigured.BeginFrame(0, 0);
        assert(!unconfigured.IsUploadConfigured());
        assert(!unconfiguredOwner->AllocateUpload(16, FrameUploadUsage::Constants).IsValid());
        unconfigured.AbortFrame();
        owners.Reset();
        owners.ReleaseUploadPages();
    }

    void TestFrameResourceUploadLimitsFromCapabilities()
    {
        // デバイスの能力から整列と上限を決める。不明（0）は仕様の最も厳しい値、整列の下限は 16
        RHI::DeviceCapabilities capabilities;
        const FrameUploadLimits unknown = FrameUploadLimits::FromCapabilities(capabilities);
        assert(unknown.ConstantsAlignment == 256 && unknown.StorageAlignment == 256);
        assert(unknown.MaxConstantsSize == 16384);

        capabilities.MinUniformBufferOffsetAlignment = 64;
        capabilities.MinStorageBufferOffsetAlignment = 4;
        capabilities.MaxUniformBufferRange = 65536;
        capabilities.MaxStorageBufferRange = 1ull << 30;
        const FrameUploadLimits known = FrameUploadLimits::FromCapabilities(capabilities);
        assert(known.ConstantsAlignment == 64);
        assert(known.StorageAlignment == 16);
        assert(known.MaxConstantsSize == 65536);
        assert(known.MaxStorageSize == (1ull << 30));
        assert(known.GetMaxSize(FrameUploadUsage::Vertex) == 0);
    }
} // namespace

int main()
{
    std::cout << "FrameResourceUploadArenaTest 開始\n";

    TestFrameResourceUploadAlignment();
    TestFrameResourceUploadMultipleBorrowsKeepContents();
    TestFrameResourceUploadPageGrowthKeepsEarlierSlices();
    TestFrameResourceUploadDescriptorOffsetFitsUint32();
    TestFrameResourceUploadReclaimReturnsSpace();
    TestFrameResourceUploadRejectedOutsideRecording();
    TestFrameResourceUploadLimitsFromCapabilities();

    std::cout << "FrameResourceUploadArenaTest 合格\n";
    return 0;
}
