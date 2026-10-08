#pragma once
#include <cmath>
#include <cstdint>
namespace NorvesLib::Core::Engine
{
    enum class TimeChannel : uint8_t
    {
        Unscaled,
        World,
        Animation,
        Particle,
        Audio,
        Physics,
        Count
    };
    using TimeChannelMask = uint8_t;
    constexpr bool IsValidTimeChannel(TimeChannel channel)
    {
        return channel < TimeChannel::Count;
    }
    constexpr TimeChannelMask TimeChannelBit(TimeChannel channel)
    {
        return IsValidTimeChannel(channel) ? static_cast<TimeChannelMask>(1u << static_cast<unsigned>(channel)) : 0;
    }
    inline constexpr TimeChannelMask ScalableTimeChannels = 0x3e;
    struct FrameTimes
    {
        float Unscaled = 0, World = 0, Animation = 0, Particle = 0, Audio = 0;
        int64_t PhysicsDeltaNanoseconds = 0;
        float GetDelta(TimeChannel channel) const
        {
            switch (channel)
            {
            case TimeChannel::Unscaled:
                return Unscaled;
            case TimeChannel::World:
                return World;
            case TimeChannel::Animation:
                return Animation;
            case TimeChannel::Particle:
                return Particle;
            case TimeChannel::Audio:
                return Audio;
            case TimeChannel::Physics:
                return static_cast<float>(static_cast<double>(PhysicsDeltaNanoseconds) * 1e-9);
            default:
                return 0;
            }
        }
        bool IsValid() const
        {
            return std::isfinite(Unscaled) && Unscaled >= 0 && std::isfinite(World) && World >= 0 &&
                   std::isfinite(Animation) && Animation >= 0 && std::isfinite(Particle) && Particle >= 0 &&
                   std::isfinite(Audio) && Audio >= 0 && PhysicsDeltaNanoseconds >= 0;
        }
    };
} // namespace NorvesLib::Core::Engine
