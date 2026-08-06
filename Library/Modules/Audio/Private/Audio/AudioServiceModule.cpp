#include "Audio/AudioDomain.h"

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

            void Tick(float) override
            {
                if (m_Service)
                {
                    m_Service->Tick();
                }
            }

            void Shutdown() override
            {
                if (m_Service)
                {
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

        private:
            Core::Container::TUniquePtr<IAudioService> m_Service;
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
