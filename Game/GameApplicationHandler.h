#pragma once

#include "Core/Public/Application/ApplicationHandlerBase.h"

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

    private:
        bool ApplyTextureAssetRuntimeConfig();

        // ゲーム固有のメンバー変数
        bool m_bIsPaused = false;
        bool m_bHasTextureAssetRuntimeConfig = false;
        NorvesLib::Core::Container::String m_TextureAssetRoot;
        NorvesLib::Core::Container::String m_TextureAssetManifestPath;
        NorvesLib::Core::Container::String m_Rendering3DTestModelPath;
    };

} // namespace Game
