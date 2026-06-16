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
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> &args) override;
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

        // シミュレーション進行ゲート（Core の Tick から参照される）。
        // Bridge の runtime 状態（Edit/Playing は進行、Paused/Stopped は停止）を反映する。
        bool ShouldAdvanceSimulation() const override;

        // Bridge runtime 状態のアクセサ（adapter がゲームスレッド上から呼ぶ）。
        Game::Bridge::BridgeRuntimeState GetBridgeRuntimeState() const { return m_BridgeRuntimeState; }
        void SetBridgeRuntimeState(Game::Bridge::BridgeRuntimeState state) { m_BridgeRuntimeState = state; }

    private:
        bool ApplyTextureAssetRuntimeConfig();

        // --bridge-port を解析する（OnPreInitialize から呼ぶ）。無効値は
        // m_bBridgeEnabled=false のまま（Bridge 無効）にして警告ログを出すのみで、
        // クラッシュさせない。
        void ParseBridgePortOption(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> &args);

        // ゲーム固有のメンバー変数
        bool m_bIsPaused = false;
        bool m_bHasTextureAssetRuntimeConfig = false;
        NorvesLib::Core::Container::String m_TextureAssetRoot;
        NorvesLib::Core::Container::String m_TextureAssetManifestPath;
        NorvesLib::Core::Container::String m_Rendering3DTestModelPath;

        // Bridge（NorvesEditor 連携）。adapter は host より長生きする必要があるため、
        // 宣言順を adapter → host にしてデストラクト順（host → adapter）を保証する。
        bool m_bBridgeEnabled = false;
        std::uint16_t m_BridgePort = 0;
        Game::Bridge::NorvesLibBridgeAdapter m_BridgeAdapter;
        Game::Bridge::BridgeServerHost m_BridgeHost;

        // Bridge runtime（play/pause/stop）状態。既定は Edit（従来挙動）。
        // m_bIsPaused（フォーカス由来のポーズ）とは別概念なので流用しない。
        Game::Bridge::BridgeRuntimeState m_BridgeRuntimeState = Game::Bridge::BridgeRuntimeState::Edit;
    };

} // namespace Game
