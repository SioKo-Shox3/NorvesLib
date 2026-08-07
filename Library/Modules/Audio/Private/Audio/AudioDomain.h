#pragma once

#include "Audio/AudioBackend.h"
#include "Audio/IAudioModule.h"
#include "Audio/IAudioService.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Modules::Audio::Private
{
    class AudioServiceTestAccess
    {
    public:
        static bool PrepareVoiceGenerationWrap(IAudioService& service, VoiceHandle& handle);
        [[nodiscard]] static size_t GetVoiceSlotCount(const IAudioService& service);
        [[nodiscard]] static size_t GetMailboxAllocationCount(const IAudioService& service);
    };

    [[nodiscard]] Core::Container::TUniquePtr<IAudioService> CreateAudioServiceForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend);
    [[nodiscard]] Core::Container::TUniquePtr<IAudioModule> CreateAudioModuleForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend);
} // namespace NorvesLib::Modules::Audio::Private
