// 草原のクック済み地形を TerrainComponent で配置し、谷・尾根・低い視点の見え方を確かめる開発用シーン。
// 視点は SceneCatalog.h の kGrasslandViews（near・valley・low）。自由飛行と撮影時の視点の切り替えに対応する。
// 開発ビルドでは H キーで犬大の代役を追う視点へ切り替え、主人公との間の草の間引きを確認する。
// --grassland-streaming では地形の区画・散布・岩の当たりをセル単位で出し入れする。
// --streaming-snap-test=<x>,<z> では組み立て後にスナップロードし、自由飛行カメラを目的地へ一度移す。
// 地形は Assets/TerrainSpecs/Grassland/GrasslandTerrain.json と Scripts/Terrain/GenerateGrasslandHeight.py から
// クックした Assets/Terrain/Grassland/Grassland.nvtrn と Tiles/GrasslandTile_<x>_<z>.generated を使う。
#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Scatter/ScatterSystem.h"
#if NORVES_BUILD_DEVELOPMENT
#include "Core/Public/Streaming/StreamingSystem.h"
#include "Core/Public/Streaming/WorldPayloadHandlers.h"
#endif
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

namespace NorvesLib::Core
{
    class Entity;
}

namespace NorvesLib::Core::Component
{
    class CameraComponent;
    class MeshComponent;
    class SpringArmComponent;
    class TerrainComponent;
}

namespace Game::GameModes
{

#if NORVES_BUILD_DEVELOPMENT
    /// 主人公視点の切り替えの押下だけを集約する入力の受け手
    class GrasslandViewInputCollector : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnKey(const NorvesLib::Core::Input::KeyEvent& event) override;
        bool ConsumeToggleRequest();
        void OnInputReset() override { m_bToggleRequested = false; }
        void OnInputFocusChanged(bool bFocused) override
        {
            if (!bFocused)
            {
                OnInputReset();
            }
        }
        const char* DebugName() const override { return "GrasslandViewInputCollector"; }

    private:
        /// 押しっぱなしの繰り返しを含めない、未処理の切り替え要求
        bool m_bToggleRequested = false;
    };
#endif

    struct GrasslandData
    {
        /// 起動時の視点の名前（--scene-view。空ならシーンの既定の視点）
        NorvesLib::Core::Container::String m_ViewName;
        /// 入力の台本のパス（--camera-script。空なら流さない）
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// 起動時にカメラパスの途中へ置く時刻[秒]（--camera-path-time）。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// 組み立てが済んだところからカメラパスを再生する（--camera-path-play）
        bool m_bCameraPathPlay = false;
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu* m_pPauseMenu = nullptr;
        /// 実行中の視点の切り替えの依頼（非所有。--capture-views の撮影が使う）。無いなら nullptr
        Game::Scenes::SceneViewSwitch* m_pViewSwitch = nullptr;

        Game::Scenes::FreeCameraRig m_Rig;
        /// 地形の配置完了を確かめる非所有の参照。実体はスコープで追跡した Entity が所有する
        NorvesLib::Core::Component::TerrainComponent* m_pTerrain = nullptr;
        /// 散布と、地形の配置を待つ開発用の合成本数。
        NorvesLib::Core::Scatter::ScatterHandle m_Scatter = NorvesLib::Core::Scatter::kInvalidScatterHandle;
        uint32_t m_ScatterStressCount = 0;
        bool m_bScatterPending = false;
        /// 地形の所有者の子として生成したコライダー用 Entity（非所有）
        NorvesLib::Core::Entity* m_pTerrainCollider = nullptr;
        /// 配置完了後のコライダー作成を試したか
        bool m_bTerrainColliderAttempted = false;
        /// 草を押す見えない走行体（非所有）。スコープが寿命を管理する
        NorvesLib::Core::Entity* m_pRunningBody = nullptr;
        /// 入場時の視点から決め、カメラを動かしても変わらない周回路
        NorvesLib::Math::Vector3 m_RunCenter;
        float m_RunAngle = 0.0f;
        uint32_t m_TickCount = 0;
#if NORVES_BUILD_DEVELOPMENT
        /// セル単位の流し込みを有効にする起動引数。
        bool m_bStreaming = false;
        /// 組み立て後にスナップロードと自由飛行カメラの移動を一度行う起動引数。
        bool m_bStreamingSnapTest = false;
        /// 目的地のワールド X・Z 座標[m]。
        float m_StreamingSnapTestX = 0.0f;
        float m_StreamingSnapTestZ = 0.0f;
        /// 要求を一度試したか。受理されなかった場合も再試行しない。
        bool m_bStreamingSnapRequested = false;
        /// PostTick または取り消し時に通知を受けたか。
        bool m_bStreamingSnapReceived = false;
        NorvesLib::Core::Streaming::StreamingSnapResult m_StreamingSnapResult;
        /// 通知結果の処理を済ませたか。Ready の場合は自由飛行カメラの移動も完了している。
        bool m_bStreamingSnapHandled = false;
        /// Ready を受けて自由飛行カメラを目的地へ移したか。
        bool m_bStreamingSnapTeleported = false;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Streaming::TerrainTilePayloadHandler> m_TerrainHandler;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Streaming::ScatterChunkPayloadHandler> m_ScatterHandler;
        NorvesLib::Core::Streaming::CellManifest m_StreamingManifest;
        NorvesLib::Core::Streaming::StreamingSystemSettings m_PreviousStreamingSettings;
        NorvesLib::Math::Vector3 m_PreviousStreamingFocus;
        bool m_bTerrainHandlerRegistered = false;
        bool m_bScatterHandlerRegistered = false;
        bool m_bStreamingSettingsChanged = false;
        bool m_bStreamingManifestLoaded = false;
        bool m_bPreviousStreamingFocus = false;
        /// H キーの押下を集約する、主人公視点専用の入力の受け手
        GrasslandViewInputCollector m_ViewInput;
        /// 入力ルーターへの登録を Leave で返すための印
        bool m_bViewInputRegistered = false;
        /// 主人公視点を描画側へ渡しているか（既定は自由飛行）
        bool m_bHeroView = false;
        /// 生成に失敗したとき、同じ滞在中の資源の再生成を防ぐ印
        bool m_bHeroCreationAttempted = false;
        /// 犬大の代役とアームの支点（非所有。スコープが寿命を持つ）
        NorvesLib::Core::Entity* m_pHero = nullptr;
        /// 自由飛行へ戻るときに代役を隠すための参照（非所有）
        NorvesLib::Core::Component::MeshComponent* m_pHeroMesh = nullptr;
        /// 主人公を追うカメラ（非所有。所有する Entity はスコープが追跡する）
        NorvesLib::Core::Component::CameraComponent* m_pHeroCamera = nullptr;
        /// カメラの公開直前に姿勢を更新するアーム（非所有）
        NorvesLib::Core::Component::SpringArmComponent* m_pHeroSpringArm = nullptr;
#endif
    };

    class GrasslandRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                                          GrasslandData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, GrasslandData& data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, GrasslandData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "Grassland";
    };

    using GrasslandMode = NorvesLib::Core::GameMode::TGameMode<GrasslandRoutine, GrasslandData>;

} // namespace Game::GameModes
