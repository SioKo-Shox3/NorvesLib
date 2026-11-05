#include "FakeAudioBackend.h"

#include "Audio/AudioClipLoader.h"
#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioService.h"
#include "Audio/SoundSystem.h"
#include "Asset/AssetBlob.h"
#include "Asset/AssetSystem.h"
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
#include <Windows.h>

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

    void TestLooseWaveClipLoading()
    {
        wchar_t temporary[32768]{};
        const DWORD length = GetTempPathW(32768, temporary);
        assert(length > 0 && length < 32768);
        wchar_t directory[32768]{};
        assert(GetTempFileNameW(temporary, L"nva", 0, directory) != 0);
        assert(DeleteFileW(directory));
        assert(CreateDirectoryW(directory, nullptr));
        const Core::Container::WideString root(directory);
        const int utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory, -1,
                                                nullptr, 0, nullptr, nullptr);
        assert(utf8Size > 0);
        VariableArray<char> utf8Root(utf8Size);
        assert(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory, -1,
                                  utf8Root.data(), utf8Size, nullptr, nullptr) == utf8Size);

        const auto writeFile = [&root](const wchar_t* name, const Core::Asset::AssetBlob::ByteArray& bytes)
        {
            const auto path = root + L"\\" + name;
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            assert(file != INVALID_HANDLE_VALUE);
            DWORD written = 0;
            assert(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
            assert(written == bytes.size());
            assert(CloseHandle(file));
        };
        // 44.1 kHz・モノ・PCM16 の 3 フレーム。奇数長の LIST と詰めを fmt と data の間に置く。
        const Core::Asset::AssetBlob::ByteArray wave = {
            'R', 'I', 'F', 'F', 54, 0, 0, 0, 'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ', 16, 0, 0, 0,
            1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0,
            'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0,
            'd', 'a', 't', 'a', 6, 0, 0, 0, 1, 0, 2, 0, 3, 0,
        };
        writeFile(L"clip.wav", wave);
        writeFile(L"other.wav", {'n', 'o', 't', ' ', 'w', 'a', 'v', 'e'});

        Core::Asset::AssetSystem assets;
        assets.SetLooseAssetRoot(Core::Container::AnsiString(utf8Root.data()));
        TSharedPtr<AudioClipResource> clip;
        assert(Private::LoadAudioClip(assets, "clip.wav", clip) == Private::AudioClipLoadResult::Success);
        assert(clip && clip->IsValid());
        assert(clip->GetFrameCount() == 3);
        const auto& format = clip->GetFormat();
        assert(format.SampleRate == 44100);
        assert(format.ChannelCount == 1);
        assert(format.BitsPerSample == 16);
        assert(format.BlockAlignment == 2);
        assert(clip->GetPcmBytes().size() == 6);
        assert(clip->GetPcmBytes()[0] == 1 && clip->GetPcmBytes()[4] == 3);
        assert(Private::LoadAudioClip(assets, "other.wav", clip) == Private::AudioClipLoadResult::NotCooked);
        assert(!clip);

        assert(DeleteFileW((root + L"\\clip.wav").c_str()));
        assert(DeleteFileW((root + L"\\other.wav").c_str()));
        assert(RemoveDirectoryW(root.c_str()));
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

    void TestNullBackendFallback()
    {
        auto failingBackend = MakeUnique<FakeAudioBackend>();
        failingBackend->bFailInitialize = true;
        auto strictModule = Private::CreateAudioModuleForBackend(std::move(failingBackend));
        assert(strictModule != nullptr);
        assert(!strictModule->Initialize());
        assert(strictModule->GetBackendStatus().Kind == AudioBackendKind::Platform);
        assert(!strictModule->GetBackendStatus().bFellBackToNull);
        strictModule->Shutdown();

        failingBackend = MakeUnique<FakeAudioBackend>();
        failingBackend->bFailInitialize = true;
        AudioModuleOptions options;
        options.bAllowNullBackendFallback = true;
        auto module = Private::CreateAudioModuleForBackend(std::move(failingBackend), options);
        assert(module != nullptr);
        assert(module->Initialize());
        assert(module->GetBackendStatus().Kind == AudioBackendKind::Null);
        assert(module->GetBackendStatus().bFellBackToNull);
        assert(module->GetBackendStatus().Reason == "backend_failure");

        auto& sound = static_cast<Private::SoundSystem&>(module->GetSoundSystem());
        const SoundId id = RegisterTestSound(sound);
        SoundInstanceHandle handle;
        assert(sound.PlayGlobal(id, SoundPlayParams{}, handle) == AudioResult::Success);
        assert(sound.IsPlaying(handle));
        module->Tick(1.0f / 60.0f);
        assert(!sound.IsPlaying(handle));
        assert(module->GetAudioService().GetDiagnostics().ActiveVoiceCount == 0);
        module->Shutdown();
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

    SoundDefinition MakeConfiguredTestSound(SoundId id)
    {
        SoundDefinition definition;
        definition.Id = id;
        definition.Clips.push_back(MakeClip());
        definition.VolumeJitterDb = 0.0f;
        definition.PitchJitterSemitones = 0.0f;
        return definition;
    }

    TSharedPtr<AudioClipResource> MakeVariantClip(uint8_t marker)
    {
        const Core::Asset::AssetBlob::ByteArray pcm(16 * 4, marker);
        const auto blob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(pcm.data(), pcm.size()),
            Core::Container::AnsiString("memory/playback-variant"));
        return MakeShared<AudioClipResource>(blob, AudioPcmFormat{48000, 2, 16, 4}, 16);
    }

    void TestVoiceStealingOrderAndStaleEvents()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystemConfig config;
        config.MaxConcurrentVoices = 4;
        Private::SoundSystem sound(*service, Random::Generator{12345}, config);

        const SoundId ids[] = {
            SoundId("AudioPlaybackTest.Steal10"), SoundId("AudioPlaybackTest.Steal50A"),
            SoundId("AudioPlaybackTest.Steal50B"), SoundId("AudioPlaybackTest.Steal90"),
            SoundId("AudioPlaybackTest.Steal30"), SoundId("AudioPlaybackTest.Steal200"),
            SoundId("AudioPlaybackTest.Steal5")};
        constexpr uint8_t priorities[] = {10, 50, 50, 90, 30, 200, 5};
        for (uint32_t index = 0; index < 7; ++index)
        {
            SoundDefinition definition = MakeConfiguredTestSound(ids[index]);
            definition.Priority = priorities[index];
            definition.MaxInstances = 0;
            definition.CooldownMs = 0;
            definition.DedupeRadius = 0.0f;
            assert(sound.RegisterDefinition(std::move(definition)) == AudioResult::Success);
        }

        SoundPlayParams params;
        SoundInstanceHandle handles[6];
        VoiceHandle voices[6];
        size_t operationIndex = fake->Operations.size();
        uint32_t liveVoiceCount = 0;
        for (uint32_t index = 0; index < 6; ++index)
        {
            assert(sound.PlayGlobal(ids[index], params, handles[index]) == AudioResult::Success);
            assert(handles[index].IsValid());
            voices[index] = Private::SoundSystemTestAccess::GetVoiceHandle(sound, handles[index]);
            assert(voices[index].IsValid());
            for (; operationIndex < fake->Operations.size(); ++operationIndex)
            {
                const FakeAudioOperation operation = fake->Operations[operationIndex];
                if (operation == FakeAudioOperation::Create)
                {
                    ++liveVoiceCount;
                }
                else if (operation == FakeAudioOperation::Destroy)
                {
                    assert(liveVoiceCount > 0);
                    --liveVoiceCount;
                }
                assert(liveVoiceCount <= config.MaxConcurrentVoices);
            }
            assert(fake->CreateCount >= fake->DestroyCount);
            assert(fake->CreateCount - fake->DestroyCount == liveVoiceCount);
            assert(fake->CreateCount - fake->DestroyCount <= 4);
            if (index == 4)
            {
                assert(sound.SetVolume(handles[0], 1.0f) == AudioResult::StaleHandle);
                assert(!sound.IsPlaying(handles[0]));
            }
            if (index == 5)
            {
                assert(sound.SetVolume(handles[4], 1.0f) == AudioResult::StaleHandle);
                assert(!sound.IsPlaying(handles[4]));
            }
        }
        for (uint32_t index = 0; index < 6; ++index)
        {
            const bool bExpectedPlaying = index == 1 || index == 2 || index == 3 || index == 5;
            assert(sound.IsPlaying(handles[index]) == bExpectedPlaying);
        }
        assert(sound.GetDiagnostics().ActiveInstanceCount == 4);
        assert(service->GetDiagnostics().ActiveVoiceCount == 4);
        assert(sound.GetDiagnostics().Voice.StolenVoiceCount == 2);

        const uint64_t rejectedBefore = sound.GetDiagnostics().Voice.RejectedPlayCount;
        const uint32_t createdBefore = fake->CreateCount;
        const uint32_t destroyedBefore = fake->DestroyCount;
        SoundInstanceHandle rejected;
        assert(sound.PlayGlobal(ids[6], params, rejected) == AudioResult::Rejected);
        assert(!rejected.IsValid());
        assert(sound.GetDiagnostics().Voice.RejectedPlayCount == rejectedBefore + 1);
        assert(fake->CreateCount == createdBefore);
        assert(fake->DestroyCount == destroyedBefore);

        const uint64_t staleBefore = service->GetDiagnostics().StaleEventCount;
        fake->Emit(voices[0], fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        fake->Emit(voices[4], fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        service->Tick();
        sound.Update(1.0f / 60.0f);
        assert(service->GetDiagnostics().StaleEventCount == staleBefore + 2);
        assert(service->GetDiagnostics().ActiveVoiceCount == 4);
        assert(sound.GetDiagnostics().ActiveInstanceCount == 4);
        const uint32_t survivorIndices[] = {1, 2, 3, 5};
        for (uint32_t index : survivorIndices)
        {
            assert(sound.IsPlaying(handles[index]));
            assert(Private::SoundSystemTestAccess::GetVoiceHandle(sound, handles[index]) == voices[index]);
            AudioVoiceState state = AudioVoiceState::Destroyed;
            assert(service->GetVoiceState(voices[index], state) == AudioResult::Success);
            assert(state == AudioVoiceState::Playing);
        }
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestCooldownUsesUpdateTime()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id("AudioPlaybackTest.Cooldown");
        SoundDefinition definition = MakeConfiguredTestSound(id);
        definition.CooldownMs = 60;
        assert(sound.RegisterDefinition(std::move(definition)) == AudioResult::Success);

        constexpr float deltaSeconds = 0.0167f;
        double expectedTime = 0.0;
        double lastExpectedPlay = 0.0;
        bool bHasExpectedPlay = false;
        uint32_t expectedCount = 0;
        uint32_t actualCount = 0;
        const uint32_t createdBefore = fake->CreateCount;
        const uint64_t suppressedBefore = sound.GetDiagnostics().Voice.SuppressedByCooldownCount;
        SoundPlayParams params;
        for (uint32_t index = 0; index < 10; ++index)
        {
            const bool bExpectedPlay = !bHasExpectedPlay || expectedTime - lastExpectedPlay >= 0.060;
            if (bExpectedPlay)
            {
                bHasExpectedPlay = true;
                lastExpectedPlay = expectedTime;
                ++expectedCount;
            }
            SoundInstanceHandle handle;
            const AudioResult result = sound.PlayGlobal(id, params, handle);
            if (result == AudioResult::Success)
            {
                ++actualCount;
                assert(handle.IsValid());
                assert(sound.IsPlaying(handle));
            }
            else
            {
                assert(result == AudioResult::Rejected);
                assert(!handle.IsValid());
            }
            assert((result == AudioResult::Success) == bExpectedPlay);
            service->Tick();
            sound.Update(deltaSeconds);
            expectedTime += static_cast<double>(deltaSeconds);
        }
        assert(expectedCount == 3);
        assert(actualCount == expectedCount);
        assert(fake->CreateCount - createdBefore == expectedCount);
        assert(sound.GetDiagnostics().Voice.SuppressedByCooldownCount - suppressedBefore == 10 - expectedCount);
        assert(sound.GetDiagnostics().Voice.SuppressedByCooldownCount - suppressedBefore == 7);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestMaxInstancesPolicies()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});

        const SoundId stopOldestId("AudioPlaybackTest.StopOldest");
        SoundDefinition stopOldest = MakeConfiguredTestSound(stopOldestId);
        stopOldest.MaxInstances = 1;
        stopOldest.LimitPolicy = SoundInstanceLimitPolicy::StopOldest;
        assert(sound.RegisterDefinition(std::move(stopOldest)) == AudioResult::Success);
        const SoundId rejectNewId("AudioPlaybackTest.RejectNew");
        SoundDefinition rejectNew = MakeConfiguredTestSound(rejectNewId);
        rejectNew.MaxInstances = 1;
        rejectNew.LimitPolicy = SoundInstanceLimitPolicy::RejectNew;
        assert(sound.RegisterDefinition(std::move(rejectNew)) == AudioResult::Success);

        SoundPlayParams params;
        SoundInstanceHandle oldest;
        SoundInstanceHandle replacement;
        const uint64_t stolenBefore = sound.GetDiagnostics().Voice.StolenVoiceCount;
        assert(sound.PlayGlobal(stopOldestId, params, oldest) == AudioResult::Success);
        assert(sound.IsPlaying(oldest));
        assert(sound.PlayGlobal(stopOldestId, params, replacement) == AudioResult::Success);
        assert(sound.SetVolume(oldest, 1.0f) == AudioResult::StaleHandle);
        assert(!sound.IsPlaying(oldest));
        assert(replacement.IsValid() && sound.IsPlaying(replacement));
        assert(sound.GetDiagnostics().Voice.StolenVoiceCount == stolenBefore + 1);

        SoundInstanceHandle retained;
        SoundInstanceHandle rejected;
        assert(sound.PlayGlobal(rejectNewId, params, retained) == AudioResult::Success);
        const uint64_t rejectedBefore = sound.GetDiagnostics().Voice.RejectedPlayCount;
        const uint32_t createdBefore = fake->CreateCount;
        assert(sound.PlayGlobal(rejectNewId, params, rejected) == AudioResult::Rejected);
        assert(!rejected.IsValid());
        assert(sound.IsPlaying(retained));
        assert(sound.IsPlaying(replacement));
        assert(sound.GetDiagnostics().Voice.RejectedPlayCount == rejectedBefore + 1);
        assert(fake->CreateCount == createdBefore);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestDedupeWithinFrame()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id("AudioPlaybackTest.Dedupe");
        SoundDefinition definition = MakeConfiguredTestSound(id);
        definition.DedupeRadius = 1.0f;
        definition.DedupeWindowMs = 0;
        assert(sound.RegisterDefinition(std::move(definition)) == AudioResult::Success);

        SoundPlayParams params;
        SoundInstanceHandle first;
        SoundInstanceHandle suppressed;
        SoundInstanceHandle distant;
        assert(sound.PlayAt(id, Math::Vector3(0.0f, 0.0f, 0.0f), params, first) == AudioResult::Success);
        assert(sound.PlayAt(id, Math::Vector3(0.5f, 0.0f, 0.0f), params, suppressed) == AudioResult::Rejected);
        assert(!suppressed.IsValid());
        assert(sound.PlayAt(id, Math::Vector3(5.0f, 0.0f, 0.0f), params, distant) == AudioResult::Success);
        assert(sound.IsPlaying(first) && sound.IsPlaying(distant));
        assert(fake->CreateCount == 2);
        assert(sound.GetDiagnostics().SuppressedByDedupeCount == 1);
        service->Tick();
        sound.Update(1.0f / 60.0f);
        SoundInstanceHandle nextFrame;
        assert(sound.PlayAt(id, Math::Vector3(0.0f, 0.0f, 0.0f), params, nextFrame) == AudioResult::Success);
        assert(sound.IsPlaying(nextFrame));
        assert(fake->CreateCount == 3);
        assert(sound.GetDiagnostics().SuppressedByDedupeCount == 1);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

    void TestNoRepeatClipDistribution()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});
        const SoundId id("AudioPlaybackTest.NoRepeat");
        SoundDefinition definition;
        definition.Id = id;
        for (uint8_t marker = 1; marker <= 3; ++marker)
        {
            definition.Clips.push_back(MakeVariantClip(marker));
        }
        definition.MaxInstances = 1;
        definition.LimitPolicy = SoundInstanceLimitPolicy::StopOldest;
        definition.VolumeJitterDb = 0.0f;
        definition.PitchJitterSemitones = 0.0f;
        assert(sound.RegisterDefinition(std::move(definition)) == AudioResult::Success);

        constexpr uint32_t playCount = 10000;
        uint32_t counts[3] = {};
        uint32_t previous = UINT32_MAX;
        SoundPlayParams params;
        for (uint32_t index = 0; index < playCount; ++index)
        {
            SoundInstanceHandle handle;
            assert(sound.PlayGlobal(id, params, handle) == AudioResult::Success);
            assert(handle.IsValid() && sound.IsPlaying(handle));
            assert(fake->SubmittedData != nullptr);
            assert(fake->SubmittedSize == 16 * 4);
            const uint8_t marker = fake->SubmittedData[0];
            assert(marker >= 1 && marker <= 3);
            for (size_t byteIndex = 1; byteIndex < fake->SubmittedSize; ++byteIndex)
            {
                assert(fake->SubmittedData[byteIndex] == marker);
            }
            const uint32_t selected = marker - 1;
            assert(selected != previous);
            ++counts[selected];
            previous = selected;
        }
        assert(fake->CreateCount == playCount);
        assert(sound.GetDiagnostics().Voice.StolenVoiceCount == playCount - 1);
        for (uint32_t count : counts)
        {
            assert(count * 3 * 100 >= playCount * 95);
            assert(count * 3 * 100 <= playCount * 105);
        }
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }


    void TestSoundBankRejectsInvalidDefinitions()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto service = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(service->Initialize() == AudioResult::Success);
        Private::SoundSystem sound(*service, Random::Generator{12345}, {});

        const Core::Asset::AssetBlob::ByteArray monoPcm(16 * 2, 0);
        const auto monoBlob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(monoPcm.data(), monoPcm.size()),
            Core::Container::AnsiString("memory/bank-mono"));
        const auto monoClip = MakeShared<AudioClipResource>(monoBlob, AudioPcmFormat{48000, 1, 16, 2}, 16);
        const auto stereoClip = MakeClip();
        struct ClipEntry
        {
            const char* Path;
            TSharedPtr<AudioClipResource> Clip;
        };
        const ClipEntry clips[] = {
            {"Audio/Test/mono.wav", monoClip},
            {"Audio/Test/stereo.wav", stereoClip},
        };
        const Private::SoundBankClipResolver resolveClip(
            [&clips](const Core::Container::String& path) -> TSharedPtr<AudioClipResource>
            {
                for (const auto& clip : clips)
                {
                    if (path == clip.Path)
                    {
                        return clip.Clip;
                    }
                }
                return {};
            });

        constexpr char json[] = R"({"version":1,"sounds":[
            {"id":"Bank.Valid","clips":["Audio/Test/mono.wav"],"spatial":"3d"},
            {"id":"Bank.Missing","clips":["Audio/Test/missing.wav"]},
            {"id":"Bank.Stereo3d","clips":["Audio/Test/stereo.wav"],"spatial":"3d"},
            {"id":"Bank.Unknown","clips":["Audio/Test/mono.wav"],"unknown_key":true}
        ]})";
        constexpr char bankKey[] = "Audio/Test/bank.soundbank.json";
        SoundBankLoadReport report;
        assert(Private::SoundSystemTestAccess::LoadBankFromText(
            sound, bankKey, Core::Container::Span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(json), sizeof(json) - 1), resolveClip, report) == AudioResult::Success);
        assert(report.LoadedDefinitionCount == 1);
        assert(report.RejectedDefinitionCount == 3);
        assert(report.Diagnostics.size() == 3);
        struct ExpectedDiagnostic
        {
            const char* Name;
            SoundBankRejectReason Reason;
        };
        const ExpectedDiagnostic expected[] = {
            {"Bank.Missing", SoundBankRejectReason::MissingClip},
            {"Bank.Stereo3d", SoundBankRejectReason::SpatialStereoClip},
            {"Bank.Unknown", SoundBankRejectReason::UnknownKey},
        };
        for (const auto& item : expected)
        {
            uint32_t matchingCount = 0;
            for (const auto& diagnostic : report.Diagnostics)
            {
                if (diagnostic.DefinitionName == item.Name && diagnostic.Reason == item.Reason)
                {
                    ++matchingCount;
                }
            }
            assert(matchingCount == 1);
        }

        SoundPlayParams params;
        SoundInstanceHandle valid;
        assert(sound.PlayGlobal(SoundId("Bank.Valid"), params, valid) == AudioResult::Success);
        assert(valid.IsValid() && sound.IsPlaying(valid));
        assert(fake->CreateCount == 1 && fake->StartCount == 1);
        for (const auto& item : expected)
        {
            SoundInstanceHandle rejected;
            assert(sound.PlayGlobal(SoundId(item.Name), params, rejected) == AudioResult::InvalidState);
            assert(!rejected.IsValid());
        }
        assert(fake->CreateCount == 1 && fake->StartCount == 1);
        assert(sound.UnloadBank(bankKey) == AudioResult::Success);
        SoundInstanceHandle unloaded;
        assert(sound.PlayGlobal(SoundId("Bank.Valid"), params, unloaded) == AudioResult::InvalidState);
        assert(!unloaded.IsValid());
        assert(fake->CreateCount == 1 && fake->StartCount == 1);
        assert(sound.Shutdown() == AudioResult::Success);
        assert(service->Shutdown() == AudioResult::Success);
    }

}

int main()
{
    TestLooseWaveClipLoading();
    TestLoopRemainsPlayingUntilStopped();
    TestPausePreservesSubmittedBuffer();
    TestQuarantinedVoiceDoesNotTouchBackend();
    TestDestroyedHandleIsStale();
    TestPanUsesEqualPowerGains();
    TestNullBackendFallback();
    TestFadeOutDurationAndDrainedLifetime();
    TestLinearFadeIn();
    TestEqualPowerFadeIn();
    TestMinimumFadeClamp();
    TestShutdownDuringFade();
    TestGenerationWrapRejectsOldHandles();
    TestVoiceStealingOrderAndStaleEvents();
    TestCooldownUsesUpdateTime();
    TestMaxInstancesPolicies();
    TestDedupeWithinFrame();
    TestNoRepeatClipDistribution();
    TestSoundBankRejectsInvalidDefinitions();
    std::cout << "AudioPlaybackTest: 合格\n";
    return 0;
}
