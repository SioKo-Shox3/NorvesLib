#include "Rendering/FrameResourceOwner.h"
#include "Rendering/FrameUploadArena.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/Containers.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    // GPU を使わないページ（CPU のメモリ）。中身の書き込みと読み戻しで重なりを確かめる
    class FakeUploadBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeUploadBuffer(uint64_t size) : m_Bytes(static_cast<size_t>(size), 0) {}

        uint64_t GetSize() const override { return m_Bytes.size(); }
        void* Map(uint64_t offset, uint64_t) override { return m_Bytes.data() + offset; }
        void Unmap() override {}
        void Update(const void*, uint64_t, uint64_t) override {}
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::VertexBuffer |
                   RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer;
        }

    private:
        Container::VariableArray<uint8_t> m_Bytes;
    };

    // 複数のワーカーから同時にページを作られる口（本番のデバイスでの作成と同じく、作成そのものは同時に呼ばれてよい）。
    // 同時に作成した最大の数を数え、ワーカーで本当に同時にページを足したことを確かめる
    class ConcurrentPageSource final : public IFrameUploadPageSource
    {
    public:
        explicit ConcurrentPageSource(const FrameUploadLimits& limits) : m_Limits(limits) {}

        bool CreatePage(uint64_t sizeBytes, FrameUploadPage& outPage) override
        {
            m_Inside.fetch_add(1);
            // 同時に 2 つ以上の作成が重なるのをまだ見ていなければ、ほかのワーカーが作成へ入るのを少し待つ
            // （区間ごとの口なら一斉に最初のページを作るので重なる。1 つの排他を共有していれば重ならない）
            if (m_MaxInside.load() < 2)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
                while (m_Inside.load() < 2 && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                }
            }
            const int inside = m_Inside.load();
            int observed = m_MaxInside.load();
            while (inside > observed && !m_MaxInside.compare_exchange_weak(observed, inside))
            {
            }
            auto buffer = Container::MakeShared<FakeUploadBuffer>(sizeBytes);
            outPage.Buffer = buffer;
            outPage.CPUAddress = static_cast<uint8_t*>(buffer->Map(0, 0));
            outPage.Size = sizeBytes;
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Created.push_back(buffer);
            }
            m_Inside.fetch_sub(1);
            return true;
        }

        FrameUploadLimits GetLimits() const override { return m_Limits; }

        size_t GetCreatedCount()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_Created.size();
        }

        int GetMaxConcurrentCreates() const { return m_MaxInside.load(); }

    private:
        FrameUploadLimits m_Limits;
        std::mutex m_Mutex;
        std::atomic<int> m_Inside{0};
        std::atomic<int> m_MaxInside{0};
        Container::VariableArray<Container::TSharedPtr<FakeUploadBuffer>> m_Created;
    };

    FrameUploadLimits MakeTestLimits()
    {
        FrameUploadLimits limits;
        limits.ConstantsAlignment = 256;
        limits.StorageAlignment = 64;
        limits.VertexAlignment = 16;
        limits.IndexAlignment = 16;
        limits.MaxConstantsSize = 4096;
        limits.MaxStorageSize = 8192;
        return limits;
    }

    // 区間のワーカーが借りた切れ端と、書いた印（区間・回の番号）
    struct BorrowedSlice
    {
        FrameUploadAllocation Allocation;
        uint32_t Section = 0;
        uint32_t Segment = 0;
        uint32_t Iteration = 0;
        const FrameResourceLane* Lane = nullptr;
    };

    constexpr uint32_t SegmentCount = 8;
    constexpr uint32_t IterationsPerSegment = 400;

    uint8_t MakeMark(uint32_t section, uint32_t segment, uint32_t iteration)
    {
        return static_cast<uint8_t>((section * 101u + segment * 37u + iteration * 11u) | 1u);
    }

    // RenderGraph の Parallel の区間の記録を写した形: 文脈を写し、区間ごとの口を入れ、ワーカーから同時に借りる
    Container::VariableArray<BorrowedSlice> RecordParallelSection(ViewRenderContext& context, uint32_t sectionIndex)
    {
        FrameResourceOwner& owner = context.FrameResources->GetOwner();
        Container::VariableArray<ViewRenderContext> segmentContexts;
        segmentContexts.reserve(SegmentCount);
        for (uint32_t segment = 0; segment < SegmentCount; ++segment)
        {
            segmentContexts.push_back(context);
            FrameResourceLane* const lane = owner.AcquireLane();
            assert(lane != nullptr);
            assert(lane != context.FrameResources);
            assert(&lane->GetOwner() == &owner);
            segmentContexts.back().FrameResources = lane;
        }
        assert(owner.GetLeasedLaneCount() == SegmentCount);

        Container::VariableArray<Container::VariableArray<BorrowedSlice>> perSegment(SegmentCount);
        std::atomic<uint32_t> ready{0};
        Container::VariableArray<std::thread> workers;
        for (uint32_t segment = 0; segment < SegmentCount; ++segment)
        {
            workers.emplace_back([&, segment]() {
                ViewRenderContext& segmentContext = segmentContexts[segment];
                // 全部のワーカーがそろってから一斉に借りる
                ready.fetch_add(1);
                while (ready.load() < SegmentCount)
                {
                    std::this_thread::yield();
                }
                uint32_t seed = 2166136261u ^ (segment * 16777619u) ^ (sectionIndex * 7919u);
                for (uint32_t iteration = 0; iteration < IterationsPerSegment; ++iteration)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const FrameUploadUsage usage = static_cast<FrameUploadUsage>((seed >> 8) % 4u);
                    const uint64_t size = 1 + (seed >> 12) % 700u;
                    FrameUploadAllocation allocation = segmentContext.FrameResources->AllocateUpload(size, usage);
                    assert(allocation.IsValid());
                    std::memset(allocation.CPUAddress, MakeMark(sectionIndex, segment, iteration),
                                static_cast<size_t>(size));
                    perSegment[segment].push_back(
                        {allocation, sectionIndex, segment, iteration, segmentContext.FrameResources});
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }

        // ワーカーが終わったら口を返す（借りた切れ端はこのフレームの回収まで有効のまま）
        for (ViewRenderContext& segmentContext : segmentContexts)
        {
            owner.ReleaseLane(segmentContext.FrameResources);
        }
        assert(owner.GetLeasedLaneCount() == 0);

        Container::VariableArray<BorrowedSlice> all;
        for (const auto& slices : perSegment)
        {
            for (const BorrowedSlice& slice : slices)
            {
                all.push_back(slice);
            }
        }
        return all;
    }

    // 全部の切れ端が互いに重ならず、書いた印がそのまま残っていること
    void CheckNoOverlapAndContents(const Container::VariableArray<BorrowedSlice>& slices)
    {
        Container::VariableArray<const BorrowedSlice*> sorted;
        sorted.reserve(slices.size());
        for (const BorrowedSlice& slice : slices)
        {
            sorted.push_back(&slice);
        }
        std::sort(sorted.begin(), sorted.end(), [](const BorrowedSlice* a, const BorrowedSlice* b) {
            if (a->Allocation.Buffer.get() != b->Allocation.Buffer.get())
            {
                return a->Allocation.Buffer.get() < b->Allocation.Buffer.get();
            }
            return a->Allocation.Offset < b->Allocation.Offset;
        });
        for (size_t index = 1; index < sorted.size(); ++index)
        {
            const FrameUploadAllocation& previous = sorted[index - 1]->Allocation;
            const FrameUploadAllocation& current = sorted[index]->Allocation;
            if (previous.Buffer.get() == current.Buffer.get())
            {
                assert(previous.Offset + previous.Size <= current.Offset && "区間をまたいで切れ端が重なった");
            }
        }
        for (const BorrowedSlice& slice : slices)
        {
            const uint8_t mark = MakeMark(slice.Section, slice.Segment, slice.Iteration);
            const uint8_t* const bytes = static_cast<const uint8_t*>(slice.Allocation.CPUAddress);
            for (uint64_t offset = 0; offset < slice.Allocation.Size; ++offset)
            {
                assert(bytes[offset] == mark && "ほかの区間の書き込みで中身が変わった");
            }
            assert(slice.Allocation.Offset % std::max<uint64_t>(MakeTestLimits().GetAlignment(slice.Allocation.Usage), 1) ==
                   0);
        }
    }

    void TestFrameResourceParallelLanesDoNotOverlap()
    {
        ConcurrentPageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        // 小さなページにして、区間ごとにワーカーでページを何回も足す道を通す
        owners.ConfigureUpload(&source, 4096);

        ViewRenderContext context;
        context.FrameResources = owners.BeginFrame(0, 0);
        FrameResourceOwner& owner = context.FrameResources->GetOwner();
        assert(&owner == &owners.GetOwner(0));
        assert(context.FrameResources->GetLaneIndex() == 0);

        // 直列の記録（既定の口）で借りた切れ端
        constexpr uint32_t SerialSection = 99;
        Container::VariableArray<BorrowedSlice> all;
        for (uint32_t iteration = 0; iteration < 16; ++iteration)
        {
            FrameUploadAllocation allocation = context.FrameResources->AllocateUpload(100, FrameUploadUsage::Constants);
            assert(allocation.IsValid());
            std::memset(allocation.CPUAddress, MakeMark(SerialSection, 0, iteration), 100);
            all.push_back({allocation, SerialSection, 0, iteration, context.FrameResources});
        }

        // 同じフレームに Parallel の区間を 2 回記録する（2 回目は返された口を使い回し、前へ進むだけなので重ならない）
        const Container::VariableArray<BorrowedSlice> first = RecordParallelSection(context, 1);
        assert(owner.GetLaneCount() == SegmentCount);
        const Container::VariableArray<BorrowedSlice> second = RecordParallelSection(context, 2);
        assert(owner.GetLaneCount() == SegmentCount && "返された口を次の区間が使い回す");
        for (const BorrowedSlice& slice : first)
        {
            all.push_back(slice);
        }
        for (const BorrowedSlice& slice : second)
        {
            all.push_back(slice);
        }

        // 直列の口と 2 回の区間の全部の切れ端が互いに重ならず、書いた印がそのまま残っている
        CheckNoOverlapAndContents(all);

        // 口はそれぞれ自分のページだけを使う（2 つの口が同じページを共有しない）
        {
            Container::VariableArray<std::pair<const RHI::IBuffer*, const FrameResourceLane*>> pageLanes;
            for (const BorrowedSlice& slice : all)
            {
                pageLanes.push_back({slice.Allocation.Buffer.get(), slice.Lane});
            }
            std::sort(pageLanes.begin(), pageLanes.end());
            for (size_t index = 1; index < pageLanes.size(); ++index)
            {
                if (pageLanes[index - 1].first == pageLanes[index].first)
                {
                    assert(pageLanes[index - 1].second == pageLanes[index].second && "2 つの口が同じページを使った");
                }
            }
        }

        // 借りた物はどの口で借りても持ち主の記録中のフレームの世代を持つ
        for (const BorrowedSlice& slice : all)
        {
            assert(owner.ValidateBorrowed(slice.Allocation, "ParallelSlice"));
            assert(slice.Allocation.GenerationSlotIndex == 0);
        }

        // ワーカーで本当に同時にページを足したこと（口ごとに 1 つ以上のページ）
        assert(source.GetMaxConcurrentCreates() >= 2);
        for (uint32_t index = 0; index < owner.GetLaneCount(); ++index)
        {
            const FrameResourceLane& lane = owner.GetLane(index);
            assert(lane.GetLaneIndex() == index + 1);
            assert(lane.GetUploadArena().GetPageCount() >= 1);
            assert(lane.GetUploadArena().GetFrameStats().Allocations == IterationsPerSegment * 2);
        }

        // 提出で全部の口の統計を合計する
        const uint64_t expectedAllocations = 16 + static_cast<uint64_t>(SegmentCount) * IterationsPerSegment * 2;
        assert(owner.GetFrameUploadStats().Allocations == expectedAllocations);
        assert(owners.CommitSubmitted(1));
        assert(owners.GetLastFrameUploadStats().Allocations == expectedAllocations);
        assert(owners.GetLastFrameUploadStats().Failures == 0);
        const size_t pagesBeforeReuse = source.GetCreatedCount();
        assert(owners.GetTotalUploadPagesAdded() == pagesBeforeReuse);

        // 回収で全部の口が先頭へ戻り、次のフレームはページを使い回す（同じ借り方ならページを足さない）
        context.FrameResources = owners.BeginFrame(0, 1);
        assert(&context.FrameResources->GetOwner() == &owner);
        assert(owner.GetReclaimCount() == 1);
        for (uint32_t index = 0; index < owner.GetLaneCount(); ++index)
        {
            assert(owner.GetLane(index).GetUploadArena().GetFrameStats().Allocations == 0);
        }
        const Container::VariableArray<BorrowedSlice> reused = RecordParallelSection(context, 3);
        CheckNoOverlapAndContents(reused);
        assert(owner.GetLaneCount() == SegmentCount);
        // 前のフレームの切れ端は世代が違うので拒む（開発ビルド）
#if NORVES_BUILD_DEVELOPMENT
        assert(!owner.ValidateBorrowed(first.front().Allocation, "PreviousFrameSlice"));
#endif
        owners.CommitSubmitted(2);
        assert(source.GetCreatedCount() == pagesBeforeReuse && "回収した口のページを使い回す");

        owners.Reset();
        owners.ReleaseUploadPages();
        assert(owner.GetUploadPageCount() == 0);
    }

    void TestFrameResourceLaneLeaseRules()
    {
        ConcurrentPageSource source(MakeTestLimits());
        FrameResourceOwners owners;
        owners.ConfigureUpload(&source, 4096);

        // 記録中でなければ口を貸さない（RenderGraph は既定の口のまま記録する）
        assert(owners.GetOwner(0).AcquireLane() == nullptr);

        FrameResourceOwner* owner = owners.BeginFrame(0, 0);
        FrameResourceLane* const a = owner->AcquireLane();
        FrameResourceLane* const b = owner->AcquireLane();
        assert(a && b && a != b);
        assert(owner->GetLeasedLaneCount() == 2);
        // 口は後から設定を引き継ぐ（ページの大きさ・作る口）
        assert(a->GetUploadArena().IsConfigured() && a->GetUploadArena().GetPageBytes() == 4096);
        assert(a->AllocateUpload(16, FrameUploadUsage::Vertex).IsValid());
        owner->ReleaseLane(a);
        // 返した口は次に借りるときに使い回す
        assert(owner->AcquireLane() == a);
        owner->ReleaseLane(a);
        // 既定の口（持ち主自身）を返しても何もしない
        owner->ReleaseLane(owner);
        assert(owner->GetLeasedLaneCount() == 1);

        // 返さないまま回収まで来た口は、後始末の漏れとして数えて返したものとして扱う
        owners.CommitSubmitted(1);
        assert(!b->AllocateUpload(16, FrameUploadUsage::Vertex).IsValid() && "提出の後は区間の口でも借りられない");
        owners.Retire(1);
        assert(owner->GetLeakedLaneLeaseCount() == 1);
        assert(owner->GetLeasedLaneCount() == 0);

        // スロットごとに口は別（別のスロットの記録中に前のスロットの口を使わない）
        FrameResourceOwner* owner1 = owners.BeginFrame(1, 1);
        FrameResourceLane* const c = owner1->AcquireLane();
        assert(c && c != a && c != b && &c->GetOwner() == owner1);
        owner1->ReleaseLane(c);
        owners.AbortFrame();
        owners.Reset();
        owners.ReleaseUploadPages();
    }
} // namespace

int main()
{
    std::cout << "FrameResourceParallelAllocTest 開始\n";

    TestFrameResourceParallelLanesDoNotOverlap();
    TestFrameResourceLaneLeaseRules();

    std::cout << "FrameResourceParallelAllocTest 合格\n";
    return 0;
}
