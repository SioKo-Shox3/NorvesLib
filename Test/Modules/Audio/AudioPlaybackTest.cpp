#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioService.h"
#include "Audio/SoundSystem.h"
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
    using NorvesLib::Test::Audio::FakeAudioOperation;

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

    SoundId RegisterTestSound(Private::SoundSystem& sound)
    {
        SoundDefinition definition;
        definition.Id = SoundId("AudioPlaybackTest.Sound");
        definition.Clips.push_back(MakeClip());
        definition.VolumeDb = 0.0f;
        definition.VolumeJitterDb = 0.0f;
        definition.PitchJitterSemitones = 0.0f;
        const SoundId id = definition.Id;
        assert(sound.RegisterDefinition(std::move(definition)) == AudioResult::Success);
        return id;
    }

    void TestFadeOutDurationAndDrainedLifetime()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;

        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
        assert(fake->CreateCount == 1);
        const VoiceHandle voice = Private::SoundSystemTestAccess::GetVoiceHandle(sound, handle);
        assert(voice.IsValid());
        assert(fake->LastVolume == 1.0f);
        const uint32_t stopBefore = fake->StopCount;
        const uint32_t destroyBefore = fake->DestroyCount;
        assert(sound.Stop(handle, SoundFade{2.0f, SoundFadeCurve::Linear}) == AudioResult::Success);

        float previous = fake->LastVolume;
        float samples[3] = {};
        uint32_t zeroFrame = 0;
        for (uint32_t frame = 1; frame <= 122; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 60.0f);
            const float volume = fake->LastVolume;
            assert(volume <= previous);
            assert(volume >= 0.0f);
            if (frame == 30 || frame == 60 || frame == 90)
            {
                samples[frame / 30 - 1] = volume;
            }
            if (volume == 0.0f && zeroFrame == 0)
            {
                zeroFrame = frame;
            }
            assert(fake->StopCount == stopBefore + (zeroFrame != 0 ? 1u : 0u));
            assert(fake->DestroyCount == destroyBefore);
            previous = volume;
        }
        assert(zeroFrame >= 119 && zeroFrame <= 121);
        fake->Emit(voice, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        service->Tick();
        sound.Update(1.0f / 60.0f);
        assert(fake->StopCount == stopBefore + 1);
        assert(fake->DestroyCount == destroyBefore + 1);
        assert(service->GetDiagnostics().ActiveVoiceCount == 0);
        assert(!sound.IsPlaying(handle));
        assert(sound.Stop(handle, SoundFade{2.0f, SoundFadeCurve::Linear}) == AudioResult::StaleHandle);

        SoundInstanceHandle slowerHandle;
        assert(sound.PlayGlobal(id, params, slowerHandle) == AudioResult::Success);
        assert(fake->CreateCount == 2);
        const VoiceHandle slowerVoice = Private::SoundSystemTestAccess::GetVoiceHandle(sound, slowerHandle);
        assert(slowerVoice.IsValid());
        const uint32_t slowerStopBefore = fake->StopCount;
        assert(sound.Stop(slowerHandle, SoundFade{2.0f, SoundFadeCurve::Linear}) == AudioResult::Success);

        previous = fake->LastVolume;
        zeroFrame = 0;
        for (uint32_t frame = 1; frame <= 62; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 30.0f);
            const float volume = fake->LastVolume;
            assert(volume <= previous);
            assert(volume >= 0.0f);
            if (frame == 15 || frame == 30 || frame == 45)
            {
                assert(std::abs(volume - samples[frame / 15 - 1]) <= 1e-4f);
            }
            if (volume == 0.0f && zeroFrame == 0)
            {
                zeroFrame = frame;
            }
            assert(fake->StopCount == slowerStopBefore + (zeroFrame != 0 ? 1u : 0u));
            previous = volume;
        }
        assert(zeroFrame >= 59 && zeroFrame <= 61);
        fake->Emit(slowerVoice, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        service->Tick();
        sound.Update(1.0f / 30.0f);
        assert(fake->DestroyCount == destroyBefore + 2);
        assert(service->GetDiagnostics().ActiveVoiceCount == 0);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestLinearFadeIn()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;
        params.FadeIn = SoundFade{1.0f, SoundFadeCurve::Linear};

        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
        assert(fake->LastVolume == 0.0f);
        float previous = fake->LastVolume;
        for (uint32_t frame = 1; frame <= 60; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 60.0f);
            const float volume = fake->LastVolume;
            assert(volume >= previous);
            assert(volume - previous <= 1.0f / 60.0f + 1e-4f);
            assert(volume <= 1.0f);
            assert(std::abs(volume - static_cast<float>(frame) / 60.0f) <= 1e-4f);
            previous = volume;
        }
        assert(std::abs(fake->LastVolume - 1.0f) <= 1e-4f);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestEqualPowerFadeIn()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;
        params.FadeIn = SoundFade{1.0f, SoundFadeCurve::EqualPower};

        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
        assert(fake->LastVolume == 0.0f);
        for (uint32_t frame = 1; frame <= 60; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 60.0f);
            const float volume = fake->LastVolume;
            const float elapsed = static_cast<float>(frame) / 60.0f;
            assert(volume >= 0.0f && volume <= 1.0f);
            assert(std::abs(volume * volume - elapsed) <= 1e-3f);
        }
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestMinimumFadeClamp()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;

        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
        const uint64_t clampedBefore = sound.GetDiagnostics().ClampedFadeCount;
        const uint32_t stopBefore = fake->StopCount;
        assert(sound.Stop(handle, SoundFade{0.01f, SoundFadeCurve::Linear}) == AudioResult::Success);
        const SoundSystemDiagnostics diagnostics = sound.GetDiagnostics();
        assert(diagnostics.ClampedFadeCount == clampedBefore + 1);
        assert(std::abs(diagnostics.LastClampedFadeRequestedSeconds - 0.01f) <= 1e-6f);
        assert(std::abs(diagnostics.LastClampedFadeAppliedSeconds - 2.0f / 60.0f) <= 1e-6f);

        service->Tick();
        sound.Update(1.0f / 60.0f);
        assert(fake->LastVolume > 0.0f);
        assert(fake->StopCount == stopBefore);
        service->Tick();
        sound.Update(1.0f / 60.0f);
        assert(fake->LastVolume == 0.0f);
        assert(fake->StopCount == stopBefore + 1);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestGenerationWrapRejectsOldHandles()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;

        SoundInstanceHandle initialHandle;
        assert(sound.PlayGlobal(id, params, initialHandle) == AudioResult::Success);
        assert(initialHandle.IsValid());
        assert(initialHandle.Generation == 1);
        SoundInstanceHandle maximumHandle = initialHandle;
        assert(Private::SoundSystemTestAccess::PrepareInstanceGenerationWrap(sound, maximumHandle));
        assert(maximumHandle.Generation == UINT32_MAX);
        const VoiceHandle voice = Private::SoundSystemTestAccess::GetVoiceHandle(sound, maximumHandle);
        assert(voice.IsValid());
        assert(sound.Stop(maximumHandle, SoundFade{2.0f / 60.0f, SoundFadeCurve::Linear}) == AudioResult::Success);
        for (uint32_t frame = 0; frame < 2; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 60.0f);
        }
        assert(fake->StopCount == 1);
        fake->Emit(voice, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        service->Tick();
        sound.Update(1.0f / 60.0f);
        assert(fake->DestroyCount == 1);

        SoundInstanceHandle replacementHandle;
        assert(sound.PlayGlobal(id, params, replacementHandle) == AudioResult::Success);
        assert(fake->CreateCount == 2);
        assert(replacementHandle.IsValid());
        assert(replacementHandle.Generation != 0);
        assert(replacementHandle.Index != initialHandle.Index || replacementHandle.Generation == 1);
        assert(sound.SetVolume(maximumHandle, 0.5f) == AudioResult::StaleHandle);
        assert(sound.Stop(maximumHandle, SoundFade{2.0f, SoundFadeCurve::Linear}) == AudioResult::StaleHandle);
        const AudioResult initialVolumeResult = sound.SetVolume(initialHandle, 0.5f);
        const AudioResult initialStopResult = sound.Stop(initialHandle, SoundFade{2.0f, SoundFadeCurve::Linear});
        if (initialVolumeResult != AudioResult::StaleHandle || initialStopResult != AudioResult::StaleHandle)
        {
            std::cerr << "古いハンドルの戻り値: SetVolume=" << static_cast<int>(initialVolumeResult)
                      << ", Stop=" << static_cast<int>(initialStopResult) << "\n";
        }
        assert(initialVolumeResult == AudioResult::StaleHandle);
        assert(initialStopResult == AudioResult::StaleHandle);
        assert(sound.IsPlaying(replacementHandle));
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestShutdownDuringFade()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id = RegisterTestSound(sound);
        SoundPlayParams params;
        params.Volume = 1.0f;

        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
        assert(sound.Stop(handle, SoundFade{2.0f, SoundFadeCurve::Linear}) == AudioResult::Success);
        for (uint32_t frame = 0; frame < 30; ++frame)
        {
            service->Tick();
            sound.Update(1.0f / 60.0f);
        }
        assert(fake->LastVolume > 0.0f && fake->LastVolume < 1.0f);
        assert(fake->StopCount == 0);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
        assert(!fake->Operations.empty());
        assert(fake->Operations.back() == FakeAudioOperation::Shutdown);
        assert(service->GetDiagnostics().ActiveVoiceCount == 0);
        SoundInstanceHandle afterShutdown;
        assert(sound.PlayGlobal(id, params, afterShutdown) == AudioResult::InvalidState);
    }

}

int main()
{
    TestLoopRemainsPlayingUntilStopped();
    TestPausePreservesSubmittedBuffer();
    TestQuarantinedVoiceDoesNotTouchBackend();
    TestDestroyedHandleIsStale();
    TestPanUsesEqualPowerGains();
    TestFadeOutDurationAndDrainedLifetime();
    TestLinearFadeIn();
    TestEqualPowerFadeIn();
    TestMinimumFadeClamp();
    TestShutdownDuringFade();
    TestGenerationWrapRejectsOldHandles();
    std::cout << "AudioPlaybackTest: 合格\n";
    return 0;
}
