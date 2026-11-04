#pragma once

#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/CombatHitEvent.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay
{
    class ProgressionState;
    class DefeatGate;

    namespace World
    {
        class EncounterService;
    }

    /**
     * @brief 攻撃判定・遭遇領域・体力などのゲームプレイの汎用部分を扱うモジュール。
     * Physics へのリンクは持たない。
     */
    class IGameplayModule : public Core::Module::IModule
    {
    public:
        virtual ~IGameplayModule() = default;

        /// 敵のパターン選択などの再現用の値を取得する。既定値は 0。
        virtual uint64_t GetRunSeed() const = 0;
        /// 敵のパターン選択などの再現用の値を、run の開始時に呼び手が設定する。
        virtual void SetRunSeed(uint64_t seed) = 0;

        virtual ProgressionState& GetProgressionState() = 0;
        virtual DefeatGate& GetDefeatGate() = 0;
        virtual Core::MulticastDelegate<const Combat::CombatHitEvent&>& GetOnAnyHit() = 0;
        virtual World::EncounterService& GetEncounterService() = 0;
    };

    /**
     * @brief Gameplay モジュールを生成して registry へ登録する。
     * @return 登録された借用ポインタ。重複時は既存モジュールの借用を返す。
     */
    IGameplayModule* RegisterGameplayModule(Core::Module::ModuleRegistry& registry);

    /**
     * @brief registry から登録済み Gameplay モジュールの借用を検索する。
     */
    IGameplayModule* FindGameplayModule(Core::Module::ModuleRegistry& registry);
}
