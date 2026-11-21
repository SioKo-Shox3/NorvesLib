#pragma once

#include "AI/Navigation/INavigationService.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::AI
{
    /// ナビゲーションサービスの寿命を管理する AI モジュール。
    class IAIModule : public Core::Module::IModule
    {
    public:
        virtual ~IAIModule() = default;

        /// モジュールが所有するサービスを GameThread で借りる。
        virtual INavigationService& GetNavigation() = 0;
        virtual const INavigationService& GetNavigation() const = 0;

        /// 格子と経路の描画を切り替える。Release では常に無効。
        virtual void SetNavDebugDrawEnabled(bool bEnabled) = 0;
        virtual bool IsNavDebugDrawEnabled() const = 0;

        /// 操舵の速度・目標・経路の描画を切り替える。Release では常に無効。
        virtual void SetSteeringDebugDrawEnabled(bool bEnabled) = 0;
        /// 所有スレッドで操舵の描画が有効かを返す。Release では常に false。
        virtual bool IsSteeringDebugDrawEnabled() const = 0;
    };

    /// モジュールを登録して借用ポインタを返す。登録済みなら同じモジュールを返す。
    IAIModule* RegisterAIModule(Core::Module::ModuleRegistry& registry);

    /// 登録済みのモジュールを検索する。未登録なら nullptr を返す。
    IAIModule* FindAIModule(Core::Module::ModuleRegistry& registry);
} // namespace NorvesLib::Modules::AI
