#pragma once

#include "Core/Public/Application/ApplicationHandlerBase.h"
#include "Core/Public/Input/MayaCameraController.h"
#include "Core/Public/Input/LightController.h"
#include "Core/Public/Rendering/SceneProxy.h"

// 前方宣言
namespace NorvesLib::Core
{
    class WorldObject;
}

namespace NorvesLib::Core::Component
{
    class MeshComponent;
}

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
        // ゲーム固有のメンバー変数
        bool m_bIsPaused = false;

        // テスト三角形オブジェクト（World管理下）
        NorvesLib::Core::WorldObject *m_pTriangleObject = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pTriangleMeshComponent = nullptr;

        // ========================================
        // 入力コントローラー
        // ========================================

        /// Maya準拠カメラコントローラー
        NorvesLib::Core::Input::MayaCameraController m_CameraController;

        /// ライトコントローラー
        NorvesLib::Core::Input::LightController m_LightController;

        /// メインディレクショナルライト
        NorvesLib::Core::Rendering::LightProxy m_MainLight;
    };

} // namespace Game
