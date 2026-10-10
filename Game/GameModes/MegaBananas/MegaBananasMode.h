#pragma once

// ---------------------------------------------------------------------------
// mega-bananas（仮想化ジオメトリのバナナのシーン）
//
// 昼の物理空と太陽の影の下、ComfyUI で生成した高ポリのバナナ（約 10 万三角形。Assets/Models/Generated/banana）を
// MegaGeometry で床いっぱいに敷き詰める。同じバナナの全インスタンスは 1 つの Model（MegaMesh）を共有し、
// 向きと大きさと位置をばらす（位置はポアソン円盤で、升目の並びは出ない）。手前には中央が盛り上がった塚を 3 つ積む
// （物理は使わず、下の形の上に初めて触れる高さへ置く）。
// 本数は --banana-count=<n>（既定 10000）。視点は SceneCatalog.h の kMegaBananasViews（overview・close・low）、
// カメラパスは kMegaBananasPath（P キー・ゲームパッドの Y で再生・停止）。
// H キーで情報（本数・1 本の三角形数・元の三角形の総数。開発ビルドは MegaGeometry の GPU 時間も）を出し入れする。
// 資産（生成した GLB・クック済みのメッシュ）が無いときは警告して、バナナ抜きの床だけで続ける。
// ---------------------------------------------------------------------------

#include <atomic>

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Logging/LogTypes.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "GameModes/MegaBananas/BananasInfo.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "GameModes/MegaBananas/MegaBananasInfoView.h"
#endif

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace Game::GameModes
{

    /// バナナの読み込みと配置の途中経過（MegaBananasRoutine.cpp が定義する）
    struct BananaAssembly;

    /// --banana-count の既定値と上限
    inline constexpr uint32_t kDefaultBananaCount = 10000;
    inline constexpr uint32_t kMaxBananaCount = 65536;

    /// H キーで情報の表示を出し入れする入力の受け手（キーを消費しない）
    class BananasInfoToggle final : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnKey(const NorvesLib::Core::Input::KeyEvent &event) override;

        /// 押下の要求があれば true を返し、要求を下ろす
        bool ConsumeToggleRequest()
        {
            const bool bRequested = m_bRequested;
            m_bRequested = false;
            return bRequested;
        }

        void OnInputReset() override
        {
            m_bRequested = false;
        }

        const char *DebugName() const override
        {
            return "BananasInfoToggle";
        }

    private:
        bool m_bRequested = false;
    };

#if NORVES_BUILD_DEVELOPMENT
    /// MegaGeometry の統計のログ行（MEGA_OCCLUSION）から、描いたクラスタ数を読む受け口。
    /// エンジンは統計をログへだけ出し、読み出す口を公開していないので、ログの配送に相乗りする。
    /// OnLog は描画スレッドなど任意のスレッドから呼ばれるので、値はアトミックで受け渡す（Logger を再入しない）
    class MegaOcclusionLogTap final : public NorvesLib::Core::Logging::ILogSink
    {
    public:
        void OnLog(const NorvesLib::Core::Logging::LogEntry &entry) override;

        /// 直近に読んだ値。まだ読んでいなければ false
        bool TryGetDrawnClusters(uint32_t &outDrawnClusters) const
        {
            if (!m_bValid.load(std::memory_order_acquire))
            {
                return false;
            }
            outDrawnClusters = m_DrawnClusters.load(std::memory_order_relaxed);
            return true;
        }

    private:
        std::atomic<bool> m_bValid{false};
        std::atomic<uint32_t> m_DrawnClusters{0};
    };
#endif

    /// 非同期に読む材質のテクスチャ。読み込みの結果は主スレッドで届く（シーンを出たら bCancelled を立てて捨てる）
    struct BananasPendingMaterial
    {
        NorvesLib::Core::Rendering::MaterialHandle TargetMaterial;
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
    };

    struct MegaBananasData
    {
        /// 起動時の視点の名前（--scene-view。空ならシーンの既定の視点）
        NorvesLib::Core::Container::String m_ViewName;
        /// 入力の台本のパス（--camera-script。空なら流さない）
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// 起動時にカメラパスの途中へ置く時刻[秒]（--camera-path-time）。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// バナナの本数（--banana-count）
        uint32_t m_BananaCount = kDefaultBananaCount;
        /// 起動時から情報を出す（--open-scene-info。開発ビルドだけ。撮影で情報を確かめるのに使う）
        bool m_bShowInfoAtStart = false;
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu *m_pPauseMenu = nullptr;
        /// クック済みの資産を引く AssetSystem の窓口
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;

        // ---- 組み立ての状態（Routine が持つ。Leave で空に戻す） ----
        NorvesLib::Core::Container::TSharedPtr<BananaAssembly> m_pAssembly;
        NorvesLib::Core::Container::TSharedPtr<BananasPendingMaterial> m_pFloorMaterial;
        bool m_bFloorReady = false;
        bool m_bBananasPlaced = false;
        /// 床以外（バナナ）を置けなかった（資産が無い・壊れている）。シーンの組み立ては済んだものとして続ける
        bool m_bBananasSkipped = false;

        // ---- 情報の表示 ----
        BananasInfoToggle m_InfoToggle;
        bool m_bInfoToggleRegistered = false;
        bool m_bInfoVisible = false;
        BananasInfo m_Info;
        /// GPU 時間のログを最後に出した Tick（約 2 秒ごとに出す）
        uint32_t m_LastGpuLogTick = 0;
#if NORVES_BUILD_DEVELOPMENT
        MegaOcclusionLogTap m_OcclusionTap;
        bool m_bOcclusionTapRegistered = false;
#endif
#if defined(NORVES_ENABLE_IMGUI)
        MegaBananasInfoView m_InfoView{&m_Info, &m_bInfoVisible};
        bool m_bInfoViewRegistered = false;
#endif
    };

    class MegaBananasRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             MegaBananasData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, MegaBananasData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, MegaBananasData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "MegaBananas";
    };

    using MegaBananasMode = NorvesLib::Core::GameMode::TGameMode<MegaBananasRoutine, MegaBananasData>;

} // namespace Game::GameModes
