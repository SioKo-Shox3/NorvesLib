#pragma once

#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::Physics
{
    /**
     * @brief Physics module の公開寿命境界
     *
     * SceneQuery provider や具象実装は公開しない。query は Engine が保持する
     * SceneQuery façade だけを通じて利用する。
     */
    class IPhysicsModule : public Core::Module::IModule
    {
    public:
        virtual ~IPhysicsModule() = default;

        /**
         * @brief 固定ステップ 1 歩の前半（剛体の前の位置の写し）
         *
         * 通常は Install で World の段 FixedBegin に登録したシステムが呼ぶ。テスト・ツールは直接呼んで 1 歩を進めてよい。
         */
        virtual void StepPreFixed(float fixedDeltaTime) = 0;

        /**
         * @brief 固定ステップ 1 歩の物理（積分・キャラクター・接触・イベント）
         *
         * 通常は段 FixedTick の、Component の FixedTick と CharacterBody の要求の後のシステムが呼ぶ。
         */
        virtual void StepFixed(float fixedDeltaTime) = 0;
    };

    /**
     * @brief Physics module を生成して registry へ登録する
     * @return 登録された借用ポインタ。重複時は既存 Physics module の借用を返す。
     */
    IPhysicsModule* RegisterPhysicsModule(Core::Module::ModuleRegistry& registry);

    /**
     * @brief registry から登録済み Physics module の借用を検索する
     */
    IPhysicsModule* FindPhysicsModule(Core::Module::ModuleRegistry& registry);
} // namespace NorvesLib::Modules::Physics
