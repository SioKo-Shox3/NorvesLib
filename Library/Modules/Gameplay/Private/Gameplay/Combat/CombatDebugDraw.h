// 戦闘判定のデバッグ表示を扱う宣言を置く。
#pragma once

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "Gameplay/Combat/CombatRuntime.h"

namespace NorvesLib::Core::Rendering
{
    class DebugDrawQueue;
}

namespace NorvesLib::Modules::Gameplay::Combat
{
    void SubmitCombatDebugShapes(const Core::Container::VariableArray<CombatVolumeSnapshot>& volumes,
                                 Core::Rendering::DebugDrawQueue& queue);
} // namespace NorvesLib::Modules::Gameplay::Combat
#endif
