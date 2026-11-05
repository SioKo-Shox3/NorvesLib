#pragma once

#include "CombatSliceTypes.h"
#include "GameMode/GameModeContext.h"
#include "Rendering/RenderTypes.h"

namespace Game::GameModes
{
    struct CombatSliceEnvironment
    {
        NorvesLib::Core::Rendering::MaterialHandle FloorMaterial;
        NorvesLib::Core::Rendering::MaterialHandle BedMaterial;
    };

    // Entity とメッシュは ScopeRef が所有する。失敗時も部分生成済みの資源を保持する。
    bool BuildCombatSliceEnvironment(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                     CombatSliceSceneIds& ids, CombatSliceEnvironment& env);

    // 成功・失敗のどちらでも ScopeRef.Cleanup() の後に呼び、材質の参照を手放す。
    void ReleaseCombatSliceEnvironment(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                       CombatSliceEnvironment& env);
} // namespace Game::GameModes
