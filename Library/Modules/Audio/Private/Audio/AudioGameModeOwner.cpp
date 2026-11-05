#include "Audio/AudioGameModeOwner.h"

#include "Audio/IAudioModule.h"

#include <utility>

namespace NorvesLib::Modules::Audio
{
    uint64_t GetGameModeOwnerKey(Core::GameMode::GameModeId id)
    {
        constexpr uint64_t modeBit = 1ull << 63;
        return modeBit | (id.GetHash() & (modeBit - 1));
    }

    AudioGameModeOwner::AudioGameModeOwner(Core::GameMode::GameModeId id,
                                         Core::Container::TUniquePtr<Core::GameMode::IGameMode> mode)
        : m_Id(id), m_Mode(std::move(mode))
    {
    }

    Core::GameMode::GameModeEnterResult AudioGameModeOwner::Enter(Core::GameMode::GameModeContext& ctx)
    {
        return m_Mode->Enter(ctx);
    }

    void AudioGameModeOwner::Tick(Core::GameMode::GameModeContext& ctx, float deltaTime)
    {
        m_Mode->Tick(ctx, deltaTime);
    }

    void AudioGameModeOwner::Leave(Core::GameMode::GameModeContext& ctx, Core::GameMode::GameModeExitReason reason)
    {
        m_Mode->Leave(ctx, reason);
        if (auto* audio = FindAudioModule(Core::Module::GetModuleRegistry()))
        {
            (void)audio->GetSoundSystem().StopOwner(GetGameModeOwnerKey(m_Id), SoundFade{});
        }
    }

    void AudioGameModeOwner::Suspend(Core::GameMode::GameModeContext& ctx)
    {
        m_Mode->Suspend(ctx);
    }

    void AudioGameModeOwner::Resume(Core::GameMode::GameModeContext& ctx)
    {
        m_Mode->Resume(ctx);
    }

    const char* AudioGameModeOwner::GetDebugName() const
    {
        return m_Mode->GetDebugName();
    }

    Core::GameMode::GameModeRegistry::Creator WrapGameModeCreatorWithAudioOwner(
        Core::GameMode::GameModeId id, Core::GameMode::GameModeRegistry::Creator creator)
    {
        return [id, creator = std::move(creator)](const Core::GameMode::GameModeParams& params)
            -> Core::Container::TUniquePtr<Core::GameMode::IGameMode>
        {
            auto mode = creator(params);
            if (!mode)
            {
                return {};
            }
            return Core::Container::MakeUnique<AudioGameModeOwner>(id, std::move(mode));
        };
    }
} // namespace NorvesLib::Modules::Audio
