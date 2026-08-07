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
