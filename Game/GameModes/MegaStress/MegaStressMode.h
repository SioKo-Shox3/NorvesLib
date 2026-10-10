#pragma once

// ---------------------------------------------------------------------------
// mega-stress（仮想化ジオメトリの負荷シーン。開発ビルドだけランチャーに出る）
//
// メッシュの一覧（Assets/AssetSets/MegaStressMeshes.json）に書いた、多種のクック済みメッシュを、
// 1 つのメッシュ = 1 つの MegaMesh（Model）として読み、個別の Entity を --stress-entities=<n>（既定 2000）個置く。
// 同じメッシュの Entity は 1 つの Model を共有し、メッシュの種類・向きは番号から決める（実行ごとに同じ）。
// 一覧に書いた資産のうちクック済みが無いものは、警告して読み飛ばす（読めた種類だけで続ける）。
// 置いた数・読めたメッシュ（資源）の数・区間（材質とプールの塊が同じ Entity の集まり。メッシュごとに 1 つ）の数は
// MEGA_STRESS_PLACED でログに出る。
// 視点は SceneCatalog.h の kMegaStressViews（overview・close・aerial）。
// --stress-moving-percent=<p> で、その割合（%）の Entity を毎フレーム動かす（置いた位置の周りを円運動し、ヨーで回る。
// 動きは Entity の番号と経過時間だけで決まる）。--stress-moving-time=<秒> で動きの時刻を底上げできる。
// --stress-stream-cell=<m> で、一辺 m[m] の区画ごとに Entity を読み込み・解放する。--stress-stream-period=<秒> ごとに、
// カメラに最も近い区画を 1 つ解放し、解放中の区画が上限（kStreamWindowCells）に達したら、解放した順に 1 つずつ読み込み直す
// （解放した区画が全部戻ったところで MEGA_STRESS_STREAM_CYCLE に Entity の数の一致を出す）。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace Game::GameModes
{

    /// 読み込みの途中経過（MegaStressRoutine.cpp が定義する）
    struct StressAssembly;
    /// 毎フレーム動かす Entity の一覧（MegaStressRoutine.cpp が定義する）
    struct StressMovers;

    /// 区画の読み込み・解放の状態（MegaStressRoutine.cpp が定義する）
    struct StressStream;

    /// --stress-entities の既定値と上限
    inline constexpr uint32_t kDefaultStressEntityCount = 2000;
    inline constexpr uint32_t kMaxStressEntityCount = 200000;

    /// --stress-stream-cell（区画の一辺[m]）の下限・上限、--stress-stream-period（周期[秒]）の既定・下限
    inline constexpr float kMinStressStreamCellMeters = 4.0f;
    inline constexpr float kMaxStressStreamCellMeters = 2000.0f;
    inline constexpr float kDefaultStressStreamPeriod = 1.0f;
    inline constexpr float kMinStressStreamPeriod = 0.01f;
    /// cycle smoke の中で、区画の周期を回す回数（超えたら止めて、基準の状態で落ち着かせる）
    inline constexpr uint32_t kSmokeStreamCycles = 3;
    /// 同時に解放している区画の数の上限（解放がここに達したら、解放した順に読み込み直す）
    inline constexpr uint32_t kStreamWindowCells = 4;

    /// 非同期に読む材質のテクスチャ。読み込みの結果は主スレッドで届く（シーンを出たら bCancelled を立てて捨てる）
    struct StressPendingMaterial
    {
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
        /// 材質ごとに作った仮想テクスチャ（シーンの範囲で追跡して、モデルと一緒に返す）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::TextureHandle> VirtualTextures;
    };

    struct MegaStressData
    {
        /// 起動時の視点の名前（--scene-view。空ならシーンの既定の視点）
        NorvesLib::Core::Container::String m_ViewName;
        /// 入力の台本のパス（--camera-script。空なら流さない）
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// 起動時にカメラパスの途中へ置く時刻[秒]（--camera-path-time）。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// 置く Entity の数（--stress-entities）
        uint32_t m_EntityCount = kDefaultStressEntityCount;
        /// 毎フレーム動かす Entity の割合[%]（--stress-moving-percent。0 なら動かさない）
        float m_MovingPercent = 0.0f;
        /// 動きの時刻の底上げ[秒]（--stress-moving-time）
        float m_MovingStartTime = 0.0f;
        /// 区画の一辺[m]（--stress-stream-cell。0 なら区画の読み込み・解放をしない）
        float m_StreamCellMeters = 0.0f;
        /// 区画を 1 つ出入りさせる周期[秒]（--stress-stream-period）
        float m_StreamPeriod = kDefaultStressStreamPeriod;
        /// 区画の周期を回す回数の上限（0 なら止めない。cycle smoke の中だけ kSmokeStreamCycles）
        uint32_t m_StreamMaxCycles = 0;
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu *m_pPauseMenu = nullptr;
        /// クック済みの資産を引く AssetSystem の窓口
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;

        // ---- 組み立ての状態（Routine が持つ。Leave で空に戻す） ----
        NorvesLib::Core::Container::TSharedPtr<StressAssembly> m_pAssembly;
        /// 動く Entity（置いた後に作る。Entity の実体はシーンの範囲が持つので、Leave で手放す）
        NorvesLib::Core::Container::TSharedPtr<StressMovers> m_pMovers;
        /// 区画の読み込み・解放の状態（--stress-stream-cell を指定したときだけ、置いた後に作る）
        NorvesLib::Core::Container::TSharedPtr<StressStream> m_pStream;
        /// 動きの経過時間[秒]（Entity を置いた後に deltaTime を積む）
        float m_MovingElapsed = 0.0f;
        bool m_bEntitiesPlaced = false;
    };

    class MegaStressRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             MegaStressData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, MegaStressData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, MegaStressData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "MegaStress";
    };

    using MegaStressMode = NorvesLib::Core::GameMode::TGameMode<MegaStressRoutine, MegaStressData>;

} // namespace Game::GameModes
