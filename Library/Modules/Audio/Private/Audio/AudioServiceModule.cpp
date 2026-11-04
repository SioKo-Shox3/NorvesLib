#include "Audio/AudioDomain.h"

#include "Audio/NullAudioBackend.h"
#include "Audio/SoundSystem.h"
#include "CoreTypes.h"
#include "Logging/LogMacros.h"

#include <utility>

namespace NorvesLib::Modules::Audio
{
    namespace
    {
        constexpr const char* kAudioModuleName = "NorvesAudioModule";

        const char* GetFailureReason(AudioResult result)
        {
            switch (result)
            {
            case AudioResult::Success:
                return "success";
            case AudioResult::AlreadyInState:
                return "already_in_state";
            case AudioResult::InvalidState:
                return "invalid_state";
            case AudioResult::StaleHandle:
                return "stale_handle";
            case AudioResult::BackendFailure:
                return "backend_failure";
            case AudioResult::Rejected:
                return "rejected";
            }
            return "unknown_audio_result";
        }
    } // namespace

    IAudioModule* FindAudioModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IAudioModule*>(registry.FindModule(Core::Identity(kAudioModuleName)));
    }
} // namespace NorvesLib::Modules::Audio

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        class AudioServiceModule final : public IAudioModule
        {
        public:
            explicit AudioServiceModule(Core::Container::TUniquePtr<IAudioService> service,
                                        const AudioModuleOptions& options, bool bBackendCreateFailed)
                : m_Service(std::move(service)), m_Options(options)
            {
                if (bBackendCreateFailed)
                {
                    SetFallbackStatus("backend_create_failed");
                }
                if (m_Service)
                {
                    m_SoundSystem = Core::Container::MakeUnique<SoundSystem>(*m_Service, Random::Generator{});
                }
            }

            Core::Identity GetModuleId() const override
            {
                return Core::Identity(kAudioModuleName);
            }

            const char* GetName() const override
            {
                return kAudioModuleName;
            }

            bool Install(Core::Engine::Engine&) override
            {
                return m_Service != nullptr;
            }

            bool Initialize() override
            {
                if (!m_Service || m_bInitialized || m_bShutdown)
                {
                    return false;
                }
                AudioResult result = m_Service->Initialize();
                if (result != AudioResult::Success && m_Options.bAllowNullBackendFallback &&
                    m_BackendStatus.Kind != AudioBackendKind::Null)
                {
                    const char* reason = GetFailureReason(result);
                    // SoundSystem の参照先を破棄する前に、参照する側を終了して外す。
                    m_SoundSystem->Shutdown();
                    m_SoundSystem.reset();
                    m_Service->Shutdown();
                    m_Service = CreateAudioServiceForBackend(Core::Container::MakeUnique<NullAudioBackend>());
                    m_SoundSystem = Core::Container::MakeUnique<SoundSystem>(*m_Service, Random::Generator{});
                    result = m_Service->Initialize();
                    if (result == AudioResult::Success)
                    {
                        SetFallbackStatus(reason);
                    }
                }
                if (result != AudioResult::Success)
                {
                    return false;
                }
                m_bInitialized = true;
                if (m_BackendStatus.bFellBackToNull)
                {
                    LOG_WARNING("AUDIO_BACKEND fallback=null reason=%s", m_FallbackReason);
                }
                return true;
            }

            void Tick(float deltaSeconds) override
            {
                if (m_Service)
                {
                    m_Service->Tick();
                    m_SoundSystem->Update(deltaSeconds);
                }
            }

            void Shutdown() override
            {
                m_bShutdown = true;
                if (m_Service)
                {
                    m_SoundSystem->Shutdown();
                    m_Service->Shutdown();
                }
            }

            void Uninstall(Core::Engine::Engine&) override
            {
            }

            IAudioService& GetAudioService() override
            {
                return *m_Service;
            }

            ISoundSystem& GetSoundSystem() override
            {
                return *m_SoundSystem;
            }

            const AudioBackendStatus& GetBackendStatus() const override
            {
                return m_BackendStatus;
            }

        private:
            void SetFallbackStatus(const char* reason)
            {
                m_BackendStatus.Kind = AudioBackendKind::Null;
                m_BackendStatus.bFellBackToNull = true;
                m_BackendStatus.Reason = reason;
                m_FallbackReason = reason;
            }

            Core::Container::TUniquePtr<IAudioService> m_Service;
            Core::Container::TUniquePtr<SoundSystem> m_SoundSystem;
            AudioModuleOptions m_Options;
            AudioBackendStatus m_BackendStatus;
            const char* m_FallbackReason = "";
            bool m_bInitialized = false;
            bool m_bShutdown = false;
        };
    } // namespace

    Core::Container::TUniquePtr<IAudioModule> CreateAudioModuleForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend, const AudioModuleOptions& options)
    {
        const bool bBackendCreateFailed = backend == nullptr;
        if (bBackendCreateFailed && options.bAllowNullBackendFallback)
        {
            backend = Core::Container::MakeUnique<NullAudioBackend>();
        }
        auto service = CreateAudioServiceForBackend(std::move(backend));
        if (!service)
        {
            return {};
        }
        return Core::Container::MakeUnique<AudioServiceModule>(std::move(service), options, bBackendCreateFailed);
    }
} // namespace NorvesLib::Modules::Audio::Private
