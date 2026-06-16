#pragma once

#include "IApplicationHandler.h"

namespace NorvesLib::Core::Application
{

    /**
     * @brief ApplicationHandlerのデフォルト実装を提供する基底クラス
     *
     * 必要なメソッドのみをオーバーライドして使用できます。
     * すべてのメソッドにデフォルト実装（何もしない）が提供されています。
     */
    class ApplicationHandlerBase : public IApplicationHandler
    {
    public:
        virtual ~ApplicationHandlerBase() = default;

        // ========== ライフサイクルイベント（デフォルト実装） ==========

        virtual bool OnPreInitialize(const Container::VariableArray<Container::String> &args) override
        {
            (void)args;
            return true;
        }

        virtual bool OnInitialize() override
        {
            return true;
        }

        virtual void OnPostInitialize() override
        {
        }

        virtual void OnUpdate(float deltaTime) override
        {
            (void)deltaTime;
        }

        bool ShouldAdvanceSimulation() const override
        {
            return true;
        }

        virtual void OnPreRender() override
        {
        }

        virtual void OnPostRender() override
        {
        }

        virtual void OnPreShutdown() override
        {
        }

        virtual void OnShutdown() override
        {
        }

        // ========== フォーカス・サスペンドイベント（デフォルト実装） ==========

        virtual void OnFocusGained() override
        {
        }

        virtual void OnFocusLost() override
        {
        }

        virtual void OnSuspend() override
        {
        }

        virtual void OnResume() override
        {
        }

        // ========== GameMode ==========

        virtual Container::TUniquePtr<GameMode::IStateMachine> CreateGameModeStateMachine() override
        {
            // ゲーム側でオーバーライドしてステートマシンを作成
            // デフォルトではnullptrを返す（GameModeなしで動作可能）
            return nullptr;
        }
    };

} // namespace NorvesLib::Core::Application
