// 枯れ度の鍵付き状態をゲームの保存データとの間で写す。
#include "VegetationSaveBridge.h"

#include "ProgressSaveData.h"
#include "Core/Public/Vegetation/DrynessMap.h"

namespace Game::Save
{
    using NorvesLib::Core::Vegetation::DrynessKeyedState;
    using NorvesLib::Core::Vegetation::DrynessMap;

    void CaptureDryness(const DrynessMap& map, ProgressSaveData& out)
    {
        Container::VariableArray<DrynessKeyedState> states;
        map.CaptureKeyedStates(states);
        out.Vegetation.Dryness.clear();
        out.Vegetation.Dryness.reserve(states.size());
        for (const auto& state : states)
        {
            out.Vegetation.Dryness.push_back({state.Key, state.CurrentLevel, state.TargetLevel, state.RemainingSeconds});
        }
    }

    void ApplyDryness(const ProgressSaveData& data, DrynessMap& map)
    {
        Container::VariableArray<DrynessKeyedState> states;
        states.reserve(data.Vegetation.Dryness.size());
        for (const auto& entry : data.Vegetation.Dryness)
        {
            states.push_back({entry.Key, entry.CurrentLevel, entry.TargetLevel, entry.RemainingSeconds});
        }
        map.RestoreKeyedStates(states);
    }

    void ResetDryness(DrynessMap& map)
    {
        map.RestoreKeyedStates({});
    }
} // namespace Game::Save
