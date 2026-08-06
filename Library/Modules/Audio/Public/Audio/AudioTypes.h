#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    enum class AudioResult : uint8_t
    {
        Success,
        AlreadyInState,
        InvalidState,
        StaleHandle,
        BackendFailure,
    };

    enum class AudioVoiceState : uint8_t
    {
        Created = 0,
        Submitted = 1,
        Playing = 2,
        Stopping = 3,
        Drained = 4,
        Destroyed = 5,
        Quarantined = 6,
    };

    struct VoiceHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Index != InvalidIndex && Generation != 0;
        }

        [[nodiscard]] constexpr bool operator==(const VoiceHandle& other) const noexcept
        {
            return Index == other.Index && Generation == other.Generation;
        }
    };

    struct AudioPcmFormat
    {
        uint32_t SampleRate = 0;
        uint16_t ChannelCount = 0;
        uint16_t BitsPerSample = 0;
        uint16_t BlockAlignment = 0;

        [[nodiscard]] constexpr bool IsSupported() const noexcept
        {
            return (SampleRate == 44100 || SampleRate == 48000) &&
                   (ChannelCount == 1 || ChannelCount == 2) &&
                   BitsPerSample == 16 &&
                   BlockAlignment == ChannelCount * sizeof(int16_t);
        }
    };

    struct AudioDiagnostics
    {
        uint32_t ActiveVoiceCount = 0;
        size_t PendingEventCount = 0;
        uint32_t InFlightCallbackCount = 0;
        uint64_t StaleEventCount = 0;
        uint64_t LateEventCount = 0;
        // Duplicate terminal callbacks coalesced behind a pending event for the same voice.
        uint64_t DroppedEventCount = 0;
        uint32_t QuarantinedVoiceCount = 0;
    };
} // namespace NorvesLib::Modules::Audio
