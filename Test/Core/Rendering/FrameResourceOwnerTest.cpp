#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/Containers.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    // 借りる口の代わり。借りるたびに次の番号を渡し、回収で 0 へ戻す（リングや pool の位置に当たる）
    class CountingAllocator final : public IFrameResourceAllocator
    {
    public:
        struct Borrowed
        {
            uint32_t Index = 0;
            FrameResourceGeneration Generation;
        };

        Borrowed Borrow(const FrameResourceOwner& owner)
        {
            assert(owner.IsRecording());
            return {m_Cursor++, owner.GetGeneration()};
        }

        void OnFrameResourcesReclaimed(const FrameResourceGeneration& generation) override
        {
            m_Cursor = 0;
            m_Reclaimed.push_back(generation);
        }

        uint32_t GetCursor() const { return m_Cursor; }
        const Container::VariableArray<FrameResourceGeneration>& GetReclaimed() const { return m_Reclaimed; }

    private:
        uint32_t m_Cursor = 0;
        Container::VariableArray<FrameResourceGeneration> m_Reclaimed;
    };

    // パスの Execute の代わり。文脈から持ち主を引いて借りるだけで、飛行中の番号も通し番号も見ない
    CountingAllocator::Borrowed ExecuteFakePass(ViewRenderContext& context, CountingAllocator& allocator)
    {
        assert(context.FrameResources != nullptr);
        return allocator.Borrow(*context.FrameResources);
    }

    void TestFrameResourceSecondExecuteKeepsEarlierBorrows()
    {
        FrameResourceOwners owners;
        CountingAllocator allocator;
        owners.GetOwner(0).AttachAllocator(&allocator);

        ViewRenderContext context;
        context.FrameResources = owners.BeginFrame(0, 0);
        assert(context.FrameResources == &owners.GetOwner(0));

        // 同じフレームに同じパスが 2 回 Execute しても、2 回目は 1 回目の後ろを借り、1 回目の分はまだ使える
        const CountingAllocator::Borrowed first = ExecuteFakePass(context, allocator);
        const CountingAllocator::Borrowed second = ExecuteFakePass(context, allocator);
        assert(first.Index == 0);
        assert(second.Index == 1);
        assert(first.Generation.IsSet());
        assert(first.Generation.Generation == second.Generation.Generation);
        assert(context.FrameResources->ValidateBorrowed(first.Generation, "First"));
        assert(context.FrameResources->IsCurrent(first.Generation));
        assert(context.FrameResources->IsCurrent(second.Generation));
        assert(allocator.GetReclaimed().empty());
        assert(owners.GetRecordingOwner() == context.FrameResources);

        // 提出の後も、完了するまでは回収しない
        assert(owners.CommitSubmitted(7));
        assert(owners.GetRecordingOwner() == nullptr);
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Submitted);
        assert(owners.GetOwner(0).GetSubmissionSerial() == 7);
        owners.Retire(6);
        assert(allocator.GetReclaimed().empty());
        assert(allocator.GetCursor() == 2);

        owners.GetOwner(0).DetachAllocator(&allocator);
    }

    void TestFrameResourceAbortReclaimsAtNextBegin()
    {
        FrameResourceOwners owners;
        CountingAllocator allocator0;
        CountingAllocator allocator1;
        owners.GetOwner(0).AttachAllocator(&allocator0);
        owners.GetOwner(1).AttachAllocator(&allocator1);

        // 提出しなかったフレームは、中止の時点ではなく次の開始で回収する（別のスロットの開始でも戻す）
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);
        allocator0.Borrow(*owner);
        const FrameResourceGeneration abortedGeneration = owner->GetGeneration();
        owners.AbortFrame();
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Aborted);
        assert(allocator0.GetReclaimed().empty());

        owner = owners.BeginFrame(1, 0);
        assert(owner == &owners.GetOwner(1));
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Idle);
        assert(allocator0.GetReclaimed().size() == 1);
        assert(allocator0.GetReclaimed()[0].Generation == abortedGeneration.Generation);
        assert(allocator0.GetCursor() == 0);

        // 0 の通し番号での提出は中止と同じ
        allocator1.Borrow(*owner);
        assert(!owners.CommitSubmitted(0));
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Aborted);

        // 提出も中止もしないまま（例外などで）次の開始が来たら、中止として扱って回収する
        owner = owners.BeginFrame(0, 0);
        assert(allocator1.GetReclaimed().size() == 1);
        allocator0.Borrow(*owner);
        owner = owners.BeginFrame(1, 0);
        assert(owners.GetAbandonedFrameCount() == 1);
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Idle);
        assert(allocator0.GetReclaimed().size() == 2);

        owners.GetOwner(0).DetachAllocator(&allocator0);
        owners.GetOwner(1).DetachAllocator(&allocator1);
    }

    void TestFrameResourceRetireReclaimsOnCompletion()
    {
        FrameResourceOwners owners;
        CountingAllocator allocator0;
        CountingAllocator allocator1;
        owners.GetOwner(0).AttachAllocator(&allocator0);
        owners.GetOwner(1).AttachAllocator(&allocator1);

        // 2 フレームを重ねる: スロット 0 を通し番号 1、スロット 1 を 2 で提出
        allocator0.Borrow(*owners.BeginFrame(0, 0));
        assert(owners.CommitSubmitted(1));
        allocator1.Borrow(*owners.BeginFrame(1, 0));
        assert(owners.CommitSubmitted(2));

        // 1 だけ完了: スロット 0 だけ戻り、スロット 1 は GPU が使っているので残る
        owners.Retire(1);
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Idle);
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Submitted);
        assert(allocator0.GetReclaimed().size() == 1);
        assert(allocator1.GetReclaimed().empty());

        // 次の開始（完了の通し番号 2）でスロット 1 も戻る
        FrameResourceOwner* owner = owners.BeginFrame(0, 2);
        assert(owner == &owners.GetOwner(0));
        assert(allocator1.GetReclaimed().size() == 1);
        assert(allocator1.GetCursor() == 0);
        assert(owners.GetUnretiredBeginCount() == 0);
        assert(owners.GetOwner(0).GetReclaimCount() == 1);
        assert(owners.GetOwner(1).GetReclaimCount() == 1);

        owners.GetOwner(0).DetachAllocator(&allocator0);
        owners.GetOwner(1).DetachAllocator(&allocator1);
    }

    void TestFrameResourceSerialRestartReclaimsSubmitted()
    {
        FrameResourceOwners owners;
        CountingAllocator allocator0;
        CountingAllocator allocator1;
        owners.GetOwner(0).AttachAllocator(&allocator0);
        owners.GetOwner(1).AttachAllocator(&allocator1);

        owners.BeginFrame(0, 0);
        assert(owners.CommitSubmitted(100));
        owners.BeginFrame(1, 99);
        assert(owners.CommitSubmitted(101));
        owners.Retire(99);
        assert(owners.GetOwner(0).GetState() == FrameResourceOwnerState::Submitted);
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Submitted);

        // スワップチェーンの作り直しで完了の通し番号が前より小さくなった: 作り直す前に GPU を待ち終えているので全部戻す
        FrameResourceOwner* owner = owners.BeginFrame(0, 0);
        assert(owners.GetSerialRestartCount() == 1);
        assert(owners.GetUnretiredBeginCount() == 0);
        assert(owners.GetOwner(1).GetState() == FrameResourceOwnerState::Idle);
        assert(allocator0.GetReclaimed().size() == 1);
        assert(allocator1.GetReclaimed().size() == 1);

        // 振り直した後の通し番号で提出し、その完了で戻る
        assert(owners.CommitSubmitted(1));
        owners.Retire(0);
        assert(owner->GetState() == FrameResourceOwnerState::Submitted);
        owners.Retire(1);
        assert(owner->GetState() == FrameResourceOwnerState::Idle);
        assert(owners.GetSerialRestartCount() == 1);
        assert(allocator0.GetReclaimed().size() == 2);

        owners.GetOwner(0).DetachAllocator(&allocator0);
        owners.GetOwner(1).DetachAllocator(&allocator1);
    }

    void TestFrameResourceStaleGenerationRejected()
    {
        FrameResourceOwners owners;
        CountingAllocator allocator;
        owners.GetOwner(0).AttachAllocator(&allocator);

        FrameResourceOwner* owner = owners.BeginFrame(0, 0);
        const CountingAllocator::Borrowed borrowed = allocator.Borrow(*owner);
        assert(owner->ValidateBorrowed(borrowed.Generation, "Borrowed"));
        assert(owner->GetStaleRejectCount() == 0);
        assert(owners.CommitSubmitted(1));

        // 提出の後（記録中でない）は、借りたフレームの物でも使えない
        assert(!owner->GetGeneration().IsSet());
#if NORVES_BUILD_DEVELOPMENT
        assert(!owner->ValidateBorrowed(borrowed.Generation, "Borrowed"));
        assert(owner->GetStaleRejectCount() == 1);
#endif

        // 同じスロットの次のフレームでは、世代が変わるので前のフレームで借りた物を拒む
        owner = owners.BeginFrame(0, 1);
        assert(owner == &owners.GetOwner(0));
        assert(owner->GetGeneration().Generation != borrowed.Generation.Generation);
        assert(!owner->IsCurrent(borrowed.Generation));
#if NORVES_BUILD_DEVELOPMENT
        assert(!owner->ValidateBorrowed(borrowed.Generation, "Borrowed"));
        // 未設定の世代と、別のスロットで借りた物も拒む
        assert(!owner->ValidateBorrowed(FrameResourceGeneration{}, "Unset"));
        const FrameResourceGeneration otherSlot{1, owner->GetGeneration().Generation};
        assert(!owner->ValidateBorrowed(otherSlot, "OtherSlot"));
        assert(owner->GetStaleRejectCount() == 4);
#endif
        const CountingAllocator::Borrowed fresh = allocator.Borrow(*owner);
        assert(owner->ValidateBorrowed(fresh.Generation, "Fresh"));

        owners.GetOwner(0).DetachAllocator(&allocator);
    }
} // namespace

int main()
{
    std::cout << "FrameResourceOwnerTest start\n";

    TestFrameResourceSecondExecuteKeepsEarlierBorrows();
    TestFrameResourceAbortReclaimsAtNextBegin();
    TestFrameResourceRetireReclaimsOnCompletion();
    TestFrameResourceSerialRestartReclaimsSubmitted();
    TestFrameResourceStaleGenerationRejected();

    std::cout << "FrameResourceOwnerTest passed\n";
    return 0;
}
