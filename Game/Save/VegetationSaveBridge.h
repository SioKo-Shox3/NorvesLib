// 枯れ度の鍵付き状態をゲームの保存データとの間で写す。
#pragma once

namespace NorvesLib::Core::Vegetation
{
    class DrynessMap;
}

namespace Game::Save
{
    struct ProgressSaveData;

    // GameThread 専用。鍵を結んだ源ごとに今の値・目標・残りを写す（SourceId は写さない）。
    void CaptureDryness(const NorvesLib::Core::Vegetation::DrynessMap& map, ProgressSaveData& out);
    // 保存の値で復元する。節が無いセーブでは空の一覧で呼び、全部の源を既定の値へ戻す。
    void ApplyDryness(const ProgressSaveData& data, NorvesLib::Core::Vegetation::DrynessMap& map);
    // 進行を空にする時に呼ぶ。生きた源は既定の値へ戻し、源の無い鍵の保持値と手放した源は捨てる。
    void ResetDryness(NorvesLib::Core::Vegetation::DrynessMap& map);
} // namespace Game::Save
