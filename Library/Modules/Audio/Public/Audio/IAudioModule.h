#pragma once

#include "Audio/IAudioService.h"
#include "Audio/ISoundSystem.h"
#include "Container/PointerTypes.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::Audio
{
    /**
     * @brief 音声サービスと論理サウンド層を ModuleRegistry の寿命に結び付ける。
     */
    class IAudioModule : public Core::Module::IModule
    {
    public:
        virtual ~IAudioModule() = default;
        [[nodiscard]] virtual IAudioService& GetAudioService() = 0;
        [[nodiscard]] virtual ISoundSystem& GetSoundSystem() = 0;
    };

    [[nodiscard]] Core::Container::TUniquePtr<IAudioModule> CreateAudioModule();
    IAudioModule* RegisterAudioModule(Core::Module::ModuleRegistry& registry);
    IAudioModule* FindAudioModule(Core::Module::ModuleRegistry& registry);
} // namespace NorvesLib::Modules::Audio
