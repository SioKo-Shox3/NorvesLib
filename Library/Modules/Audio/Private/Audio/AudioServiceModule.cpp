#include "Audio/AudioDomain.h"
#include "Audio/AudioComponent.h"

#include "Audio/AudioListenerComponent.h"
#include "Audio/AudioListenerResolver.h"
#include "Audio/AudioMixerDomain.h"
#include "Audio/AudioSpatialHost.h"
#include "Audio/NullAudioBackend.h"
#include "Audio/SoundSystem.h"
#include "CoreTypes.h"
#include "Logging/LogMacros.h"

#include <algorithm>
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
        template <typename T>
        void RegisterComponent(Core::Container::VariableArray<T*>& components, T& component)
        {
            if (std::find(components.begin(), components.end(), &component) == components.end())
            {
                components.push_back(&component);
            }
        }

        template <typename T>
        void UnregisterComponent(Core::Container::VariableArray<T*>& components, T& component)
        {
            const auto it = std::find(components.begin(), components.end(), &component);
            if (it != components.end())
            {
                components.erase(it);
            }
        }

        class AudioServiceModule final : public IAudioModule, public IAudioSpatialHost
        {
        public:
            explicit AudioServiceModule(Core::Container::TUniquePtr<IAudioService> service,
                                        const AudioModuleOptions& options, bool bBackendCreateFailed)
                : m_Service(std::move(service)), m_Mixer(Core::Container::MakeUnique<AudioMixerDomain>()), m_Options(options)
            {
                if (bBackendCreateFailed)
                {
                    SetFallbackStatus("backend_create_failed");
                }
                m_Mixer->BindService(m_Service.get());
                if (m_Service)
                {
                    m_SoundSystem = Core::Container::MakeUnique<SoundSystem>(*m_Service, Random::Generator{});
                    m_SoundSystem->BindMixer(m_Mixer.get());
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

            bool Install(Core::Engine::Engine& engine) override
            {
                if (!m_Service)
                {
                    return false;
                }
                m_Engine = &engine;
                m_CameraListenerSource.SetEngine(&engine);
                return true;
            }

            void RegisterReflectedTypes() override
            {
                Core::ClassRegistry::Get().RegisterClass(AudioComponent::StaticClass());
                Core::ClassRegistry::Get().RegisterClass(AudioListenerComponent::StaticClass());
            }

            bool Initialize() override
            {
                if (!m_Service || m_bInitialized || m_bShutdown)
                {
                    return false;
                }
                bool bServiceInitialized = false;
                AudioResult result = InitializeServiceAndBuses(bServiceInitialized);
                if (result != AudioResult::Success && m_Options.bAllowNullBackendFallback &&
                    m_BackendStatus.Kind != AudioBackendKind::Null)
                {
                    const char* reason = bServiceInitialized ? "bus_setup_failed" : GetFailureReason(result);
                    // SoundSystem の参照先を破棄する前に、参照する側を終了して外す。
                    m_SoundSystem->Shutdown();
                    m_SoundSystem.reset();
                    m_Mixer->BindService(nullptr);
                    m_Service->Shutdown();
                    m_Service = CreateAudioServiceForBackend(Core::Container::MakeUnique<NullAudioBackend>());
                    m_SoundSystem = Core::Container::MakeUnique<SoundSystem>(*m_Service, Random::Generator{});
                    m_SoundSystem->BindMixer(m_Mixer.get());
                    result = InitializeServiceAndBuses(bServiceInitialized);
                    if (result == AudioResult::Success)
                    {
                        SetFallbackStatus(reason);
                    }
                }
                if (result != AudioResult::Success)
                {
                    if (bServiceInitialized)
                    {
                        m_Mixer->BindService(nullptr);
                        m_Service->Shutdown();
                    }
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
                    const AudioListenerState listener = m_ListenerResolver.Resolve(m_CameraListenerSource,
                        Core::Container::Span<AudioListenerComponent* const>(m_Listeners), deltaSeconds);
                    m_SoundSystem->SetListener(listener);
                    for (AudioComponent* emitter : m_SpatialEmitters)
                    {
                        emitter->SyncSpatialEmitter(*m_SoundSystem);
                    }
                    m_SoundSystem->Update(deltaSeconds);
                }
            }

            void Shutdown() override
            {
                m_bShutdown = true;
                m_Listeners.clear();
                m_SpatialEmitters.clear();
                m_ListenerResolver.Reset();
                if (m_Service)
                {
                    m_SoundSystem->Shutdown();
                    m_Mixer->Shutdown();
                    m_Service->Shutdown();
                }
            }

            void Uninstall(Core::Engine::Engine& engine) override
            {
                if (m_Engine == &engine)
                {
                    m_Engine = nullptr;
                    m_CameraListenerSource.SetEngine(nullptr);
                }
            }

            void RegisterListener(AudioListenerComponent& listener) override
            {
                RegisterComponent(m_Listeners, listener);
            }

            void UnregisterListener(AudioListenerComponent& listener) override
            {
                UnregisterComponent(m_Listeners, listener);
            }

            void RegisterSpatialEmitter(AudioComponent& emitter) override
            {
                RegisterComponent(m_SpatialEmitters, emitter);
            }

            void UnregisterSpatialEmitter(AudioComponent& emitter) override
            {
                UnregisterComponent(m_SpatialEmitters, emitter);
            }

            IAudioService& GetAudioService() override
            {
                return *m_Service;
            }

            ISoundSystem& GetSoundSystem() override
            {
                return *m_SoundSystem;
            }

            IAudioMixer& GetMixer() override
            {
                return *m_Mixer;
            }

            const AudioBackendStatus& GetBackendStatus() const override
            {
                return m_BackendStatus;
            }

        private:
            AudioResult InitializeServiceAndBuses(bool& bServiceInitialized)
            {
                AudioResult result = m_Service->Initialize();
                bServiceInitialized = result == AudioResult::Success;
                if (!bServiceInitialized)
                {
                    return result;
                }
                m_Mixer->BindService(m_Service.get());
                result = m_Mixer->ReapplyBackendState();
                if (result != AudioResult::Success)
                {
                    LOG_WARNING("AUDIO_MIXER bus_setup=failed result=%d", static_cast<int>(result));
                }
                return result;
            }

            void SetFallbackStatus(const char* reason)
            {
                m_BackendStatus.Kind = AudioBackendKind::Null;
                m_BackendStatus.bFellBackToNull = true;
                m_BackendStatus.Reason = reason;
                m_FallbackReason = reason;
            }

            Core::Container::TUniquePtr<IAudioService> m_Service;
            Core::Container::TUniquePtr<AudioMixerDomain> m_Mixer;
            Core::Container::TUniquePtr<SoundSystem> m_SoundSystem;
            Core::Engine::Engine* m_Engine = nullptr;
            MainCameraAudioListenerSource m_CameraListenerSource;
            AudioListenerResolver m_ListenerResolver;
            Core::Container::VariableArray<AudioListenerComponent*> m_Listeners;
            Core::Container::VariableArray<AudioComponent*> m_SpatialEmitters;
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
