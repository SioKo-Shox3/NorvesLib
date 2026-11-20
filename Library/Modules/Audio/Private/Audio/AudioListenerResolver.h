#pragma once

#include "Audio/AudioListener.h"
#include "Container/Span.h"
#include "Math/Transform.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Modules::Audio
{
    class AudioListenerComponent;

    namespace Private
    {
        class MainCameraAudioListenerSource final : public IAudioListenerSource
        {
        public:
            void SetEngine(Core::Engine::Engine* engine) noexcept;
            [[nodiscard]] bool TryGetListenerPose(AudioListenerState& outState) const override;

        private:
            Core::Engine::Engine* m_Engine = nullptr;
        };

        [[nodiscard]] AudioListenerComponent* SelectAudioListener(Core::Container::Span<AudioListenerComponent* const> listeners) noexcept;
        [[nodiscard]] AudioListenerState MakeAudioListenerPose(const Math::Transform& worldTransform) noexcept;

        class AudioListenerResolver
        {
        public:
            /** 選んだリスナーの位置・基底と、位置の差分を経過時間で割った速度を返す。GameThread 専用。 */
            AudioListenerState Resolve(const IAudioListenerSource& cameraSource,
                                       Core::Container::Span<AudioListenerComponent* const> listeners,
                                       float deltaSeconds);
            [[nodiscard]] uint32_t GetFallbackLogCount() const noexcept;
            void Reset() noexcept;

        private:
            Math::Vector3 m_PreviousPosition;
            const AudioListenerComponent* m_PreviousSelection = nullptr;
            bool m_bHasPreviousPosition = false;
            bool m_bPreviousWasCamera = false;
            uint32_t m_FallbackLogCount = 0;
        };
    }
}
