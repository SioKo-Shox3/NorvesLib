#pragma once

namespace NorvesLib::Modules::Gameplay
{
    class ProgressionState;
}

namespace Game::Save
{
    struct ProgressSaveData;

    // GameThread 専用。進行の定義（LoadDefinitionFromJson）は写さず、撃破の集合だけを写す。
    // 読み取りに失敗した場合、out は変わらない。
    bool CaptureProgression(const NorvesLib::Modules::Gameplay::ProgressionState& state, ProgressSaveData& out);
    bool ApplyProgression(const ProgressSaveData& data, NorvesLib::Modules::Gameplay::ProgressionState& state);
    bool ClearProgression(NorvesLib::Modules::Gameplay::ProgressionState& state);
} // namespace Game::Save
