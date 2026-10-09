#pragma once

#include "CameraLateUpdate.h"
#include "Gameplay/CharacterDriveOptions.h"
#include "Input/GameInputSettings.h"
#include <cstdint>

#include "Core/Public/Application/ApplicationHandlerBase.h"

#include "Bridge/BridgeRuntimeState.h"
#include "Bridge/BridgeServerHost.h"
#include "Bridge/NorvesLibBridgeAdapter.h"
#include "Debug/SequenceFrameCapture.h"
#include "GameModes/Rendering3DTest/M9WorldAcceptance.h"
#include "Pause/PauseMenu.h"
#include "Scenes/SceneCycleSmoke.h"
#include "Scripting/M6ScriptSmokeController.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "Debug/EngineStatsImGuiView.h"
#include "Pause/PauseMenuView.h"
#endif

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
} // namespace NorvesLib::Core::Asset

namespace NorvesLib::Core::GameMode
{
    class GameModeStateMachine;
} // namespace NorvesLib::Core::GameMode

namespace Game
{

    /**
     * @brief ゲームアプリケーションハンドラ
     *
     * ゲーム固有のアプリケーションイベントを処理します。
     */
    class GameApplicationHandler : public NorvesLib::Core::Application::ApplicationHandlerBase
    {
    public:
        GameApplicationHandler() = default;
        virtual ~GameApplicationHandler() = default;

        // ライフサイクルイベント
        virtual bool OnPreInitialize(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& args) override;
        virtual bool OnInitialize() override;
        virtual void OnPostInitialize() override;
        virtual void OnUpdate(float deltaTime) override;
        void OnLateUpdate(float deltaTime) override;
        virtual void OnPreRender() override;
        virtual void OnPostRender() override;
        virtual void OnPreShutdown() override;
        virtual void OnShutdown() override;

        // フォーカスイベント
        virtual void OnFocusGained() override;
        virtual void OnFocusLost() override;

        // GameModeステートマシン作成
        virtual NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::GameMode::IStateMachine>
        CreateGameModeStateMachine() override;

        /**
         * @brief シミュレーション進行ゲート（Core の Tick から参照される）。
         *
         * Bridge の runtime 状態（Edit/Playing は進行、Paused/Stopped は停止）を反映する。
         * @return シミュレーションを進行してよいなら true。
         * @note Bridge 無効時は常に true（従来挙動）。ゲームスレッドから呼ばれる。
         */
        bool ShouldAdvanceSimulation() const override;

        /**
         * @brief Bridge runtime 状態のアクセサ（adapter がゲームスレッド上から呼ぶ）。
         * @return 現在の Bridge runtime 状態。
         * @note ゲームスレッド上からのみアクセスすること。
         */
        Game::Bridge::BridgeRuntimeState GetBridgeRuntimeState() const { return m_BridgeRuntimeState; }
        /**
         * @brief Bridge runtime 状態を設定する（adapter がゲームスレッド上から呼ぶ）。
         * @param state 設定する Bridge runtime 状態。
         * @note ゲームスレッド上からのみアクセスすること。
         */
        void SetBridgeRuntimeState(Game::Bridge::BridgeRuntimeState state) { m_BridgeRuntimeState = state; }

        /**
         * @brief テクスチャアセットルートのアクセサ（Bridge adapter がゲームスレッド上から呼ぶ）。
         * @return 設定済みのアセットルート（未設定なら空）。
         * @note ゲームスレッド上からのみアクセスすること。借用参照（adapter は保持しない）。
         */
        const NorvesLib::Core::Container::String &GetTextureAssetRoot() const { return m_TextureAssetRoot; }
        /**
         * @brief テクスチャアセット manifest パスのアクセサ（Bridge adapter がゲームスレッド上から呼ぶ）。
         * @return 設定済みの manifest パス（未設定なら空）。
         * @note ゲームスレッド上からのみアクセスすること。借用参照（adapter は保持しない）。
         */
        const NorvesLib::Core::Container::String &GetTextureAssetManifestPath() const { return m_TextureAssetManifestPath; }

        /**
         * @brief 起動時に構成された asset manifest を同期再読込する。
         * @return runtime が同一 immutable snapshot を受理した場合だけ true。
         * @note 失敗時は現在の snapshot を変更しない。ゲームスレッド上からのみ呼ぶこと。
         */
        bool ReloadConfiguredAssetManifest();

        /**
         * @brief クック済みのマニフェストを使えないとき、マニフェストの無い
         * AssetSystem を「クック済みを使う前提」で入れる。
         * @note ばらの元画像の
         * root（m_TextureLooseAssetRoot）を読み、読んだ各パスに
         * TEXTURE_COOKED_MISSING を1回ずつ警告する。
         */
        void InstallCookedManifestUnavailableAssetSystem();

        bool PrepareM9WorldAssets();

        /**
         * @brief --scene・--scene-view の指定をシーンの一覧と照合して確定する（OnPreInitialize の最後に呼ぶ）。
         * @return 起動を続けてよいなら true。未知の id・視点はエラーをログに出して既定へ落とし、true を返す。
         *         --dog-movement-smoke との併用など、解決できない指定だけ false。
         */
        bool ResolveStartupScene();
        bool HasPendingAssetConsumers() const override;
        Game::Input::GameInputSettings& GetInputSettings() { return m_InputSettings; }
        const Game::Input::GameInputSettings& GetInputSettings() const { return m_InputSettings; }

        /**
         * @brief Bridge 読み取り用の immutable asset snapshot を取得する。
         * @return 現在保持中の snapshot。未構成または未受理なら null。
         * @note by-value で返し、呼び出し中の snapshot 寿命を保持する。
         */
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>
        GetAssetSystemSnapshot() const;

        /**
         * @brief クック済みのマニフェストに、テクスチャの論理パス（"Assets/..."
         * でもよい）の項目があるか。
         * @note マニフェストを読んでいない（クック済みを使わない）ときは常に
         * false。
         */
        bool IsTextureCooked(const NorvesLib::Core::Container::String &logicalPath) const;

    private:
        Game::Input::GameInputSettings m_InputSettings;
        NorvesLib::Core::Container::TSharedPtr<CameraLateUpdateSlot> m_CameraLateUpdateSlot =
            NorvesLib::Core::Container::MakeShared<CameraLateUpdateSlot>();
        /**
         * @brief --bridge-port を解析する（OnPreInitialize から呼ぶ）。無効値は
         *        m_bBridgeEnabled=false のまま（Bridge 無効）にして警告ログを出すのみで、
         *        クラッシュさせない。
         * @param args コマンドライン引数列（args[0]=実行ファイル）。
         */
        void ParseBridgePortOption(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& args);

        /**
         * @brief
         * 非同期の読み込みが落ち着いた最初の描画の後に、テクスチャとバッファの確保量を
         * VRAM_LEDGER として1回ログへ出す。
         * @note
         * 読み込み中を一度も見ないまま一定フレームが過ぎたときも、その時点で1回出す。
         */
        void LogVramLedgerOnce();

        /**
         * @brief ポーズメニューの開閉をエンジンへ反映し、決定された項目を実行する（OnUpdate から呼ぶ）。
         * @note 開いた間は入力の文脈をメニューにしてカーソルを出し、閉じたら戻す。
         */
        void UpdatePauseMenu();

        // ゲーム固有のメンバー変数
        bool m_bIsPaused = false;
        bool m_bHasTextureAssetRuntimeConfig = false;
        bool m_bRendering3DTestUseCookedModel = false;
        // --no-cooked-textures: クック済みを使わず、ばらの元画像を無圧縮で読む（見た目の比較用）。
        bool m_bNoCookedTextures = false;
        // 既定の build/CookedAssets/ を root にしたとき、マニフェストに無いテクスチャをばらで読む場所（Assets/）。
        // 空のときは root と同じ場所を読む（--texture-asset-root で明示したときの従来の動き）。
        NorvesLib::Core::Container::String m_TextureLooseAssetRoot;
        // 既定のクック済みの設定が使えず（マニフェストが無い・読めない）、全テクスチャをばらの元画像で読むとき true。
        // 読み込む各パスに TEXTURE_COOKED_MISSING を警告するため、マニフェストの無い AssetSystem を入れ直す。
        bool m_bCookedManifestUnavailable = false;
        NorvesLib::Core::Container::String m_TextureAssetRoot;
        NorvesLib::Core::Container::String m_TextureAssetManifestPath;
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem> m_AssetSystemSnapshot;
        NorvesLib::Core::Container::String m_Rendering3DTestModelPath;
        Game::Scripting::M6ScriptSmokeController m_M6ScriptSmokeController;
        // --capture-sequence で、1回の起動の中の複数の描画フレームを撮る。
        Game::Debug::SequenceFrameCapture m_SequenceFrameCapture;
        NorvesLib::Core::Container::TSharedPtr<Game::GameModes::M9WorldAcceptanceConfig> m_M9WorldAcceptance;

        // 起動画面の非同期の読み込みが終わった後に VRAM_LEDGER を1回だけログへ出すための状態。
        bool m_bVramLedgerLogged = false;
        bool m_bVramLedgerSawPending = false;
        uint32_t m_VramLedgerFrameCount = 0;

        // Bridge（NorvesEditor 連携）。adapter は host より長生きする必要があるため、
        // 宣言順を adapter → host にしてデストラクト順（host → adapter）を保証する。
        /** @brief Bridge（NorvesEditor 連携）が有効かどうか。 */
        bool m_bBridgeEnabled = false;
        /** @brief Bridge サーバーの待受ポート（1-65535、0 は無効）。 */
        uint16_t m_BridgePort = 0;
        /** @brief NorvesLib 用エンジンアダプタ。host より長生きする（宣言順で保証）。 */
        Game::Bridge::NorvesLibBridgeAdapter m_BridgeAdapter;
        /** @brief Bridge サーバーホスト（受信スレッドと送受信を所有）。 */
        Game::Bridge::BridgeServerHost m_BridgeHost;

        /**
         * @brief Bridge runtime（play/pause/stop）状態。既定は Edit（従来挙動）。
         * @note m_bIsPaused（フォーカス由来のポーズ）とは別概念なので流用しない。
         */
        Game::Bridge::BridgeRuntimeState m_BridgeRuntimeState = Game::Bridge::BridgeRuntimeState::Edit;

        bool m_bAnimationDebugRequested = false;
        bool m_bDogMovementSmoke = false;
        /// --scene=<id> で指定された起動シーンの id。空なら既定のシーン（ResolveStartupScene が決める）
        NorvesLib::Core::Container::String m_StartupSceneId;
        /// ランチャーから始めるか（--scene を省いた起動、--scene=launcher、--scene-cycle-smoke）。ResolveStartupScene が決める
        bool m_bLauncherStartup = false;
        /// --scene-view=<name> で指定された名前付き視点。空ならシーンの既定の視点
        NorvesLib::Core::Container::String m_StartupSceneView;
        /// --scene-report=<path> で指定された、シーンの解決結果（未知の id・視点、欠けた資産）の出力先。空なら書かない
        NorvesLib::Core::Container::String m_SceneReportPath;
        /// --camera-script=<file>（開発ビルドだけ）で指定された、自由飛行カメラへ流す入力の台本。空なら流さない
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// --camera-path-time=<秒> で指定された、起動時にカメラパスの途中へ置く時刻。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// --scene-cycle-smoke=<id,...> の検査（ランチャーとシーンの間を周回して、資源が増えないことを確かめる）
        Game::Scenes::SceneCycleSmoke m_SceneCycle;
        /// ポーズメニュー（ランチャー以外のシーンで Esc・Start で開く。ランチャーは入場でこれを無効にする）
        Game::Pause::PauseMenu m_PauseMenu;
        /// ポーズメニューを使う起動か。ImGui が使え、撮影・自動実行（--capture-png・--exit-after-frames）でないとき。
        /// --open-pause-menu（開発ビルドだけ）の起動は撮影でも使う
        bool m_bPauseMenuWanted = false;
        /// ポーズメニューを入力ルーターと ImGui へ登録済みか
        bool m_bPauseMenuRegistered = false;
        /// ポーズメニューが開いた状態をエンジン（入力文脈・カーソル・撮影への重ね）へ反映済みか
        bool m_bPauseMenuApplied = false;
        /// 反映のためにメニュー用の入力文脈を積んだか（閉じるときに外す）
        bool m_bPauseMenuPushedContext = false;
        /// --open-pause-menu: 起動して数フレーム後に開く（開発ビルドだけ。撮影で開いた状態を確かめるのに使う）
        bool m_bOpenPauseMenuAtStartup = false;
        uint32_t m_PauseMenuTickCount = 0;
#if defined(NORVES_ENABLE_IMGUI)
        Game::Pause::PauseMenuView m_PauseMenuView{&m_PauseMenu};
#endif
        /// CreateGameModeStateMachine が作ったステートマシン（非所有。Engine が所有する。OnPreShutdown で外す）
        NorvesLib::Core::GameMode::GameModeStateMachine *m_pGameModeStateMachine = nullptr;
        NorvesLib::Modules::Physics::CharacterDriveMode m_DogDrive =
            NorvesLib::Modules::Physics::CharacterDriveMode::Fixed;
#if defined(NORVES_ENABLE_IMGUI)
        bool m_bImGuiRequested = false;
        bool m_bEngineStatsImGuiViewRegistered = false;
        Game::Debug::EngineStatsImGuiView m_EngineStatsImGuiView;
#endif
    };

} // namespace Game
