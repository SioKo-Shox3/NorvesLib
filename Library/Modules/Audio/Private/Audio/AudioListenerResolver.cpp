#include "Audio/AudioListenerResolver.h"

#include "Audio/AudioListenerComponent.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/SceneProxy.h"

#include <cmath>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        Math::Vector3 NormalizeAxis(const Math::Vector3& axis, const Math::Vector3& defaultAxis) noexcept
        {
            const double x = axis.x;
            const double y = axis.y;
            const double z = axis.z;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (!std::isfinite(length) || length < 1.0e-6f)
            {
                return defaultAxis;
            }
            return {static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length)};
        }
    }

    void MainCameraAudioListenerSource::SetEngine(Core::Engine::Engine* engine) noexcept
    {
        m_Engine = engine;
    }

    bool MainCameraAudioListenerSource::TryGetListenerPose(AudioListenerState& outState) const
    {
        if (!m_Engine)
        {
            return false;
        }
        const auto& camera = m_Engine->GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        if (!camera.IsValid())
        {
            return false;
        }
        const AudioListenerState defaults;
        outState.Position = {camera.PositionX, camera.PositionY, camera.PositionZ};
        outState.Right = NormalizeAxis({camera.RightX, camera.RightY, camera.RightZ}, defaults.Right);
        outState.Up = NormalizeAxis({camera.UpX, camera.UpY, camera.UpZ}, defaults.Up);
        outState.Forward = NormalizeAxis({camera.ForwardX, camera.ForwardY, camera.ForwardZ}, defaults.Forward);
        return true;
    }

    AudioListenerComponent* SelectAudioListener(Core::Container::Span<AudioListenerComponent* const> listeners) noexcept
    {
        AudioListenerComponent* selected = nullptr;
        int32_t selectedPriority = 0;
        for (auto* listener : listeners)
        {
            if (!listener || !listener->IsActive())
            {
                continue;
            }
            const int32_t priority = listener->getPriority();
            if (!selected || priority > selectedPriority ||
                (priority == selectedPriority && listener->GetOwnerId() < selected->GetOwnerId()))
            {
                selected = listener;
                selectedPriority = priority;
            }
        }
        return selected;
    }

    // Entity の基底は CameraComponent と同じ規約を使い、前方は +Z 軸にワールド回転を掛けて求める。
    AudioListenerState MakeAudioListenerPose(const Math::Transform& worldTransform) noexcept
    {
        AudioListenerState pose;
        pose.Position = worldTransform.position;
        pose.Right = NormalizeAxis(worldTransform.rotation * Math::Vector3::Right, Math::Vector3::Right);
        pose.Up = NormalizeAxis(worldTransform.rotation * Math::Vector3::Up, Math::Vector3::Up);
        pose.Forward = NormalizeAxis(worldTransform.rotation * Math::Vector3::Forward, Math::Vector3::Forward);
        return pose;
    }

    AudioListenerState AudioListenerResolver::Resolve(const IAudioListenerSource& cameraSource,
                                                      Core::Container::Span<AudioListenerComponent* const> listeners,
                                                      float deltaSeconds)
    {
        const auto* selected = SelectAudioListener(listeners);
        const bool implicitCamera = selected == nullptr;
        const AudioListenerMode mode = implicitCamera ? AudioListenerMode::MainCamera : *selected->getMode();
        AudioListenerState pose;
        const char* reason = nullptr;
        if (mode != AudioListenerMode::Entity && !cameraSource.TryGetListenerPose(pose))
        {
            reason = "no_camera";
        }
        if (mode != AudioListenerMode::MainCamera)
        {
            const auto* owner = selected->GetOwner();
            if (!owner)
            {
                pose.Position = Math::Vector3::Zero;
                reason = "entity_missing";
            }
            else if (mode == AudioListenerMode::Hybrid)
            {
                pose.Position = owner->GetWorldTransform().position;
            }
            else
            {
                pose = MakeAudioListenerPose(owner->GetWorldTransform());
            }
        }
        if (reason && m_FallbackLogCount == 0)
        {
            LOG_WARNING("AUDIO_LISTENER fallback=origin reason=%s", reason);
            m_FallbackLogCount = 1;
        }

        pose.Velocity = Math::Vector3::Zero;
        if (m_bHasPreviousPosition && selected == m_PreviousSelection && implicitCamera == m_bPreviousWasCamera &&
            deltaSeconds > 0.0f && std::isfinite(deltaSeconds))
        {
            // 小さな正の時間でも逆数があふれないように、各成分の差と除算を倍精度で求める。
            const auto velocity = [deltaSeconds](float current, float previous)
            {
                return static_cast<float>((static_cast<double>(current) - previous) / deltaSeconds);
            };
            pose.Velocity = {velocity(pose.Position.x, m_PreviousPosition.x),
                             velocity(pose.Position.y, m_PreviousPosition.y),
                             velocity(pose.Position.z, m_PreviousPosition.z)};
        }
        m_PreviousPosition = pose.Position;
        m_PreviousSelection = selected;
        m_bPreviousWasCamera = implicitCamera;
        m_bHasPreviousPosition = true;
        return pose;
    }

    uint32_t AudioListenerResolver::GetFallbackLogCount() const noexcept
    {
        return m_FallbackLogCount;
    }

    void AudioListenerResolver::Reset() noexcept
    {
        m_PreviousPosition = Math::Vector3::Zero;
        m_PreviousSelection = nullptr;
        m_bHasPreviousPosition = false;
        m_bPreviousWasCamera = false;
    }
}
