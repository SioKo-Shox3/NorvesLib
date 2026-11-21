#pragma once

#include "Audio/AudioSpatial.h"
#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    enum class AudioListenerMode : uint8_t
    {
        MainCamera,
        Entity,
        Hybrid
    };

    /** @brief リスナーの位置と基底を読む。速度は解決側で位置の差分と経過時間から求める。 */
    class IAudioListenerSource
    {
    public:
        virtual ~IAudioListenerSource() = default;

        /** 読めたら true を返す。読めないときは outState を変えない。 */
        [[nodiscard]] virtual bool TryGetListenerPose(AudioListenerState& outState) const = 0;
    };
}
