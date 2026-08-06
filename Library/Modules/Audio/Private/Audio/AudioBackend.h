#pragma once

#include "Audio/AudioTypes.h"
#include "Container/Span.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio::Private
{
    enum class AudioBackendEventKind : uint8_t
    {
        Drained,
        VoiceError,
    };

    class IAudioBackendEventSink
    {
    public:
        virtual ~IAudioBackendEventSink() = default;
        virtual void EnqueueBackendEvent(VoiceHandle handle,
                                         uint64_t shutdownEpoch,
                                         AudioBackendEventKind kind) noexcept = 0;
    };

    /**
     * Stop is complete only after both consumption is stopped and queued buffers
     * have been flushed. The flags expose a partial adapter failure to the domain.
     */
    struct AudioBackendStopResult
    {
        AudioResult Result = AudioResult::BackendFailure;
        bool bVoiceStopped = false;
        bool bBuffersFlushed = false;
    };

    class IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;
        virtual AudioResult Initialize(IAudioBackendEventSink& eventSink, uint64_t shutdownEpoch) = 0;
        virtual AudioResult CreateVoice(VoiceHandle handle, const AudioPcmFormat& format) = 0;
        virtual AudioResult SubmitVoice(VoiceHandle handle, Core::Container::Span<const uint8_t> pcmBytes) = 0;
        virtual AudioResult StartVoice(VoiceHandle handle) = 0;
        virtual AudioBackendStopResult StopVoice(VoiceHandle handle) = 0;
        virtual void QuiesceCallbacks() = 0;
        virtual AudioResult DestroyVoice(VoiceHandle handle) = 0;
        virtual void Shutdown() = 0;
    };
} // namespace NorvesLib::Modules::Audio::Private
