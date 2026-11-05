// しがみつきの調整値と JSON 読み込みを提供する。
#pragma once

#include "Container/String.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Grip
{
    /**
     * @brief しがみつきの荷重・ゲージ・減衰の調整値。
     * 値は仮。free 1.5 g・panic 4 g は要件の例。
     */
    struct GripTuning
    {
        // この荷重以下では基本の減りだけがかかり、足場上で回復できる。
        float FreeLoadG = 1.5f;
        // 荷重による減りが最大になる荷重。FreeLoadG より大きい。
        float PanicLoadG = 4.0f;
        // 満タンを 1 とするゲージの毎秒の基本の減り。
        float BaseDrainPerSecond = 0.02f;
        // 荷重による毎秒の減りの係数。
        float LoadDrainPerSecond = 0.25f;
        // 足場上での毎秒の回復量。
        float RecoveryPerSecond = 0.1f;
        // 荷重の平滑の時定数。0 は平滑なし。
        float SmoothingSeconds = 0.1f;
        // 張力とアンカーの加速度の重み。両方の和は正。
        float TensionWeight = 1.0f;
        float AccelerationWeight = 0.0f;
        // サブステップ数の上限。1 から 8。
        uint32_t MaxSubsteps = 8;
        // 混ぜた荷重の上限。PanicLoadG 以上。
        float MaxLoadG = 10.0f;
        // 握りの点に対する接線速度の毎秒の減衰率。
        float SwingDampingPerSecond = 0.5f;
    };

    // 失敗時は out を変更せず、outError にソース名・キーの道筋・説明を返す。
    bool LoadGripTuningFromJson(const Core::Container::String& text, const Core::Container::String& sourceName,
                                GripTuning& out, Core::Container::String& outError);

    // assetPath は Assets からの相対パス。既定の資産は Gameplay/grip.tuning.json。
    bool LoadGripTuningFromAsset(const Core::Container::String& assetPath, GripTuning& out,
                                 Core::Container::String& outError);
} // namespace NorvesLib::Modules::Gameplay::Grip
