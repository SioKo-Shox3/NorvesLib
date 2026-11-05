#pragma once

#include "Audio/ISoundSystem.h"
#include "Component/Component.h"

namespace NorvesLib::Modules::Audio
{
    class IAudioModule;

    /**
     * @brief Entity に属する 2D 音声を再生する。全操作は GameThread 専用。
     * @details 所有キーは初期化時に採番する 0 以外の連番で、最上位ビットを 0 に保つ。
     * 別の Component と GameMode のキーには重ならず、使い終えたキーは再利用しない。
     */
    class AudioComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(AudioComponent, Core::Component::Component)

    public:
        AudioComponent();
        AudioComponent(const AudioComponent&) = delete;
        AudioComponent& operator=(const AudioComponent&) = delete;

        void Initialize() override;
        void Finalize() override;

        AudioResult Play(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle);
        AudioResult Stop(SoundInstanceHandle handle, const SoundFade& fade);
        AudioResult StopAll(const SoundFade& fade);

        PROPERTY(Core::Container::String, SoundName)
        PROPERTY(bool, bAutoPlay)
        PROPERTY(float, Volume)
        PROPERTY(float, PitchSemitones)

    private:
        [[nodiscard]] IAudioModule* FindRegisteredModule() const;
        void PruneHandles(ISoundSystem& soundSystem);

        IAudioModule* m_AudioModule = nullptr;
        uint64_t m_OwnerKey = 0;
        Core::Container::VariableArray<SoundInstanceHandle> m_Handles;
    };
} // namespace NorvesLib::Modules::Audio
