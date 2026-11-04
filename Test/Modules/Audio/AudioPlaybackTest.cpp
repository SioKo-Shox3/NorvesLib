#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioService.h"
#include "Asset/AssetBlob.h"
#include "Container/PointerTypes.h"
#include "CoreTypes.h"

// Release でも検査と assert 内の API 呼び出しを省略しない。
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Modules::Audio;
    using NorvesLib::Test::Audio::FakeAudioBackend;

    TSharedPtr<AudioClipResource> MakeClip()
    {
        // ステレオ・16 ビットの 16 フレームで、途中のループ範囲を確保する。
        const Core::Asset::AssetBlob::ByteArray pcm(16 * 4, 0);
        const auto blob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(pcm.data(), pcm.size()),
            Core::Container::AnsiString("memory/playback"));
        return MakeShared<AudioClipResource>(blob, AudioPcmFormat{48000, 2, 16, 4}, 16);
    }

    void TestLoopRemainsPlayingUntilStopped()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);

        AudioVoiceDesc desc;
        desc.bLoop = true;
        desc.LoopBeginFrame = 4;
        desc.LoopLengthFrames = 8;
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), desc, handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        for (uint32_t index = 0; index < 600; ++index)
        {
            module->Tick();
        }

        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Playing);
        assert(fake->LastBufferDesc.bLoop);
        assert(fake->LastBufferDesc.LoopBeginFrame == desc.LoopBeginFrame);
        assert(fake->LastBufferDesc.LoopLengthFrames == desc.LoopLengthFrames);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestPausePreservesSubmittedBuffer()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);

        const auto pauseCount = fake->PauseCount;
        const auto resumeCount = fake->ResumeCount;
        const auto flushCount = fake->FlushCount;
        const auto submitCount = fake->SubmitCount;
        assert(module->PauseVoice(handle) == AudioResult::Success);
        assert(fake->PauseCount == pauseCount + 1);
        assert(fake->FlushCount == flushCount);
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Playing);
        assert(module->PauseVoice(handle) == AudioResult::AlreadyInState);
        assert(fake->PauseCount == pauseCount + 1);
        assert(module->ResumeVoice(handle) == AudioResult::Success);
        assert(fake->ResumeCount == resumeCount + 1);
        assert(fake->FlushCount == flushCount);
        assert(fake->SubmitCount == submitCount);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestQuarantinedVoiceDoesNotTouchBackend()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        fake->bFailFlush = true;
        assert(module->StopVoice(handle) == AudioResult::BackendFailure);
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Quarantined);

        const auto operationCount = fake->Operations.size();
        const auto volumeCount = fake->SetVolumeCount;
        const auto pitchCount = fake->SetFrequencyRatioCount;
        const auto panCount = fake->SetOutputGainsCount;
        const auto pauseCount = fake->PauseCount;
        const auto resumeCount = fake->ResumeCount;
        const auto flushCount = fake->FlushCount;
        assert(module->SetVoiceVolume(handle, 0.5f) == AudioResult::InvalidState);
        assert(module->SetVoicePitch(handle, 3.0f) == AudioResult::InvalidState);
        assert(module->SetVoicePan(handle, 0.5f) == AudioResult::InvalidState);
        assert(module->PauseVoice(handle) == AudioResult::InvalidState);
        assert(module->ResumeVoice(handle) == AudioResult::InvalidState);
        assert(fake->Operations.size() == operationCount);
        assert(fake->SetVolumeCount == volumeCount);
        assert(fake->SetFrequencyRatioCount == pitchCount);
        assert(fake->SetOutputGainsCount == panCount);
        assert(fake->PauseCount == pauseCount);
        assert(fake->ResumeCount == resumeCount);
        assert(fake->FlushCount == flushCount);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestDestroyedHandleIsStale()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->StopVoice(handle) == AudioResult::Success);
        fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->DestroyVoice(handle) == AudioResult::Success);

        assert(module->SetVoiceVolume(handle, 0.5f) == AudioResult::StaleHandle);
        assert(module->SetVoicePitch(handle, 3.0f) == AudioResult::StaleHandle);
        assert(module->SetVoicePan(handle, 0.5f) == AudioResult::StaleHandle);
        assert(module->PauseVoice(handle) == AudioResult::StaleHandle);
        assert(module->ResumeVoice(handle) == AudioResult::StaleHandle);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestPanUsesEqualPowerGains()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);

        constexpr float PanValues[] = {-1.0f, 0.0f, 0.5f, 1.0f};
        constexpr double Tolerance = 1e-5;
        const double pi = std::acos(-1.0);
        for (float pan : PanValues)
        {
            assert(module->SetVoicePan(handle, pan) == AudioResult::Success);
            const double angle = (static_cast<double>(pan) + 1.0) * pi / 4.0;
            const double leftGain = fake->LastLeftGain;
            const double rightGain = fake->LastRightGain;
            assert(std::abs(leftGain - std::cos(angle)) <= Tolerance);
            assert(std::abs(rightGain - std::sin(angle)) <= Tolerance);
            assert(std::abs(leftGain * leftGain + rightGain * rightGain - 1.0) <= Tolerance);
        }
        assert(module->Shutdown() == AudioResult::Success);
    }
}

int main()
{
    TestLoopRemainsPlayingUntilStopped();
    TestPausePreservesSubmittedBuffer();
    TestQuarantinedVoiceDoesNotTouchBackend();
    TestDestroyedHandleIsStale();
    TestPanUsesEqualPowerGains();
    std::cout << "AudioPlaybackTest: 合格\n";
    return 0;
}
