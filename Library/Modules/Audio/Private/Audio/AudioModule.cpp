#include "Audio/IAudioModule.h"

#include "Audio/AudioBackendFactory.h"
#include "Audio/AudioDomain.h"

namespace NorvesLib::Modules::Audio
{
    Core::Container::TUniquePtr<IAudioModule> CreateAudioModule(const AudioModuleOptions& options)
    {
        return Private::CreateAudioModuleForBackend(Private::CreatePlatformAudioBackend(), options);
    }

    IAudioModule* RegisterAudioModule(Core::Module::ModuleRegistry& registry, const AudioModuleOptions& options)
    {
        if (IAudioModule* existing = FindAudioModule(registry))
        {
            return existing;
        }
        return dynamic_cast<IAudioModule*>(registry.Register(CreateAudioModule(options)));
    }
} // namespace NorvesLib::Modules::Audio
