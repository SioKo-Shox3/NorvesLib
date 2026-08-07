#pragma once

#include "Audio/AudioTypes.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Modules::Audio
{
    class AudioClipResource;

    /**
     * @brief Backend-neutral audio service driven on the GameThread.
     */
    class IAudioService
    {
    public:
        virtual ~IAudioService() = default;

        virtual AudioResult Initialize() = 0;
        virtual AudioResult CreateVoice(const Core::Container::TSharedPtr<AudioClipResource>& clip,
                                        VoiceHandle& outHandle) = 0;
        virtual AudioResult StartVoice(VoiceHandle handle) = 0;
        virtual AudioResult StopVoice(VoiceHandle handle) = 0;
        virtual AudioResult DestroyVoice(VoiceHandle handle) = 0;
        virtual AudioResult GetVoiceState(VoiceHandle handle, AudioVoiceState& outState) const = 0;
        virtual void Tick() = 0;
        virtual AudioResult Shutdown() = 0;
        [[nodiscard]] virtual AudioDiagnostics GetDiagnostics() const = 0;
    };
} // namespace NorvesLib::Modules::Audio
