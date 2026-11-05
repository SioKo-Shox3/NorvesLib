// ストリーミングの予算と輪の境界を確かめる。
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
            Jobs.push_back(std::move(job));
            return true;
        }

        void RunAll()
        {
            while (!Jobs.empty())
            {
                auto job = std::move(Jobs.front());
                Jobs.erase(Jobs.begin());
                job();
            }
        }

    private:
        Container::VariableArray<Thread::TaskFunction> Jobs;
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

        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor,
                                        uint64_t& outCommittedBytes) override
        {
            CommitCells.push_back(context.Cell);
            m_Clock.TimeMs += CommitStepMs;
            outCommittedBytes = CommitStepBytes;
            ++cursor.Step;
            return cursor.Step >= CommitSteps ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        void SetDormant(const StreamingPayloadContext&, bool) override
        {
        }

        EStreamingStepResult UnloadStep(const StreamingPayloadContext&, StreamingStepCursor& cursor) override
        {
            ++cursor.Step;
            return cursor.Step >= UnloadSteps ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        void DiscardPrepared(const StreamingPayloadContext&) override
        {
        }

        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext&) const override
        {
            return ResidentBytes;
        }

        double CommitStepMs = 0.0;
        uint64_t CommitStepBytes = 16;
        uint32_t CommitSteps = 1;
        uint32_t UnloadSteps = 1;
        StreamingPayloadBytes ResidentBytes{64, 0};
        Container::VariableArray<StreamingCellId> CommitCells;

    private:
        FakeClock& m_Clock;
    };

    Container::String MakeManifest(uint32_t cellCount, bool bTwoPayloads = false)
    {
        const char* cells[] = {
            R"({"id":11,"bounds":{"min":[0,-1,-1],"max":[1,1,1]},"payloads":[)",
            R"({"id":12,"bounds":{"min":[2,-1,-1],"max":[3,1,1]},"payloads":[)",
            R"({"id":13,"bounds":{"min":[4,-1,-1],"max":[5,1,1]},"payloads":[)",
            R"({"id":14,"bounds":{"min":[6,-1,-1],"max":[7,1,1]},"payloads":[)",
            R"({"id":15,"bounds":{"min":[8,-1,-1],"max":[9,1,1]},"payloads":[)"
        };
        Container::String text = R"({"format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":1,"cells":[)";
        for (uint32_t i = 0; i < cellCount; ++i)
        {
            if (i > 0)
            {
                text += ",";
            }
            text += cells[i];
            text += R"({"kind":"alpha","logicalPath":"cells/first"})";
            if (bTwoPayloads)
            {
                text += R"(,{"kind":"alpha","logicalPath":"cells/second"})";
            }
            text += "]}";
        }
        text += "]}";
        return text;
    }

    StreamingSystemSettings MakeSettings()
    {
        StreamingSystemSettings settings;
        settings.Ring.ActiveRadius = 10.0f;
        settings.Ring.DormantRadius = 25.0f;
        settings.Ring.UnloadRadius = 40.0f;
        settings.Ring.DirectionWeight = 0.0f;
        settings.Ring.LatencySeconds = 0.0f;
        return settings;
    }

    struct TestRig
    {
        FakeClock Clock;
        ManualExecutor Executor;
        FakeHandler Handler{Clock};
        StreamingSystem System;

        ~TestRig()
        {
            // 完了待ちへ入る前に、手動実行器の仕事を必ず回す。
            Executor.RunAll();
            System.Shutdown();
        }

        bool Load(const Container::String& text, const StreamingSystemSettings& settings)
        {
            System.SetEnvironment(&Clock, &Executor);
            if (!Check(System.SetSettings(settings), u8"輪の設定を受け付けません") ||
                !Check(System.RegisterHandler(&Handler), u8"ハンドラを登録できません"))
            {
                return false;
            }
            System.SetRecordTransitions(true);
            CellManifestError error;
            if (!System.LoadManifestFromText(text, &error))
            {
                ++g_Failures;
                std::cerr << Utf8(u8"検査失敗: マニフェストを読み込めません: ") << ToString(error.Code) << "\n";
                return false;
            }
            return true;
        }

        void Focus(float x, float velocity = 0.0f)
        {
            System.SetFocus({Math::Vector3(x, 0.0f, 0.0f), Math::Vector3(velocity, 0.0f, 0.0f)});
        }

        void Advance()
        {
            System.Update();
            Executor.RunAll();
        }
    };

    bool CheckState(const TestRig& rig, StreamingCellId cell, EStreamingCellState expected, const char8_t* message)
    {
        const auto actual = rig.System.GetCellState(cell);
        if (!Check(actual == expected, message))
        {
            std::cerr << Utf8(u8"  セル=") << cell << Utf8(u8" フレーム=") << rig.System.GetLastFrameStats().Frame
                      << Utf8(u8" 実際=") << ToString(actual) << Utf8(u8" 期待=") << ToString(expected) << "\n";
            return false;
        }
        return true;
    }

    uint32_t CountTransitions(const TestRig& rig, StreamingCellId cell, EStreamingCellState to)
    {
        uint32_t count = 0;
        for (const auto& transition : rig.System.GetTransitions())
        {
            if (transition.Cell == cell && transition.To == to)
            {
                ++count;
            }
        }
        return count;
    }

    uint32_t CountTransitions(const TestRig& rig, StreamingCellId cell,
                              EStreamingCellState from, EStreamingCellState to)
    {
        uint32_t count = 0;
        for (const auto& transition : rig.System.GetTransitions())
        {
            if (transition.Cell == cell && transition.From == from && transition.To == to)
            {
                ++count;
            }
        }
        return count;
    }

    bool CommittedSince(const TestRig& rig, StreamingCellId cell, size_t start)
    {
        for (size_t i = start; i < rig.Handler.CommitCells.size(); ++i)
        {
            if (rig.Handler.CommitCells[i] == cell)
            {
                return true;
            }
        }
        return false;
    }

    bool AllActive(const TestRig& rig, uint32_t cellCount)
    {
        for (uint32_t i = 0; i < cellCount; ++i)
        {
            if (rig.System.GetCellState(11 + i) != EStreamingCellState::Active)
            {
                return false;
            }
        }
        return true;
    }

    void TestTimeBudget()
    {
        using S = EStreamingCellState;
        auto settings = MakeSettings();
        settings.Budget.PerFrameCommitMs = 2.0;

        {
            TestRig rig;
            rig.Handler.CommitSteps = 4;
            rig.Handler.CommitStepMs = 0.7;
            if (!rig.Load(MakeManifest(3, true), settings))
            {
                return;
            }
            rig.Focus(0.5f);
            bool bWithinBudget = true;
            bool bSawCommitting = false;
            bool bContinued = false;
            bool bEveryCarryContinued = true;
            StreamingCellId pendingCell = kInvalidStreamingCellId;
            uint32_t totalSteps = 0;
            for (uint32_t frame = 0; frame < 64; ++frame)
            {
                const double timeBefore = rig.Clock.TimeMs;
                const size_t commitsBefore = rig.Handler.CommitCells.size();
                rig.Advance();
                const auto& stats = rig.System.GetLastFrameStats();
                const double elapsed = rig.Clock.TimeMs - timeBefore;
                bWithinBudget = bWithinBudget && stats.StepMs <= settings.Budget.PerFrameCommitMs + 0.7 + 1e-8 &&
                    elapsed <= settings.Budget.PerFrameCommitMs + 0.7 + 1e-8 &&
                    stats.StepMs >= elapsed - 1e-8 && stats.StepMs <= elapsed + 1e-8;
                if (pendingCell != kInvalidStreamingCellId)
                {
                    const bool bThisCarryContinued = CommittedSince(rig, pendingCell, commitsBefore);
                    bContinued = bContinued || bThisCarryContinued;
                    bEveryCarryContinued = bEveryCarryContinued && bThisCarryContinued;
                }
                totalSteps += stats.CommitSteps;
                pendingCell = kInvalidStreamingCellId;
                for (StreamingCellId cell = 11; cell <= 13; ++cell)
                {
                    if (rig.System.GetCellState(cell) == S::Committing)
                    {
                        bSawCommitting = true;
                        if (pendingCell == kInvalidStreamingCellId)
                        {
                            pendingCell = cell;
                        }
                    }
                }
                if (AllActive(rig, 3))
                {
                    break;
                }
            }
            Check(bWithinBudget, u8"時間予算と一歩分を超えるコミットがありました");
            Check(bSawCommitting && bContinued && bEveryCarryContinued,
                  u8"時間予算で残った同じセルのコミットが次のフレームへ続きません");
            Check(AllActive(rig, 3) && totalSteps == 24, u8"複数セルとペイロードのコミットが最後まで進みません");
        }

        {
            TestRig rig;
            rig.Handler.CommitSteps = 4;
            rig.Handler.CommitStepMs = 5.0;
            if (!rig.Load(MakeManifest(1), settings))
            {
                return;
            }
            rig.Focus(0.5f);
            bool bOneStepPerFrame = true;
            uint32_t totalSteps = 0;
            for (uint32_t frame = 0; frame < 16; ++frame)
            {
                rig.Advance();
                const auto& stats = rig.System.GetLastFrameStats();
                if (frame > 0)
                {
                    bOneStepPerFrame = bOneStepPerFrame && stats.CommitSteps == 1 &&
                        stats.StepMs >= 5.0 - 1e-8 && stats.StepMs <= 5.0 + 1e-8;
                }
                totalSteps += stats.CommitSteps;
                if (AllActive(rig, 1))
                {
                    break;
                }
            }
            Check(bOneStepPerFrame, u8"予算より長い歩が一フレームに一歩ずつ進みません");
            Check(AllActive(rig, 1) && totalSteps == 4, u8"予算より長い歩を最後まで完了できません");
        }
    }

    void TestByteBudget()
    {
        using S = EStreamingCellState;
        auto settings = MakeSettings();
        settings.Budget.PerFrameCommitMs = 100.0;
        settings.Budget.PerFrameCommitBytes = 30;
        TestRig rig;
        rig.Handler.CommitSteps = 3;
        rig.Handler.CommitStepMs = 0.1;
        rig.Handler.CommitStepBytes = 13;
        if (!rig.Load(MakeManifest(3, true), settings))
        {
            return;
        }
        rig.Focus(0.5f);
        bool bWithinBudget = true;
        bool bBytesMatchSteps = true;
        bool bSawOvershoot = false;
        bool bSawCommitting = false;
        bool bContinued = false;
        bool bEveryCarryContinued = true;
        StreamingCellId pendingCell = kInvalidStreamingCellId;
        uint32_t totalSteps = 0;
        for (uint32_t frame = 0; frame < 64; ++frame)
        {
            const size_t commitsBefore = rig.Handler.CommitCells.size();
            rig.Advance();
            const auto& stats = rig.System.GetLastFrameStats();
            bWithinBudget = bWithinBudget && stats.CommitBytes <= settings.Budget.PerFrameCommitBytes + 13;
            bBytesMatchSteps = bBytesMatchSteps && stats.CommitBytes == stats.CommitSteps * 13;
            bSawOvershoot = bSawOvershoot || stats.CommitBytes > settings.Budget.PerFrameCommitBytes;
            if (pendingCell != kInvalidStreamingCellId)
            {
                const bool bThisCarryContinued = CommittedSince(rig, pendingCell, commitsBefore);
                bContinued = bContinued || bThisCarryContinued;
                bEveryCarryContinued = bEveryCarryContinued && bThisCarryContinued;
            }
            totalSteps += stats.CommitSteps;
            pendingCell = kInvalidStreamingCellId;
            for (StreamingCellId cell = 11; cell <= 13; ++cell)
            {
                if (rig.System.GetCellState(cell) == S::Committing)
                {
                    bSawCommitting = true;
                    if (pendingCell == kInvalidStreamingCellId)
                    {
                        pendingCell = cell;
                    }
                }
            }
            if (AllActive(rig, 3))
            {
                break;
            }
        }
        Check(bWithinBudget && bBytesMatchSteps, u8"コミットのバイト数が歩数と合わないか予算と一歩分を超えました");
        Check(bSawOvershoot && bSawCommitting && bContinued && bEveryCarryContinued,
              u8"バイト予算で残った同じセルのコミットが次のフレームへ続きません");
        Check(AllActive(rig, 3) && totalSteps == 18, u8"バイト予算の下で全コミットを完了できません");
    }

    void TestResidentCellLimit()
    {
        using S = EStreamingCellState;
        auto settings = MakeSettings();
        settings.Budget.MaxResidentCells = 3;
        TestRig rig;
        if (!rig.Load(MakeManifest(5), settings))
        {
            return;
        }
        rig.Focus(0.5f);
        bool bWithinLimit = true;
        for (uint32_t frame = 0; frame < 12; ++frame)
        {
            rig.Advance();
            bWithinLimit = bWithinLimit && rig.System.GetLastFrameStats().ResidentCells <= 3;
        }
        for (StreamingCellId cell = 11; cell <= 15; ++cell)
        {
            CheckState(rig, cell, cell <= 13 ? S::Active : S::Unloaded,
                       u8"常駐数の上限で焦点に近い三セルが残りません");
        }

        rig.System.ClearTransitions();
        rig.Focus(8.5f);
        uint32_t evictions = 0;
        for (uint32_t frame = 0; frame < 16; ++frame)
        {
            rig.Advance();
            const auto& stats = rig.System.GetLastFrameStats();
            bWithinLimit = bWithinLimit && stats.ResidentCells <= 3;
            evictions += stats.Evictions;
        }
        StreamingCellId firstEvicted = kInvalidStreamingCellId;
        for (const auto& transition : rig.System.GetTransitions())
        {
            if (transition.To == S::Unloading)
            {
                firstEvicted = transition.Cell;
                break;
            }
        }
        Check(evictions > 0 && firstEvicted == 11, u8"焦点の移動後に最も遠い常駐セルから追い出しません");
        Check(CountTransitions(rig, 11, S::Preparing) == 0 && CountTransitions(rig, 12, S::Preparing) == 0,
              u8"焦点の移動で追い出したセルを直後に再び準備しました");
        for (StreamingCellId cell = 11; cell <= 15; ++cell)
        {
            CheckState(rig, cell, cell >= 13 ? S::Active : S::Unloaded,
                       u8"焦点の移動後に近い三セルが常駐しません");
        }

        rig.System.ClearTransitions();
        uint32_t repeatEvictions = 0;
        for (uint32_t frame = 0; frame < 40; ++frame)
        {
            rig.Advance();
            const auto& stats = rig.System.GetLastFrameStats();
            bWithinLimit = bWithinLimit && stats.ResidentCells <= 3;
            repeatEvictions += stats.Evictions;
        }
        Check(bWithinLimit, u8"常駐セル数が上限の三を超えました");
        Check(repeatEvictions == 0, u8"焦点を固定しても常駐セルの追い出しが続きます");
        for (StreamingCellId cell = 11; cell <= 15; ++cell)
        {
            Check(CountTransitions(rig, cell, S::Preparing) == 0,
                  u8"常駐数の上限で追い出されたセルが再び準備され続けます");
        }
    }

    void TestResidentByteLimit()
    {
        using S = EStreamingCellState;
        auto settings = MakeSettings();
        settings.Budget.MaxCpuBytes = 128;

        {
            TestRig rig;
            if (!rig.Load(MakeManifest(3), settings))
            {
                return;
            }
            rig.Focus(0.5f);
            uint32_t evictions = 0;
            for (uint32_t frame = 0; frame < 16; ++frame)
            {
                rig.Advance();
                evictions += rig.System.GetLastFrameStats().Evictions;
            }
            Check(evictions > 0, u8"初回読み込みで常駐バイト数を超えても追い出しません");
            CheckState(rig, 11, S::Active, u8"常駐バイト数の上限で最も近いセルが残りません");
            CheckState(rig, 12, S::Active, u8"常駐バイト数の上限で二番目に近いセルが残りません");
            CheckState(rig, 13, S::Unloaded, u8"常駐バイト数の上限で最も遠いセルを解放しません");
            Check(rig.System.GetLastFrameStats().ResidentCpuBytes <= 128,
                  u8"初回の追い出し後も常駐 CPU バイト数が上限を超えます");
            Check(CountTransitions(rig, 13, S::Preparing) == 1,
                  u8"初回に追い出したセルを直後に再び準備しました");
            rig.System.ClearTransitions();
            uint32_t repeatEvictions = 0;
            for (uint32_t frame = 0; frame < 40; ++frame)
            {
                rig.Advance();
                repeatEvictions += rig.System.GetLastFrameStats().Evictions;
            }
            Check(repeatEvictions == 0, u8"常駐バイト数の上限で追い出しが続きます");
            for (StreamingCellId cell = 11; cell <= 13; ++cell)
            {
                Check(CountTransitions(rig, cell, S::Preparing) == 0 &&
                      CountTransitions(rig, cell, S::Unloading) == 0,
                      u8"常駐バイト数の上限でセルの準備とアンロードが往復します");
            }
        }

        {
            TestRig rig;
            settings.Budget.MaxCpuBytes = 192;
            if (!rig.Load(MakeManifest(3), settings))
            {
                return;
            }
            rig.Focus(0.5f);
            for (uint32_t frame = 0; frame < 8; ++frame)
            {
                rig.Advance();
            }
            Check(AllActive(rig, 3), u8"上限引き下げ前に三セルが常駐しません");
            settings.Budget.MaxCpuBytes = 128;
            if (!Check(rig.System.SetSettings(settings), u8"常駐バイト数の上限を変更できません"))
            {
                return;
            }
            rig.System.ClearTransitions();
            rig.Advance();
            Check(rig.System.GetLastFrameStats().Evictions > 0 &&
                  CountTransitions(rig, 13, S::Active, S::Unloading) == 1,
                  u8"上限引き下げ時に最も遠い常駐セルからアンロードしません");
            CheckState(rig, 13, S::Unloaded, u8"上限引き下げ後に最も遠いセルを解放しません");
            Check(rig.System.GetLastFrameStats().ResidentCpuBytes <= 128,
                  u8"上限引き下げ後の常駐 CPU バイト数が上限を超えます");
            rig.System.ClearTransitions();
            uint32_t repeatEvictions = 0;
            for (uint32_t frame = 0; frame < 40; ++frame)
            {
                rig.Advance();
                repeatEvictions += rig.System.GetLastFrameStats().Evictions;
            }
            Check(repeatEvictions == 0, u8"上限引き下げ後に追い出しが続きます");
            for (StreamingCellId cell = 11; cell <= 13; ++cell)
            {
                Check(CountTransitions(rig, cell, S::Preparing) == 0 &&
                      CountTransitions(rig, cell, S::Unloading) == 0,
                      u8"上限引き下げ後にセルの準備とアンロードが往復します");
            }
            CheckState(rig, 11, S::Active, u8"上限引き下げ後に最も近いセルが残りません");
            CheckState(rig, 12, S::Active, u8"上限引き下げ後に二番目に近いセルが残りません");
        }
    }

    void TestRingHysteresis()
    {
        using S = EStreamingCellState;
        auto settings = MakeSettings();
        settings.Ring.ActiveRadius = 5.0f;
        settings.Ring.DormantRadius = 10.0f;
        settings.Ring.UnloadRadius = 20.0f;

        {
            TestRig rig;
            rig.Handler.UnloadSteps = 3;
            if (!rig.Load(MakeManifest(1), settings))
            {
                return;
            }
            rig.Focus(0.5f);
            rig.Advance();
            rig.Advance();
            CheckState(rig, 11, S::Active, u8"輪の境界を調べるセルが Active になりません");
            rig.System.ClearTransitions();

            bool bActiveStable = true;
            for (uint32_t frame = 0; frame < 30; ++frame)
            {
                rig.Focus(1.0f + (frame % 2 == 0 ? 4.9f : 5.1f));
                rig.Advance();
                bActiveStable = bActiveStable && rig.System.GetCellState(11) == S::Active;
            }
            Check(bActiveStable && rig.System.GetTransitions().empty(),
                  u8"Ra の内外を往復すると Active が変化します");

            rig.Focus(11.1f);
            rig.Advance();
            CheckState(rig, 11, S::Dormant, u8"Rd の外へ出ても Dormant になりません");
            bool bDormantStable = true;
            for (uint32_t frame = 0; frame < 30; ++frame)
            {
                rig.Focus(1.0f + (frame % 2 == 0 ? 9.9f : 10.1f));
                rig.Advance();
                bDormantStable = bDormantStable && rig.System.GetCellState(11) == S::Dormant;
            }
            Check(bDormantStable && CountTransitions(rig, 11, S::Active, S::Dormant) == 1 &&
                  CountTransitions(rig, 11, S::Dormant, S::Active) == 0,
                  u8"Rd の内外を往復すると Active と Dormant が往復します");
            rig.Focus(6.1f);
            rig.Advance();
            CheckState(rig, 11, S::Dormant, u8"Ra の外側にいる Dormant が復帰しました");
            rig.Focus(5.9f);
            rig.Advance();
            CheckState(rig, 11, S::Active, u8"Ra の内側へ入っても Active に復帰しません");
            Check(CountTransitions(rig, 11, S::Dormant, S::Active) == 1,
                  u8"Ra を越えた復帰が一回ではありません");

            rig.Focus(11.1f);
            rig.Advance();
            CheckState(rig, 11, S::Dormant, u8"Ru の検査前に Dormant へ移れません");
            rig.System.ClearTransitions();
            bool bSawUnloading = false;
            bool bUnloadedStable = true;
            bool bWasUnloaded = false;
            for (uint32_t frame = 0; frame < 40; ++frame)
            {
                rig.Focus(1.0f + (frame % 2 == 0 ? 19.9f : 20.1f));
                rig.Advance();
                const auto state = rig.System.GetCellState(11);
                bSawUnloading = bSawUnloading || state == S::Unloading;
                bUnloadedStable = bUnloadedStable && (!bWasUnloaded || state == S::Unloaded);
                bWasUnloaded = bWasUnloaded || state == S::Unloaded;
            }
            Check(bSawUnloading && bWasUnloaded && bUnloadedStable &&
                  CountTransitions(rig, 11, S::Dormant, S::Unloading) == 1 &&
                  CountTransitions(rig, 11, S::Unloading, S::Unloaded) == 1 &&
                  CountTransitions(rig, 11, S::Preparing) == 0,
                  u8"Ru の内外を往復するとアンロードや再読み込みが繰り返されます");

            rig.System.ClearTransitions();
            rig.Focus(7.1f);
            rig.Advance();
            CheckState(rig, 11, S::Unloaded, u8"読み込み半径の外で準備を始めました");
            rig.Focus(6.9f);
            rig.Advance();
            CheckState(rig, 11, S::Preparing, u8"読み込み半径の内側で準備を始めません");
            for (uint32_t frame = 0; frame < 30; ++frame)
            {
                rig.Focus(1.0f + (frame % 2 == 0 ? 6.1f : 5.9f));
                rig.Advance();
            }
            CheckState(rig, 11, S::Dormant, u8"読み込み半径の内外で常駐を保ちません");
            Check(CountTransitions(rig, 11, S::Preparing) == 1 && CountTransitions(rig, 11, S::Unloading) == 0,
                  u8"読み込み半径の内外で準備とアンロードが往復します");
        }

        {
            TestRig rig;
            settings.Ring.LatencySeconds = 1.0f;
            if (!rig.Load(MakeManifest(1), settings))
            {
                return;
            }
            const StreamingFocus still{Math::Vector3(0.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 0.0f)};
            const StreamingFocus moving{Math::Vector3(0.0f, 0.0f, 0.0f), Math::Vector3(2.0f, 0.0f, 0.0f)};
            Check(ComputeLoadRadius(1.0f, still, settings.Ring) == 6.0f &&
                  ComputeLoadRadius(1.0f, moving, settings.Ring) == 8.0f,
                  u8"速度による先読みが読み込み半径に反映されません");
            rig.Focus(9.1f, 2.0f);
            rig.Advance();
            CheckState(rig, 11, S::Unloaded, u8"先読み半径の外で準備を始めました");
            rig.Focus(8.9f, 2.0f);
            rig.Advance();
            CheckState(rig, 11, S::Preparing, u8"先読み半径の内側で準備を始めません");
            for (uint32_t frame = 0; frame < 40; ++frame)
            {
                rig.Focus(1.0f + (frame % 2 == 0 ? 8.1f : 7.9f), 2.0f);
                rig.Advance();
            }
            CheckState(rig, 11, S::Dormant, u8"先読み半径の内外で常駐を保ちません");
            Check(CountTransitions(rig, 11, S::Preparing) == 1 && CountTransitions(rig, 11, S::Unloading) == 0,
                  u8"先読み半径の内外で読み込みとアンロードが往復します");
        }
    }
}

int main()
{
    TestTimeBudget();
    TestByteBudget();
    TestResidentCellLimit();
    TestResidentByteLimit();
    TestRingHysteresis();
    if (g_Failures != 0)
    {
        std::cerr << Utf8(u8"検査失敗数: ") << g_Failures << "\n";
    }
    return g_Failures == 0 ? 0 : 1;
}
