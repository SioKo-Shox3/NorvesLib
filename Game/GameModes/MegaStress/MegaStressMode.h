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
// 後の項目（動く Entity・区画の読み込み・解放）は、この Entity の置き方を土台に足す。
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

    /// --stress-entities の既定値と上限
    inline constexpr uint32_t kDefaultStressEntityCount = 2000;
    inline constexpr uint32_t kMaxStressEntityCount = 200000;

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
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu *m_pPauseMenu = nullptr;
        /// クック済みの資産を引く AssetSystem の窓口
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;

        // ---- 組み立ての状態（Routine が持つ。Leave で空に戻す） ----
        NorvesLib::Core::Container::TSharedPtr<StressAssembly> m_pAssembly;
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
