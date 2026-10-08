#include "Engine/TimeSystem.h"
#include "Engine/TimeScaleFadeMath.h"
#include "Engine/TimeScaleMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Engine
{
    namespace
    {
        TimeSystemResult Convert(TimeScaleMathResult value)
        {
            return value == TimeScaleMathResult::Success    ? TimeSystemResult::Success
                   : value == TimeScaleMathResult::Overflow ? TimeSystemResult::Overflow
                                                            : TimeSystemResult::InvalidArgument;
        }
        bool SecondsToNs(double seconds, int64_t& out)
        {
            if (!std::isfinite(seconds) || seconds < 0)
                return false;
            const double value = std::round(seconds * 1e9);
            // INT64_MAXはdoubleで2^63へ丸まるので排他的上限で確認する。
            if (!std::isfinite(value) || value >= 9223372036854775808.0)
                return false;
            out = static_cast<int64_t>(value);
            return true;
        }
    } // namespace
    TimeSystemResult TimeSystem::PushScale(const TimeScaleRequest& settings, TimeScaleHandle& out)
    {
        if (!settings.Channels || (settings.Channels & ~ScalableTimeChannels))
            return TimeSystemResult::InvalidArgument;
        Request candidate;
        int64_t duration = 0, fadeIn = 0, fadeOut = 0;
        if (!SecondsToNs(settings.DurationSeconds, duration) || duration <= 0 ||
            !SecondsToNs(settings.FadeInSeconds, fadeIn) || !SecondsToNs(settings.FadeOutSeconds, fadeOut) ||
            fadeOut > duration || fadeIn > duration - fadeOut)
            return TimeSystemResult::InvalidArgument;
        const auto scaleResult = QuantizeTimeScale(settings.Scale, candidate.Numerator);
        if (scaleResult != TimeScaleMathResult::Success)
            return Convert(scaleResult);
        if (duration > std::numeric_limits<int64_t>::max() - m_Now || m_NextId == 0)
            return TimeSystemResult::Overflow;
        candidate.Handle = {m_NextId};
        candidate.Settings = settings;
        candidate.Start = m_Now;
        candidate.End = m_Now + duration;
        candidate.FadeInEnd = m_Now + fadeIn;
        candidate.FadeOutStart = candidate.End - fadeOut;
        m_Requests.push_back(candidate);
        ++m_NextId;
        out = candidate.Handle;
        return TimeSystemResult::Success;
    }
    TimeSystemResult TimeSystem::RemoveScale(TimeScaleHandle handle)
    {
        if (!handle.IsValid())
            return TimeSystemResult::InvalidArgument;
        for (size_t i = 0; i < m_Requests.size(); ++i)
            if (m_Requests[i].Handle.Value == handle.Value)
            {
                m_Requests.erase(m_Requests.begin() + i);
                return TimeSystemResult::Success;
            }
        return TimeSystemResult::StaleHandle;
    }
    void TimeSystem::Reset()
    {
        m_Requests.clear();
        m_Now = 0;
        m_PhysicsCarry = 0;
        m_Frame = {};
        // ハンドル番号は巻き戻さず、Reset前のハンドルを新要求へ再利用しない。
    }
    TimeSystemResult TimeSystem::BeginFrame(int64_t raw, float clampedDt, bool advance)
    {
        if (raw < 0 || !std::isfinite(clampedDt) || clampedDt < 0 || (raw == 0 && clampedDt != 0))
            return TimeSystemResult::InvalidArgument;
        if (raw > std::numeric_limits<int64_t>::max() - m_Now)
            return TimeSystemResult::Overflow;
        const int64_t end = m_Now + raw;
        FrameTimes frame;
        frame.Unscaled = clampedDt;
        uint32_t carry = m_PhysicsCarry;
        double seconds[4]{};
        if (advance && raw > 0)
        {
            for (int64_t start = m_Now; start < end;)
            {
                int64_t next = end;
                for (const auto& request : m_Requests)
                    for (const int64_t boundary : {request.FadeInEnd, request.FadeOutStart, request.End})
                        if (boundary > start && boundary < next)
                            next = boundary;
                const auto duration = next - start;
                for (unsigned channel = static_cast<unsigned>(TimeChannel::World);
                     channel <= static_cast<unsigned>(TimeChannel::Physics); ++channel)
                {
                    const bool physics = channel == static_cast<unsigned>(TimeChannel::Physics);
                    if (!physics && clampedDt == 0)
                        continue;
                    Container::VariableArray<LinearTimeScale> factors;
                    Container::VariableArray<uint32_t> numerators;
                    bool fading = false;
                    for (const auto& request : m_Requests)
                    {
                        if (start < request.Start || start >= request.End ||
                            !(request.Settings.Channels & TimeChannelBit(static_cast<TimeChannel>(channel))))
                            continue;
                        const double target =
                            physics ? double(request.Numerator) / TimeScaleDenominator : request.Settings.Scale;
                        LinearTimeScale factor{target, target};
                        // startのphaseを使い、期限の左極限を積分する。終了後の1を偽のrampにしない。
                        if (target != 1 && start < request.FadeInEnd)
                        {
                            const double a = double(start - request.Start) / double(request.FadeInEnd - request.Start);
                            const double b = double(next - request.Start) / double(request.FadeInEnd - request.Start);
                            factor = {(1 - a) + target * a, (1 - b) + target * b};
                            fading = true;
                        }
                        else if (target != 1 && start >= request.FadeOutStart && request.FadeOutStart < request.End)
                        {
                            const double a =
                                double(start - request.FadeOutStart) / double(request.End - request.FadeOutStart);
                            const double b =
                                double(next - request.FadeOutStart) / double(request.End - request.FadeOutStart);
                            factor = {target * (1 - a) + a, target * (1 - b) + b};
                            fading = true;
                        }
                        factors.push_back(factor);
                        numerators.push_back(request.Numerator);
                    }
                    if (physics)
                    {
                        uint32_t numerator = TimeScaleDenominator;
                        TimeScaleMathResult status;
                        if (!fading)
                            status = ComposeTimeScales({numerators.data(), numerators.size()}, numerator);
                        else
                        {
                            double mean = 0;
                            status = AverageLinearTimeScales({factors.data(), factors.size()}, mean);
                            if (status == TimeScaleMathResult::Success)
                                status = QuantizeTimeScale(mean, numerator);
                        }
                        if (status != TimeScaleMathResult::Success)
                            return Convert(status);
                        int64_t scaled = 0;
                        status = ScaleTimeNanoseconds(duration, numerator, carry, scaled);
                        if (status != TimeScaleMathResult::Success)
                            return Convert(status);
                        if (scaled > std::numeric_limits<int64_t>::max() - frame.PhysicsDeltaNanoseconds)
                            return TimeSystemResult::Overflow;
                        frame.PhysicsDeltaNanoseconds += scaled;
                    }
                    else
                    {
                        double mean = 0;
                        const auto status = AverageLinearTimeScales({factors.data(), factors.size()}, mean);
                        if (status != TimeScaleMathResult::Success)
                            return Convert(status);
                        const double delta = mean * (double(duration) / double(raw)) * clampedDt;
                        double& total = seconds[channel - static_cast<unsigned>(TimeChannel::World)];
                        total += delta;
                        if (!std::isfinite(total) || total > std::numeric_limits<float>::max())
                            return TimeSystemResult::Overflow;
                    }
                }
                start = next;
            }
        }
        frame.World = static_cast<float>(seconds[0]);
        frame.Animation = static_cast<float>(seconds[1]);
        frame.Particle = static_cast<float>(seconds[2]);
        frame.Audio = static_cast<float>(seconds[3]);
        m_Now = end;
        m_Frame = frame;
        m_PhysicsCarry = carry;
        for (size_t i = 0; i < m_Requests.size();)
            if (m_Requests[i].End <= m_Now)
                m_Requests.erase(m_Requests.begin() + i);
            else
                ++i;
        return TimeSystemResult::Success;
    }
} // namespace NorvesLib::Core::Engine
