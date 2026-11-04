#include "Audio/AudioDomain.h"

#include "Audio/SoundSystem.h"
#include "CoreTypes.h"

#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        constexpr const char* kAudioModuleName = "NorvesAudioModule";

        class AudioServiceModule final : public IAudioModule
        {
        public:
            explicit AudioServiceModule(Core::Container::TUniquePtr<IAudioService> service)
                : m_Service(std::move(service))
            {
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
                return m_Service != nullptr && m_Service->Initialize() == AudioResult::Success;
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

        private:
            Core::Container::TUniquePtr<IAudioService> m_Service;
            Core::Container::TUniquePtr<SoundSystem> m_SoundSystem;
        };
    } // namespace

    Core::Container::TUniquePtr<IAudioModule> CreateAudioModuleForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend)
    {
        auto service = CreateAudioServiceForBackend(std::move(backend));
        if (!service)
        {
            return {};
        }
        return Core::Container::MakeUnique<AudioServiceModule>(std::move(service));
    }
} // namespace NorvesLib::Modules::Audio::Private
