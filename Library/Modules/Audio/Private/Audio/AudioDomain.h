#pragma once

#include "Audio/AudioBackend.h"
#include "Audio/IAudioModule.h"
#include "Audio/IAudioService.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Scene
{
    class AcousticTransmissionEvaluator;
}

namespace NorvesLib::Modules::Audio
{
    // 1 voice に渡せる PCM のバイト数の上限。全バックエンドで共通とし、XAudio2 の単一バッファの上限以下に保つ。
    inline constexpr uint64_t kAudioMaxVoicePcmBytes = 0x80000000ull;

    // サービスの CreateVoice と SoundSystem の 3D 再生開始時に、PCM のバイト数、ピッチの上限、開始フレーム、ループ区間を検査する。
    [[nodiscard]] bool IsPlayableVoiceDesc(uint64_t frameCount, uint64_t pcmBytes, const AudioVoiceDesc& desc) noexcept;
}

namespace NorvesLib::Modules::Audio::Private
{
    class AudioOcclusionUpdater;

    // モジュールがサービスの遮蔽更新器を借りるための内部の口。
    [[nodiscard]] AudioOcclusionUpdater* GetAudioServiceOcclusion(IAudioService& service);
    // モジュールがサービスの遮蔽更新器に評価器を渡すための内部の口。
    void SetAudioServiceAcousticsEvaluator(IAudioService& service, const Core::Scene::AcousticTransmissionEvaluator* evaluator);

    class AudioServiceTestAccess
    {
    public:
        static bool PrepareVoiceGenerationWrap(IAudioService& service, VoiceHandle& handle);
        [[nodiscard]] static size_t GetVoiceSlotCount(const IAudioService& service);
        [[nodiscard]] static size_t GetMailboxAllocationCount(const IAudioService& service);
    };

    [[nodiscard]] Core::Container::TUniquePtr<IAudioService> CreateAudioServiceForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend);
    [[nodiscard]] Core::Container::TUniquePtr<IAudioModule> CreateAudioModuleForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend, const AudioModuleOptions& options = {});
} // namespace NorvesLib::Modules::Audio::Private
