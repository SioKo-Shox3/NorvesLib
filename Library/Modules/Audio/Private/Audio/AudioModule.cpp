#include "Audio/IAudioModule.h"

#include "Audio/AudioBackendFactory.h"
#include "Audio/AudioDomain.h"

#include <utility>

namespace NorvesLib::Modules::Audio
{
    namespace
    {
        constexpr const char* kAudioModuleName = "NorvesAudioModule";
    }

    Core::Container::TUniquePtr<IAudioModule> CreateAudioModule()
    {
        auto backend = Private::CreatePlatformAudioBackend();
        if (!backend)
        {
            return {};
        }
        return Private::CreateAudioModuleForBackend(std::move(backend));
    }

    IAudioModule* RegisterAudioModule(Core::Module::ModuleRegistry& registry)
    {
        if (IAudioModule* existing = FindAudioModule(registry))
        {
            return existing;
        }
        return dynamic_cast<IAudioModule*>(registry.Register(CreateAudioModule()));
    }

    IAudioModule* FindAudioModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IAudioModule*>(
            registry.FindModule(Core::Identity(kAudioModuleName)));
    }
} // namespace NorvesLib::Modules::Audio
