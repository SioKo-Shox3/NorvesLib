#pragma once

#include <cmath>

namespace NorvesLib::Core::Input
{
    // 無効時はnext/finishedを保持。加算前に残り時間で比較し巨大dtのoverflowを避ける。
    inline bool AdvanceHapticsPlaybackTime(double duration, bool loop, double current, double delta,
        double& next, bool& finished)
    {
        if (!std::isfinite(duration) || duration <= 0 || !std::isfinite(current) || current < 0 ||
            current > duration || (loop && current == duration) || !std::isfinite(delta) || delta < 0)
        {
            return false;
        }
        double candidate = current;
        bool ended = false;
        if (loop)
        {
            const double remainder = std::fmod(delta, duration);
            const double remaining = duration - current;
            candidate = remainder >= remaining ? remainder - remaining : current + remainder;
            if (candidate >= duration)
            {
                candidate = 0;
            }
        }
        else if (delta >= duration - current)
        {
            candidate = duration;
            ended = true;
        }
        else
        {
            candidate = current + delta;
            // 残り未満でも加算の丸めで端点へ達した場合は、終了状態と値を揃える。
            if (candidate >= duration)
            {
                candidate = duration;
                ended = true;
            }
        }
        next = candidate;
        finished = ended;
        return true;
    }
} // namespace NorvesLib::Core::Input
