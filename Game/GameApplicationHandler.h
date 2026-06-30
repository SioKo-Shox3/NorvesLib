#pragma once

#include <cstdint>

#include "Core/Public/Application/ApplicationHandlerBase.h"

#include "Bridge/BridgeRuntimeState.h"
#include "Bridge/BridgeServerHost.h"
#include "Bridge/NorvesLibBridgeAdapter.h"

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

    private:
        bool ApplyTextureAssetRuntimeConfig();

        /**
         * @brief --bridge-port を解析する（OnPreInitialize から呼ぶ）。無効値は
         *        m_bBridgeEnabled=false のまま（Bridge 無効）にして警告ログを出すのみで、
         *        クラッシュさせない。
         * @param args コマンドライン引数列（args[0]=実行ファイル）。
         */
        void ParseBridgePortOption(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& args);

        // ゲーム固有のメンバー変数
        bool m_bIsPaused = false;
        bool m_bHasTextureAssetRuntimeConfig = false;
        NorvesLib::Core::Container::String m_TextureAssetRoot;
        NorvesLib::Core::Container::String m_TextureAssetManifestPath;
        NorvesLib::Core::Container::String m_Rendering3DTestModelPath;

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
    };

} // namespace Game
