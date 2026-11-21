#pragma once

#include "Math/Vector3.h"
#include <cstdint>

/// 刺激の強さ I と感度から、気づき a を clamp(a + (Gain × I × 感度 − Decay × (1 − I)) × dt, 0, 1) で更新する。
/// 上昇閾値に達したら、シードと反応回数で決まる遅れを経て昇格する。
/// 閾値からヒステリシス幅を引いた値を下回ると、即座に降格または昇格の保留を取り消す。
/// 位置を伴う刺激が途絶えてから記憶時間が経つと、最終既知位置を忘れる。閾値と係数は根拠のない仮値。
namespace NorvesLib::Modules::AI::Perception
{
    struct AwarenessTuning
    {
        float Gain = 1.5f;   // 最大刺激・感度 1 での毎秒の増加量
        float Decay = 0.25f; // 無刺激での毎秒の減少量
        float SuspiciousThreshold = 0.35f;
        float AlertThreshold = 0.8f;
        float Hysteresis = 0.05f;
        float ReactionDelaySeconds = 0.3f;
        float ReactionJitterSeconds = 0.1f;
        float MemorySeconds = 10.0f;
    };

    enum class EAwarenessLevel : uint8_t
    {
        Unaware,
        Suspicious,
        Alert
    };

    struct AwarenessState
    {
        float Awareness = 0.0f;
        EAwarenessLevel Level = EAwarenessLevel::Unaware;
        EAwarenessLevel PendingLevel = EAwarenessLevel::Unaware;
        float PendingSeconds = 0.0f; // PendingLevel が Level より高い間の残り秒数
        bool bHasLastKnownPosition = false;
        Math::Vector3 LastKnownPosition;
        float SecondsSinceStimulus = 0.0f;
        uint32_t ReactionCount = 0; // 昇格の保留を開始した回数
    };

    struct AwarenessInput
    {
        float Intensity = 0.0f;
        float Sensitivity = 1.0f;
        bool bHasStimulusPosition = false;
        Math::Vector3 StimulusPosition;
    };

    /// 正の有限な時間刻みで気づきと位置の記憶を更新する。非有限の強さ・感度は零とする。
    void UpdateAwareness(AwarenessState& state, const AwarenessInput& input, const AwarenessTuning& tuning,
                         float deltaSeconds, uint32_t seed);
}
