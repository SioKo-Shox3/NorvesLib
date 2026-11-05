#pragma once

#include "Audio/IAudioService.h"
#include "Audio/ISoundSystem.h"
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::Audio
{
    struct AudioModuleOptions
    {
        bool bAllowNullBackendFallback = false;
    };

    enum class AudioBackendKind : uint8_t
    {
        Platform,
        Null,
    };

    struct AudioBackendStatus
    {
        AudioBackendKind Kind = AudioBackendKind::Platform;
        bool bFellBackToNull = false;
        Core::Container::String Reason;
    };

    /**
     * @brief 音声サービスと論理サウンド層を ModuleRegistry の寿命に結び付ける。
     */
    class IAudioModule : public Core::Module::IModule
    {
    public:
        virtual ~IAudioModule() = default;
        [[nodiscard]] virtual IAudioService& GetAudioService() = 0;
        [[nodiscard]] virtual ISoundSystem& GetSoundSystem() = 0;
        [[nodiscard]] virtual const AudioBackendStatus& GetBackendStatus() const = 0;
    };

    [[nodiscard]] Core::Container::TUniquePtr<IAudioModule> CreateAudioModule(const AudioModuleOptions& options = {});
    IAudioModule* RegisterAudioModule(Core::Module::ModuleRegistry& registry, const AudioModuleOptions& options = {});
    IAudioModule* FindAudioModule(Core::Module::ModuleRegistry& registry);
} // namespace NorvesLib::Modules::Audio
