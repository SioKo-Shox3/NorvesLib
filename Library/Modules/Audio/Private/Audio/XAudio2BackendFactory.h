#pragma once

#include "Audio/AudioBackend.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Modules::Audio::Private
{
    [[nodiscard]] Core::Container::TUniquePtr<IAudioBackend> CreatePlatformAudioBackend();
} // namespace NorvesLib::Modules::Audio::Private
