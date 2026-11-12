// セルの状態遷移とマニフェストの拒否を確かめる。
#include "Streaming/StreamingSystem.h"

#include <iostream>
#include <utility>

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

        double TimeMs = 0.0;
    };

    class ManualExecutor final : public IStreamingJobExecutor
    {
    public:
        bool Submit(Thread::TaskFunction job) override
        {
            ++SubmitCount;
            if (!bAccept)
            {
                return false;
            }
            Jobs.push_back(std::move(job));
            return true;
        }

        bool RunOne()
        {
            if (Jobs.empty())
            {
                return false;
            }
            auto job = std::move(Jobs.front());
            Jobs.erase(Jobs.begin());
            job();
            return true;
        }

        void RunAll()
        {
            while (RunOne())
            {
            }
        }

        size_t PendingCount() const
        {
            return Jobs.size();
        }

        bool bAccept = true;
        uint32_t SubmitCount = 0;

    private:
        Container::VariableArray<Thread::TaskFunction> Jobs;
    };

    enum class ECall : uint8_t
    {
        Prepare, Commit, Dormant, Unload, Discard, ResidentBytes
    };

    struct Call
    {
        ECall Kind;
        StreamingCellId Cell;
        uint32_t Payload;
        uint32_t Handler;
        bool bDormant = false;
        EStreamingPrepareResult PrepareResult = EStreamingPrepareResult::Success;
    };

    struct PrepareRule
    {
        StreamingCellId Cell;
        uint32_t FailuresRemaining;
        EStreamingPrepareResult Afterwards;
    };

    class FakeHandler final : public IStreamingPayloadHandler
    {
    public:
        FakeHandler(const char* kind, uint32_t handler, FakeClock& clock, Container::VariableArray<Call>& calls)
            : m_Kind(kind), m_Handler(handler), m_Clock(clock), m_Calls(calls)
        {
        }

        const char* GetKind() const override
        {
            return m_Kind;
        }

        EStreamingPrepareResult Prepare(const StreamingPayloadContext& context, const StreamingCancelToken& cancel) override
        {
            EStreamingPrepareResult result = EStreamingPrepareResult::Success;
            if (cancel.IsCancelled())
            {
                result = EStreamingPrepareResult::Cancelled;
            }
            else
            {
                for (auto& rule : Rules)
                {
                    if (rule.Cell == context.Cell)
                    {
                        if (rule.FailuresRemaining > 0)
                        {
                            --rule.FailuresRemaining;
                            result = EStreamingPrepareResult::Failed;
                        }
                        else
                        {
                            result = rule.Afterwards;
                        }
                        break;
                    }
                }
            }
            m_Calls.push_back({ECall::Prepare, context.Cell, context.PayloadIndex, m_Handler, false, result});
            return result;
        }

        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor,
                                        uint64_t& outCommittedBytes) override
        {
            m_Calls.push_back({ECall::Commit, context.Cell, context.PayloadIndex, m_Handler});
            m_Clock.TimeMs += CommitStepMs;
            outCommittedBytes = 16;
            ++cursor.Step;
            return cursor.Step >= CommitSteps ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        void SetDormant(const StreamingPayloadContext& context, bool bDormant) override
        {
            m_Calls.push_back({ECall::Dormant, context.Cell, context.PayloadIndex, m_Handler, bDormant});
        }

        EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) override
        {
            m_Calls.push_back({ECall::Unload, context.Cell, context.PayloadIndex, m_Handler});
            ++cursor.Step;
            return cursor.Step >= UnloadSteps ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        void DiscardPrepared(const StreamingPayloadContext& context) override
        {
            m_Calls.push_back({ECall::Discard, context.Cell, context.PayloadIndex, m_Handler});
        }

        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext& context) const override
        {
            m_Calls.push_back({ECall::ResidentBytes, context.Cell, context.PayloadIndex, m_Handler});
            return ResidentBytes;
        }

        Container::VariableArray<PrepareRule> Rules;
        uint32_t CommitSteps = 2;
        uint32_t UnloadSteps = 2;
        double CommitStepMs = 1.0;
        StreamingPayloadBytes ResidentBytes{64, 32};

    private:
        const char* m_Kind;
        uint32_t m_Handler;
        FakeClock& m_Clock;
        Container::VariableArray<Call>& m_Calls;
    };

    Container::String MakeManifest(const char* kind = "alpha", const char* path = "cells/item",
                                   const char* cellSize = "1", const char* minX = "-1",
                                   uint32_t payloadCount = 1, bool bDuplicate = false,
                                   const char* format = "norves.cell-manifest.v1")
    {
        Container::String cell = R"({"id":11,"bounds":{"min":[)";
        cell += minX;
        cell += R"(,-1,-1],"max":[1,1,1]},"payloads":[{"kind":")";
        cell += kind;
        cell += R"(","logicalPath":")";
        cell += path;
        cell += R"("})";
        if (payloadCount >= 2)
        {
            cell += R"(,{"kind":"beta","logicalPath":"cells/second"})";
        }
        if (payloadCount >= 3)
        {
            cell += R"(,{"kind":"alpha","logicalPath":"cells/third"})";
        }
        cell += "]}";

        Container::String text = R"({"format":")";
        text += format;
        text += R"(","origin":[0,0,0],"cellSize":)";
        text += cellSize;
        text += R"(,"cells":[)";
        text += cell;
        if (bDuplicate)
        {
            text += ",";
            text += cell;
        }
        text += "]}";
        return text;
    }

    StreamingSystemSettings MakeSettings()
    {
        StreamingSystemSettings settings;
        settings.Ring.ActiveRadius = 5.0f;
        settings.Ring.DormantRadius = 10.0f;
        settings.Ring.UnloadRadius = 20.0f;
        settings.Ring.DirectionWeight = 0.0f;
        settings.Ring.LatencySeconds = 0.0f;
        settings.Budget.PerFrameCommitMs = 1.0;
        settings.Retry.MaxPrepareAttempts = 3;
        settings.Retry.RetryDelayMs = 10.0;
        return settings;
    }

    struct TestRig
    {
        FakeClock Clock;
        ManualExecutor Executor;
        Container::VariableArray<Call> Calls;
        FakeHandler Alpha{"alpha", 0, Clock, Calls};
        FakeHandler Beta{"beta", 1, Clock, Calls};
        StreamingSystem System;

        ~TestRig()
        {
            // 完了待ちへ入る前に、手動実行器の仕事を必ず回す。
            Executor.RunAll();
            System.Shutdown();
        }

        bool Load(const Container::String& text, const StreamingSystemSettings& settings = MakeSettings())
        {
            Executor.RunAll();
            System.SetEnvironment(&Clock, &Executor);
            if (!Check(System.SetSettings(settings), u8"輪の設定を受け付けません") ||
                !Check(System.RegisterHandler(&Alpha), u8"alpha を登録できません") ||
                !Check(System.RegisterHandler(&Beta), u8"beta を登録できません"))
            {
                return false;
            }
            System.SetRecordTransitions(true);
            Executor.RunAll();
            CellManifestError error;
            if (!System.LoadManifestFromText(text, &error))
            {
                ++g_Failures;
                std::cerr << Utf8(u8"検査失敗: マニフェストを読み込めません: ")
                          << ToString(error.Code) << "\n";
                return false;
            }
            return true;
        }

        void Focus(float x)
        {
            System.SetFocus({Math::Vector3(x, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 0.0f)});
        }
    };

    Container::VariableArray<Call> CallsOf(const TestRig& rig, ECall kind)
    {
        Container::VariableArray<Call> result;
        for (const auto& call : rig.Calls)
        {
            if (call.Kind == kind)
            {
                result.push_back(call);
            }
        }
        return result;
    }

    size_t CountCalls(const TestRig& rig, ECall kind, StreamingCellId cell)
    {
        size_t count = 0;
        for (const auto& call : rig.Calls)
        {
            if (call.Kind == kind && call.Cell == cell)
            {
                ++count;
            }
        }
        return count;
    }

    struct StatePair
    {
        EStreamingCellState From;
        EStreamingCellState To;
    };

    template<size_t N>
    void CheckTransitions(const TestRig& rig, const StatePair (&expected)[N], const char8_t* message)
    {
        const auto transitions = rig.System.GetTransitions();
        if (!Check(transitions.size() == N, message))
        {
            return;
        }
        for (size_t i = 0; i < N; ++i)
        {
            const auto& actual = transitions[i];
            if (actual.Cell != 11 || actual.From != expected[i].From || actual.To != expected[i].To)
            {
                ++g_Failures;
                std::cerr << Utf8(u8"検査失敗: ") << Utf8(message) << Utf8(u8" 順番=") << i
                          << Utf8(u8" 実際=") << ToString(actual.From) << " -> " << ToString(actual.To)
                          << Utf8(u8" 期待=") << ToString(expected[i].From) << " -> "
                          << ToString(expected[i].To) << "\n";
            }
        }
    }

    template<size_t N>
    void CheckPayloadOrder(const Container::VariableArray<Call>& calls, const uint32_t (&expected)[N],
                           const char8_t* message)
    {
        if (!Check(calls.size() == N, message))
        {
            return;
        }
        for (size_t i = 0; i < N; ++i)
        {
            const uint32_t expectedHandler = expected[i] == 1 ? 1 : 0;
            Check(calls[i].Cell == 11 && calls[i].Payload == expected[i] && calls[i].Handler == expectedHandler,
                  message);
        }
    }

    void TestCompleteTransitions()
    {
        using S = EStreamingCellState;
        TestRig rig;
        if (!rig.Load(MakeManifest("alpha", "cells/item", "1", "-1", 2)))
        {
            return;
        }
        rig.Focus(0.0f);
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Preparing && rig.Executor.PendingCount() == 1,
              u8"近いセルの準備が投入されません");
        Check(rig.Executor.RunOne(), u8"準備の仕事がありません");
        for (uint32_t step = 0; step < 4; ++step)
        {
            rig.System.Update();
            Check(rig.System.GetLastFrameStats().CommitSteps == 1,
                  u8"時間予算によりコミットが一歩ずつ進みません");
            Check(rig.System.GetCellState(11) == (step == 3 ? S::Active : S::Committing),
                  u8"複数歩のコミット中または完了後の状態が違います");
        }
        Check(rig.System.GetLastFrameStats().ResidentCpuBytes == 128 &&
              rig.System.GetLastFrameStats().ResidentGpuBytes == 64,
              u8"二つのペイロードの常駐量が集計されません");
        rig.Focus(14.0f);
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Dormant, u8"中間の輪で休止しません");
        rig.Focus(0.0f);
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Active, u8"近づいても休止から復帰しません");
        rig.Focus(100.0f);
        for (uint32_t step = 0; step < 4; ++step)
        {
            rig.System.Update();
            Check(rig.System.GetCellState(11) == (step == 3 ? S::Unloaded : S::Unloading),
                  u8"複数歩のアンロード中または完了後の状態が違います");
        }

        const StatePair expected[] = {
            {S::Unloaded, S::Preparing}, {S::Preparing, S::ReadyToCommit},
            {S::ReadyToCommit, S::Committing}, {S::Committing, S::Active},
            {S::Active, S::Dormant}, {S::Dormant, S::Active},
            {S::Active, S::Unloading}, {S::Unloading, S::Unloaded}
        };
        CheckTransitions(rig, expected, u8"通常の状態遷移の列が違います");
        const uint32_t prepareOrder[] = {0, 1};
        const uint32_t commitOrder[] = {0, 0, 1, 1};
        const uint32_t unloadOrder[] = {1, 1, 0, 0};
        const uint32_t dormantOrder[] = {0, 1, 0, 1};
        const uint32_t discardOrder[] = {0, 1};
        const auto prepared = CallsOf(rig, ECall::Prepare);
        CheckPayloadOrder(prepared, prepareOrder, u8"準備の順番が違います");
        if (prepared.size() == 2)
        {
            Check(prepared[0].PrepareResult == EStreamingPrepareResult::Success &&
                  prepared[1].PrepareResult == EStreamingPrepareResult::Success,
                  u8"二つのペイロードの準備が成功していません");
        }
        CheckPayloadOrder(CallsOf(rig, ECall::Commit), commitOrder, u8"コミットの順番が違います");
        CheckPayloadOrder(CallsOf(rig, ECall::Unload), unloadOrder, u8"アンロードが逆順ではありません");
        const auto dormant = CallsOf(rig, ECall::Dormant);
        CheckPayloadOrder(dormant, dormantOrder, u8"休止切り替えの呼び出し順が違います");
        if (dormant.size() == 4)
        {
            Check(dormant[0].bDormant && dormant[1].bDormant && !dormant[2].bDormant && !dormant[3].bDormant,
                  u8"休止と復帰の引数が違います");
        }
        CheckPayloadOrder(CallsOf(rig, ECall::Discard), discardOrder, u8"解放後の準備済みデータの破棄が違います");
    }

    void TestCancelPreparing()
    {
        using S = EStreamingCellState;
        TestRig rig;
        if (!rig.Load(MakeManifest("alpha", "cells/item", "1", "-1", 2)))
        {
            return;
        }
        rig.Focus(0.0f);
        rig.System.Update();
        Check(rig.Executor.PendingCount() == 1 && rig.System.GetCellState(11) == S::Preparing,
              u8"取り消す仕事が準備中ではありません");
        rig.Focus(100.0f);
        rig.System.Update();
        Check(rig.Executor.PendingCount() == 1 && rig.System.GetCellState(11) == S::Preparing,
              u8"取り消しの完了前に準備中でなくなりました");
        Check(rig.Executor.RunOne(), u8"取り消した仕事がありません");
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Unloaded, u8"取り消し後に未読込へ戻りません");
        const auto prepared = CallsOf(rig, ECall::Prepare);
        if (!prepared.empty())
        {
            Check(prepared.size() == 1 && prepared[0].PrepareResult == EStreamingPrepareResult::Cancelled,
                  u8"取り消し済みの準備が取り消しを返しません");
        }
        Check(CallsOf(rig, ECall::Commit).empty(), u8"取り消したセルがコミットされました");
        const uint32_t discardOrder[] = {0, 1};
        CheckPayloadOrder(CallsOf(rig, ECall::Discard), discardOrder, u8"取り消し後に準備済みデータを破棄しません");
        const StatePair expected[] = {{S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded}};
        CheckTransitions(rig, expected, u8"準備中の取り消しの遷移が違います");

        // セルの公開口では取り消し印そのものと、それを受けた手続きの戻り値を確かめる。
        const char* kinds[] = {"alpha", "beta"};
        CellManifest manifest;
        if (!Check(CellManifest::TryParse(MakeManifest(), {kinds, 2}, manifest),
                   u8"取り消し印の検査用マニフェストを読めません"))
        {
            return;
        }
        const auto* desc = manifest.FindCell(11);
        if (!Check(desc != nullptr, u8"取り消し印の検査用セルがありません"))
        {
            return;
        }
        StreamingCell cell(desc, MakeSettings().Retry);
        if (!Check(cell.BeginPrepare(rig.Clock.NowMs()), u8"取り消し印の検査で準備を始められません"))
        {
            return;
        }
        const auto token = cell.GetCancelToken();
        cell.RequestUnload();
        if (Check(Container::IsValid(token) && token->IsCancelled(), u8"準備中の解放要求で取り消し印が立ちません"))
        {
            Check(rig.Alpha.Prepare({11, 0, &desc->Payloads[0]}, *token) == EStreamingPrepareResult::Cancelled,
                  u8"手続きが取り消し印を見て取り消しを返しません");
        }
    }

    void TestUnloadDuringCommit()
    {
        using S = EStreamingCellState;
        TestRig rig;
        rig.Alpha.CommitSteps = 1;
        rig.Beta.CommitSteps = 3;
        if (!rig.Load(MakeManifest("alpha", "cells/item", "1", "-1", 3)))
        {
            return;
        }
        rig.Focus(0.0f);
        rig.System.Update();
        Check(rig.Executor.RunOne(), u8"途中解放するセルの準備がありません");
        rig.System.Update();
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Committing &&
              rig.System.GetLastFrameStats().CommitSteps == 1,
              u8"二番目のペイロードのコミット途中で止まりません");
        const uint32_t commitOrder[] = {0, 1};
        CheckPayloadOrder(CallsOf(rig, ECall::Commit), commitOrder, u8"途中解放前に開始したペイロードが違います");
        rig.Focus(100.0f);
        for (uint32_t step = 0; step < 4; ++step)
        {
            rig.System.Update();
            Check(rig.System.GetCellState(11) == (step == 3 ? S::Unloaded : S::Unloading),
                  u8"コミット途中の解放が直ちに始まらないか完了しません");
        }
        const uint32_t unloadOrder[] = {1, 1, 0, 0};
        const uint32_t discardOrder[] = {0, 1, 2};
        CheckPayloadOrder(CallsOf(rig, ECall::Unload), unloadOrder,
                          u8"開始済みのペイロードだけを逆順に戻していません");
        CheckPayloadOrder(CallsOf(rig, ECall::Discard), discardOrder, u8"途中解放後の準備済みデータの破棄が違います");
        CheckPayloadOrder(CallsOf(rig, ECall::Commit), commitOrder, u8"解放の要求後にコミットが進みました");
        const StatePair expected[] = {
            {S::Unloaded, S::Preparing}, {S::Preparing, S::ReadyToCommit},
            {S::ReadyToCommit, S::Committing}, {S::Committing, S::Unloading},
            {S::Unloading, S::Unloaded}
        };
        CheckTransitions(rig, expected, u8"コミット途中の解放の遷移が違います");
    }

    void TestPrepareRetries()
    {
        using S = EStreamingCellState;
        for (uint32_t failures : {3u, 2u})
        {
            TestRig rig;
            rig.Alpha.Rules.push_back({11, failures, EStreamingPrepareResult::Success});
            if (!rig.Load(MakeManifest()))
            {
                return;
            }
            rig.Focus(0.0f);
            rig.System.Update();
            for (uint32_t attempt = 0; attempt < 3; ++attempt)
            {
                Check(rig.System.GetCellState(11) == S::Preparing, u8"再試行で準備中に入りません");
                Check(rig.Executor.RunOne(), u8"再試行の仕事がありません");
                rig.System.Update();
                if (attempt < failures)
                {
                    if (attempt == 2)
                    {
                        Check(rig.System.GetCellState(11) == S::Failed, u8"上限回数の失敗後に諦めません");
                        break;
                    }
                    Check(rig.System.GetCellState(11) == S::Unloaded, u8"準備の失敗後に待機状態へ戻りません");
                    rig.Clock.TimeMs += 9.0;
                    rig.System.Update();
                    Check(rig.System.GetCellState(11) == S::Unloaded && rig.Executor.PendingCount() == 0 &&
                          CountCalls(rig, ECall::Prepare, 11) == attempt + 1,
                          u8"再試行の待ち時間より前に準備を始めました");
                    rig.Clock.TimeMs += 1.0;
                    rig.System.Update();
                }
                else
                {
                    Check(rig.System.GetCellState(11) == S::Committing, u8"再試行成功後にコミットを始めません");
                    rig.System.Update();
                    Check(rig.System.GetCellState(11) == S::Active, u8"再試行成功後に Active へ進みません");
                }
            }
            Check(CountCalls(rig, ECall::Prepare, 11) == 3, u8"準備の試行回数が違います");
            Check(CountCalls(rig, ECall::Discard, 11) == failures, u8"失敗した準備の破棄回数が違います");
            if (failures == 3)
            {
                rig.Clock.TimeMs += 100.0;
                for (uint32_t i = 0; i < 3; ++i)
                {
                    rig.System.Update();
                }
                Check(rig.System.GetCellState(11) == S::Failed && rig.Executor.PendingCount() == 0 &&
                      CountCalls(rig, ECall::Prepare, 11) == 3,
                      u8"諦めたセルが同じ位置で再び準備されました");
                const StatePair expected[] = {
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded},
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded},
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::Failed}
                };
                CheckTransitions(rig, expected, u8"再試行を諦めるまでの遷移が違います");
            }
            else
            {
                const StatePair expected[] = {
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded},
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded},
                    {S::Unloaded, S::Preparing}, {S::Preparing, S::ReadyToCommit},
                    {S::ReadyToCommit, S::Committing}, {S::Committing, S::Active}
                };
                CheckTransitions(rig, expected, u8"再試行から成功までの遷移が違います");
            }
        }
    }

    void TestRejectedSubmit()
    {
        using S = EStreamingCellState;
        TestRig rig;
        if (!rig.Load(MakeManifest()))
        {
            return;
        }
        rig.Focus(0.0f);
        rig.Executor.bAccept = false;
        rig.System.Update();
        Check(rig.Executor.SubmitCount == 1 && rig.Executor.PendingCount() == 0 &&
              rig.System.GetCellState(11) == S::Unloaded,
              u8"仕事の拒否後に準備中から戻りません");
        Check(CallsOf(rig, ECall::Prepare).empty(), u8"拒否された仕事の準備が実行されました");
        rig.Executor.bAccept = true;
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Preparing && rig.Executor.PendingCount() == 1,
              u8"受付再開後の次のフレームに準備されません");
        Check(rig.Executor.RunOne(), u8"受付再開後の仕事がありません");
        rig.System.Update();
        rig.System.Update();
        Check(rig.System.GetCellState(11) == S::Active, u8"受付再開後に Active へ進みません");
        const StatePair expected[] = {
            {S::Unloaded, S::Preparing}, {S::Preparing, S::Unloaded},
            {S::Unloaded, S::Preparing}, {S::Preparing, S::ReadyToCommit},
            {S::ReadyToCommit, S::Committing}, {S::Committing, S::Active}
        };
        CheckTransitions(rig, expected, u8"仕事の拒否と再開の遷移が違います");
    }

    void TestDeterminism()
    {
        using S = EStreamingCellState;
        const Container::String manifest = R"({
            "format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":1,
            "cells":[
                {"id":10,"bounds":{"min":[0,-1,-1],"max":[1,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/0"}]},
                {"id":11,"bounds":{"min":[6,-1,-1],"max":[7,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/1"}]},
                {"id":12,"bounds":{"min":[12,-1,-1],"max":[13,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/2"}]},
                {"id":13,"bounds":{"min":[18,-1,-1],"max":[19,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/3"}]},
                {"id":14,"bounds":{"min":[24,-1,-1],"max":[25,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/4"}]},
                {"id":15,"bounds":{"min":[30,-1,-1],"max":[31,1,1]},"payloads":[{"kind":"alpha","logicalPath":"cells/5"}]}
            ]})";
        const auto run = [&manifest]()
        {
            Container::VariableArray<StreamingTransition> result;
            TestRig rig;
            auto settings = MakeSettings();
            settings.MaxInFlightPrepares = 2;
            settings.Retry.RetryDelayMs = 2.0;
            rig.Alpha.Rules.push_back({13, 1, EStreamingPrepareResult::Success});
            if (!rig.Load(manifest, settings))
            {
                return result;
            }
            const float positions[] = {0.0f, 12.0f, 24.0f, 0.0f, 100.0f};
            for (float position : positions)
            {
                rig.Focus(position);
                for (uint32_t frame = 0; frame < 12; ++frame)
                {
                    rig.System.Update();
                    rig.Executor.RunAll();
                    rig.Clock.TimeMs += 1.0;
                }
            }
            uint32_t prepareCount = 0;
            bool bSawFailure = false;
            bool bSawSuccessAfterFailure = false;
            for (const auto& call : rig.Calls)
            {
                if (call.Kind == ECall::Prepare && call.Cell == 13)
                {
                    ++prepareCount;
                    if (call.PrepareResult == EStreamingPrepareResult::Failed)
                    {
                        bSawFailure = true;
                    }
                    else if (bSawFailure && call.PrepareResult == EStreamingPrepareResult::Success)
                    {
                        bSawSuccessAfterFailure = true;
                    }
                }
            }
            Check(prepareCount >= 2 && bSawFailure && bSawSuccessAfterFailure,
                  u8"決定性の試験で注入した失敗から再試行に成功しません");
            bool seen[6] = {};
            bool bUnloaded = false;
            for (const auto& transition : rig.System.GetTransitions())
            {
                result.push_back(transition);
                if (transition.Cell >= 10 && transition.Cell <= 15)
                {
                    seen[transition.Cell - 10] = true;
                }
                bUnloaded = bUnloaded || transition.To == S::Unloading;
            }
            for (bool bSeen : seen)
            {
                Check(bSeen, u8"決定性の試験で六つのセルすべてが遷移しません");
            }
            Check(bUnloaded, u8"決定性の試験で焦点移動による解放が起きません");
            return result;
        };
        const auto first = run();
        const auto second = run();
        if (!Check(!first.empty() && first.size() == second.size(), u8"二回の遷移の件数が一致しません"))
        {
            return;
        }
        for (size_t i = 0; i < first.size(); ++i)
        {
            const auto& a = first[i];
            const auto& b = second[i];
            if (a.Frame != b.Frame || a.Cell != b.Cell || a.From != b.From || a.To != b.To)
            {
                ++g_Failures;
                std::cerr << Utf8(u8"検査失敗: 二回の遷移が一致しません。順番=") << i << "\n";
            }
        }
    }

    void TestManifestRejections()
    {
        const char* kinds[] = {"alpha", "beta"};
        CellManifest manifest;
        CellManifestError error;
        Check(CellManifest::TryParse(MakeManifest(), {kinds, 2}, manifest, &error) &&
              error.Code == ECellManifestError::None && manifest.GetCells().size() == 1,
              u8"正しいマニフェストを受け付けません");
        struct ManifestCase
        {
            Container::String Text;
            ECellManifestError Expected;
            const char8_t* Message;
        };
        const ManifestCase cases[] = {
            {MakeManifest("unknown"), ECellManifestError::UnknownPayloadKind, u8"未知の種別を拒否しません"},
            {MakeManifest("alpha", "cells/item", "1", "-1", 1, true),
             ECellManifestError::DuplicateCellId, u8"重複したセル ID を拒否しません"},
            {MakeManifest("alpha", "cells/item", "1", "1e39"),
             ECellManifestError::NonFiniteValue, u8"有限の float を超える境界を拒否しません"},
            {MakeManifest("alpha", "cells/item", "0"), ECellManifestError::NonPositiveCellSize,
             u8"セルの大きさゼロを拒否しません"},
            {MakeManifest("alpha", "cells/item", "-1"), ECellManifestError::NonPositiveCellSize,
             u8"負のセルの大きさを拒否しません"},
            {MakeManifest("alpha", "cells/../item"), ECellManifestError::ParentDirectoryInPath,
             u8"親ディレクトリを含む論理パスを拒否しません"},
            {MakeManifest("alpha", "/cells/item"), ECellManifestError::AbsolutePath,
             u8"絶対論理パスを拒否しません"},
            {MakeManifest("alpha", "cells/item", "1", "-1", 1, false, "other-format"),
             ECellManifestError::UnsupportedFormat, u8"未対応の形式を拒否しません"},
            {R"({"format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":1})",
             ECellManifestError::MissingField, u8"必須項目の欠けを拒否しません"},
            {R"({"format":"norves.cell-manifest.v1","origin":[0,0],"cellSize":1,"cells":[]})",
             ECellManifestError::InvalidFieldType, u8"不正な項目の型を拒否しません"},
            {R"({"format":)", ECellManifestError::ParseError, u8"壊れた JSON を拒否しません"}
        };
        for (const auto& test : cases)
        {
            const bool bParsed = CellManifest::TryParse(test.Text, {kinds, 2}, manifest, &error);
            if (bParsed || error.Code != test.Expected || !manifest.GetCells().empty())
            {
                ++g_Failures;
                std::cerr << Utf8(u8"検査失敗: ") << Utf8(test.Message)
                          << Utf8(u8" 期待=") << ToString(test.Expected)
                          << Utf8(u8" 実際=") << ToString(error.Code) << "\n";
            }
        }
    }
} // namespace

int main()
{
    TestCompleteTransitions();
    TestCancelPreparing();
    TestUnloadDuringCommit();
    TestPrepareRetries();
    TestRejectedSubmit();
    TestDeterminism();
    TestManifestRejections();
    if (g_Failures != 0)
    {
        std::cerr << Utf8(u8"失敗数: ") << g_Failures << "\n";
    }
    return g_Failures == 0 ? 0 : 1;
}
