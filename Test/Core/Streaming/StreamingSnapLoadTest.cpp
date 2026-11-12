// ファストトラベルのスナップロード・先読み・ヒッチの数え方を確かめる。
#include "Streaming/StreamingSystem.h"

#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Streaming;

namespace
{
    int g_Failures = 0;

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    bool Check(bool bCondition, const char8_t* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << Utf8(u8"検査失敗: ") << Utf8(message) << "\n";
        }
        return bCondition;
    }

    class FakeClock final : public IStreamingClock
    {
    public:
        double NowMs() const override
        {
            return TimeMs;
        }

        double TimeMs = 100.0;
    };

    class ImmediateExecutor final : public IStreamingJobExecutor
    {
    public:
        bool Submit(Thread::TaskFunction job) override
        {
            job();
            return true;
        }
    };

    class FakeHandler final : public IStreamingPayloadHandler
    {
    public:
        explicit FakeHandler(FakeClock& clock) : m_Clock(clock)
        {
        }

        const char* GetKind() const override
        {
            return "alpha";
        }

        EStreamingPrepareResult Prepare(const StreamingPayloadContext&, const StreamingCancelToken& cancel) override
        {
            return cancel.IsCancelled() ? EStreamingPrepareResult::Cancelled : EStreamingPrepareResult::Success;
        }

        EStreamingStepResult CommitStep(const StreamingPayloadContext&, StreamingStepCursor& cursor,
                                        uint64_t& outCommittedBytes) override
        {
            m_Clock.TimeMs += 0.5;
            outCommittedBytes = 16;
            return ++cursor.Step == 3 ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        bool IsPayloadReady(const StreamingPayloadContext& context) const override
        {
            return Ready[context.Cell - 11][context.PayloadIndex];
        }

        void SetDormant(const StreamingPayloadContext&, bool) override
        {
        }

        EStreamingStepResult UnloadStep(const StreamingPayloadContext&, StreamingStepCursor&) override
        {
            return EStreamingStepResult::Done;
        }

        void DiscardPrepared(const StreamingPayloadContext&) override
        {
        }

        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext&) const override
        {
            return {64, 32};
        }

        bool Ready[3][2]{};

    private:
        FakeClock& m_Clock;
    };

    struct SnapRig
    {
        FakeClock Clock;
        ImmediateExecutor Executor;
        FakeHandler Handler{Clock};
        StreamingSystem System;

        bool Load()
        {
            System.SetEnvironment(&Clock, &Executor);
            StreamingSystemSettings settings;
            settings.Ring.ActiveRadius = 10.0f;
            settings.Ring.DormantRadius = 25.0f;
            settings.Ring.UnloadRadius = 40.0f;
            settings.Ring.DirectionWeight = 0.0f;
            settings.Ring.LatencySeconds = 0.0f;
            settings.MaxInFlightPrepares = 1;
            settings.Budget.MaxResidentCells = 1;
            settings.Budget.MaxCpuBytes = 1;
            settings.Budget.MaxGpuBytes = 1;
            settings.Budget.PerFrameCommitMs = 0.1;
            settings.Budget.PerFrameCommitBytes = 1;
            if (!Check(System.SetSettings(settings), u8"スナップ用の設定を受け付けません") ||
                !Check(System.RegisterHandler(&Handler), u8"スナップ用のハンドラを登録できません"))
            {
                return false;
            }
            const Container::String manifest = R"({"format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":1,"cells":[
                {"id":11,"bounds":{"min":[0,-1,-1],"max":[1,1,1]},"payloads":[
                    {"kind":"alpha","logicalPath":"cells/first"},{"kind":"alpha","logicalPath":"cells/second"}]},
                {"id":12,"bounds":{"min":[2,-1,-1],"max":[3,1,1]},"payloads":[
                    {"kind":"alpha","logicalPath":"cells/first"},{"kind":"alpha","logicalPath":"cells/second"}]},
                {"id":13,"bounds":{"min":[100,-1,-1],"max":[101,1,1]},"payloads":[
                    {"kind":"alpha","logicalPath":"cells/first"},{"kind":"alpha","logicalPath":"cells/second"}]}]})";
            if (!Check(System.LoadManifestFromText(manifest), u8"スナップ用のマニフェストを読み込めません"))
            {
                return false;
            }
            System.SetFocus({Math::Vector3(1000.0f, 0.0f, 0.0f), Math::Vector3::Zero});
            return true;
        }

        void Advance()
        {
            Clock.TimeMs += 16.0;
            System.Update();
        }

        bool FirstTargetsActive() const
        {
            return System.GetCellState(11) == EStreamingCellState::Active &&
                System.GetCellState(12) == EStreamingCellState::Active;
        }
    };

    void TestSnapReadyAfterAllActive()
    {
        uint32_t notifications = 0;
        StreamingSnapResult received;
        SnapRig rig;
        if (!rig.Load())
        {
            return;
        }
        rig.Advance();
        Check(rig.System.GetCellState(11) == EStreamingCellState::Unloaded &&
              rig.System.GetCellState(12) == EStreamingCellState::Unloaded,
              u8"遠い焦点だけでスナップ対象が読み込まれました");
        const double requestedAt = rig.Clock.TimeMs;
        Check(rig.System.RequestSnapLoad(Math::Vector3::Zero, 2.0f,
            [&](const StreamingSnapResult& result)
            {
                ++notifications;
                received = result;
                Check(rig.FirstTargetsActive(), u8"通知時点で対象の全セルが Active ではありません");
                Check(result.ElapsedMs == rig.Clock.TimeMs - requestedAt,
                      u8"通知までの経過時間が注入した時計と合いません");
            }), u8"スナップ要求を受け付けません");
        Check(notifications == 0, u8"Update 前にスナップ完了を通知しました");
        rig.Advance();
        Check(rig.System.GetLastFrameStats().InFlightPrepares == 2 && notifications == 0,
              u8"準備の同時実行数の上限を越えて対象を投入できないか、準備中に通知しました");
        rig.Advance();
        Check(rig.FirstTargetsActive() && rig.System.GetLastFrameStats().CommitSteps == 12 &&
              rig.System.GetLastFrameStats().CommitBytes == 192,
              u8"時間・バイト・常駐数の予算を越えて全対象をコミットできません");
        for (uint32_t frame = 0; frame < 4; ++frame)
        {
            rig.Advance();
            Check(rig.FirstTargetsActive() && notifications == 0,
                  u8"ペイロードの常駐待ちの間に通知するか対象を手放しました");
        }
        rig.Handler.Ready[0][0] = true;
        rig.Handler.Ready[0][1] = true;
        rig.Handler.Ready[1][0] = true;
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            rig.Advance();
            Check(notifications == 0, u8"最後のペイロードの常駐前に通知しました");
        }
        rig.Handler.Ready[1][1] = true;
        rig.Advance();
        Check(notifications == 1 && received.Status == EStreamingSnapStatus::Ready && received.Cells == 2,
              u8"全ペイロードの常駐後の Update で Ready と対象数を一度だけ通知しません");
        for (uint32_t frame = 0; frame < 40; ++frame)
        {
            rig.Advance();
            Check(rig.FirstTargetsActive() && notifications == 1,
                  u8"到着前に対象を休止・解放するか完了を二重に通知しました");
        }
        Check(rig.System.GetCellState(13) == EStreamingCellState::Unloaded,
              u8"目的地の半径外のセルまで読み込みました");
        rig.System.CancelSnapLoad();
        Check(notifications == 1, u8"通知済みの保持を外したときに再び通知しました");
    }

    void TestSnapSupersededIsCancelledOnce()
    {
        uint32_t firstNotifications = 0;
        uint32_t secondNotifications = 0;
        StreamingSnapResult firstResult;
        StreamingSnapResult secondResult;
        SnapRig rig;
        if (!rig.Load())
        {
            return;
        }
        Check(rig.System.RequestSnapLoad(Math::Vector3::Zero, 2.0f,
            [&](const StreamingSnapResult& result)
            {
                ++firstNotifications;
                firstResult = result;
            }), u8"最初のスナップ要求を受け付けません");
        rig.Advance();
        Check(firstNotifications == 0 && rig.System.GetCellState(11) == EStreamingCellState::Preparing,
              u8"差し替え前の最初の要求が未完了ではありません");
        Check(rig.System.RequestSnapLoad(Math::Vector3(100.0f, 0.0f, 0.0f), 0.0f,
            [&](const StreamingSnapResult& result)
            {
                ++secondNotifications;
                secondResult = result;
                Check(rig.System.GetCellState(13) == EStreamingCellState::Active,
                      u8"二番目の要求の対象が Active になる前に通知しました");
            }), u8"二番目のスナップ要求を受け付けません");
        Check(firstNotifications == 1 && firstResult.Status == EStreamingSnapStatus::Cancelled &&
              firstResult.Cells == 2 && secondNotifications == 0,
              u8"差し替え時に最初の要求へ Cancelled を一度だけ通知しません");
        rig.Advance();
        rig.Advance();
        Check(secondNotifications == 0 && rig.System.GetCellState(13) == EStreamingCellState::Active,
              u8"二番目の対象が Active にならないかペイロードの常駐前に通知しました");
        rig.Handler.Ready[2][0] = true;
        rig.Handler.Ready[2][1] = true;
        rig.Advance();
        Check(secondNotifications == 1 && secondResult.Status == EStreamingSnapStatus::Ready && secondResult.Cells == 1,
              u8"二番目の要求が Ready で一度だけ完了しません");
        for (uint32_t frame = 0; frame < 40; ++frame)
        {
            rig.Advance();
        }
        Check(firstNotifications == 1 && secondNotifications == 1,
              u8"差し替えた要求か二番目の要求が重複して通知されました");
        rig.System.CancelSnapLoad();
    }

    void TestHitchCounting()
    {
        StreamingHitchCounter warmup;
        bool bWarmupAccepted = true;
        for (uint32_t i = 0; i < 29; ++i)
        {
            bWarmupAccepted &= !warmup.AddFrame(16.0);
        }
        Check(bWarmupAccepted && !warmup.AddFrame(1000.0) && warmup.GetHitchCount() == 0 &&
              warmup.GetLastMedianMs() == 0.0,
              u8"最少標本数に達する前のフレームがヒッチとして数えられました");

        StreamingHitchCounter counter;
        for (uint32_t i = 0; i < 30; ++i)
        {
            counter.AddFrame(16.0);
        }
        Check(counter.AddFrame(33.0) && counter.GetHitchCount() == 1 &&
              counter.GetLastHitchFrameMs() == 33.0 && counter.GetLastMedianMs() == 16.0,
              u8"中央値の 2 倍を超えたフレームを記録できません");
        Check(!counter.AddFrame(32.0) && counter.GetHitchCount() == 1 &&
              counter.GetLastHitchFrameMs() == 33.0 && counter.GetLastMedianMs() == 16.0,
              u8"中央値のちょうど 2 倍をヒッチとして数えました");

        StreamingHitchCounter moving(4, 2);
        Check(!moving.AddFrame(2.0), u8"最初の標本をヒッチとして数えました");
        Check(!moving.AddFrame(4.0), u8"最少標本数に達する前にヒッチを数えました");
        Check(moving.AddFrame(20.0) && moving.GetLastMedianMs() == 3.0,
              u8"偶数個の標本の中央値が中央の 2 つの平均になりません");
        Check(!moving.AddFrame(3.0) && moving.GetLastMedianMs() == 4.0,
              u8"奇数個の標本の中央値が正しくありません");
        Check(!moving.AddFrame(5.0) && moving.GetLastMedianMs() == 3.5,
              u8"満杯の窓の中央値が正しくありません");
        Check(!moving.AddFrame(6.0) && moving.GetLastMedianMs() == 4.5,
              u8"最も古い標本を捨てた後の中央値が正しくありません");
        Check(!moving.AddFrame(7.0) && moving.GetLastMedianMs() == 5.5,
              u8"窓を再び更新した後の中央値が正しくありません");

        StreamingHitchCounter invalid(4, 2);
        invalid.AddFrame(4.0);
        invalid.AddFrame(6.0);
        const double invalidFrames[] = {
            -1.0,
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(),
        };
        bool bRejected = true;
        for (double frame : invalidFrames)
        {
            bRejected &= !invalid.AddFrame(frame);
        }
        Check(bRejected && invalid.GetHitchCount() == 0 && invalid.GetLastMedianMs() == 0.0 &&
              invalid.AddFrame(11.0) && invalid.GetHitchCount() == 1 &&
              invalid.GetLastHitchFrameMs() == 11.0 && invalid.GetLastMedianMs() == 5.0,
              u8"非有限または負の値が窓や回数に影響しました");
        Check(!invalid.AddFrame(-1.0) && invalid.GetLastHitchFrameMs() == 11.0 &&
              invalid.GetLastMedianMs() == 5.0,
              u8"無効な入力で直前の判定記録が変わりました");

        counter.Reset();
        Check(counter.GetHitchCount() == 0 && counter.GetLastHitchFrameMs() == 0.0 &&
              counter.GetLastMedianMs() == 0.0 && !counter.AddFrame(33.0) &&
              counter.GetHitchCount() == 0 && counter.GetLastMedianMs() == 0.0,
              u8"リセットで窓と判定記録が空になりません");

        StreamingHitchCounter clamped(0, 30);
        Check(!clamped.AddFrame(1.0) && clamped.AddFrame(3.0) &&
              clamped.GetHitchCount() == 1 && clamped.GetLastMedianMs() == 1.0,
              u8"窓の長さと最少標本数が正しく丸められません");
        StreamingHitchCounter minimum(4, 0);
        Check(!minimum.AddFrame(1.0) && !minimum.AddFrame(2.0) && minimum.GetLastMedianMs() == 1.0,
              u8"最少標本数が 0 のときに 1 へ丸められません");
    }
}

int main()
{
    TestHitchCounting();
    TestSnapReadyAfterAllActive();
    TestSnapSupersededIsCancelledOnce();
    if (g_Failures != 0)
    {
        std::cerr << Utf8(u8"検査失敗数: ") << g_Failures << "\n";
    }
    return g_Failures;
}
