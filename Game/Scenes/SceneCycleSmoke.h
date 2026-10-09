#pragma once

// ---------------------------------------------------------------------------
// シーンの出入りの周回の検査（--scene-cycle-smoke=<id,...> / --scene-cycle-count=<n>）
//
// 何も置かないホームのモードと指定のシーンの間を RequestChange で周回し、周回ごとに資源の数
// （材質・テクスチャ・メッシュ・Model・Entity）と GPU の割り当て量をログへ出す。シーンを出た後に
// 資源が返っていれば、2周目以降の値は1周目と同じになる。増えた（変わった）種類があれば失敗にする。
//   SCENE_CYCLE_SAMPLE ... 周回の1回分の値（シーンを出て、遅延解放が済むのを待った後）
//   SCENE_CYCLE_DIFF   ... 1周目と違った値
//   SCENE_CYCLE_SMOKE result=pass|fail ... 結果。終了コードは pass で 0、fail で 1
// 値が落ち着くのを待ってから測る（読み込み・RenderThread の遅延解放が終わるまで同じ値が続くこと）。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/Containers.h"
#include "Core/Public/GameMode/GameModeParams.h"
#include "Scenes/SceneCatalog.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::GameMode
{
    class IGameModeController;
}

namespace Game::Scenes
{

    class SceneCycleSmoke
    {
    public:
        /// 周回数の既定
        static constexpr uint32_t kDefaultCycleCount = 3;

        /// カンマ区切りのシーンの id を読み、検査を有効にする。空・未知の id・重複は false（理由はログ）
        bool ParseScenes(const char *sceneList);
        /// 周回数（1 以上）。2周目以降を1周目と比べるので、比べるには 2 以上が要る
        void SetCycleCount(uint32_t cycleCount);
        /// シーンへ入るときに渡すパラメータ
        void SetModeParams(const NorvesLib::Core::GameMode::GameModeParams &params) { m_ModeParams = params; }

        bool IsEnabled() const { return m_bEnabled; }
        uint32_t GetCycleCount() const { return m_CycleCount; }

        /// GameThread から毎フレーム呼ぶ。全周が終わると結果をログへ出して Engine::RequestExit を呼ぶ。
        void Update(NorvesLib::Core::Engine::Engine &engine, NorvesLib::Core::GameMode::IGameModeController &controller);

        /// 測る値の数
        static constexpr std::size_t kFieldCount = 12;

        /// 測った値の1組
        struct Snapshot
        {
            uint64_t Values[kFieldCount] = {};
            bool operator==(const Snapshot &other) const;
        };

    private:
        enum class Phase
        {
            WaitHome,    ///< 起動直後のホームが落ち着くのを待つ
            DwellScene,  ///< シーンへ入り、組み立てと読み込みが落ち着くのを待つ
            SettleHome,  ///< ホームへ戻り、解放と遅延解放が済むのを待つ
            Done,
        };

        struct Sample
        {
            uint32_t Cycle = 0;
            uint32_t SceneIndex = 0;
            bool bSettled = false;
            Snapshot Values;
        };

        void BeginVisit();
        void EnterPhase(Phase phase);
        void RecordSample(bool bSettled);
        void Finish(NorvesLib::Core::Engine::Engine &engine);

        bool m_bEnabled = false;
        uint32_t m_CycleCount = kDefaultCycleCount;
        NorvesLib::Core::Container::VariableArray<const SceneDescriptor *> m_Scenes;
        NorvesLib::Core::GameMode::GameModeParams m_ModeParams;
        NorvesLib::Core::GameMode::IGameModeController *m_pController = nullptr;

        Phase m_Phase = Phase::WaitHome;
        /// 段階に入ってから描画されたフレーム数（GameThread の更新回数ではない。RenderThread が止まっている間は進めない）
        uint32_t m_PhaseFrames = 0;
        /// 値が変わらず、読み込みと遅延解放が無かった描画フレームの連続数
        uint32_t m_StableFrames = 0;
        uint64_t m_LastRenderedFrame = 0;
        std::chrono::steady_clock::time_point m_PhaseStart;
        Snapshot m_Last;
        bool m_bHasLast = false;
        uint32_t m_Cycle = 0;
        uint32_t m_SceneIndex = 0;
        NorvesLib::Core::Container::VariableArray<Sample> m_Samples;
    };

} // namespace Game::Scenes
