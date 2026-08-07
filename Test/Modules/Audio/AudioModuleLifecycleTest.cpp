#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioModule.h"
#include "Asset/AssetBlob.h"
#include "Container/PointerTypes.h"
#include "CoreTypes.h"
#include "Object/Resource.h"

#include <cassert>
#include <iostream>
#include <utility>
#include <type_traits>

static_assert(std::is_base_of_v<NorvesLib::Core::Resource,
                               NorvesLib::Modules::Audio::AudioClipResource>);

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Modules::Audio;
    using NorvesLib::Test::Audio::FakeAudioBackend;
    using NorvesLib::Test::Audio::FakeAudioOperation;

    TSharedPtr<AudioClipResource> MakeClip()
    {
        const Core::Asset::AssetBlob::ByteArray pcm{0, 0, 1, 0, 2, 0, 3, 0};
        const auto blob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(pcm.data(), pcm.size()),
            Core::Container::AnsiString("memory/audio"));
        return MakeShared<AudioClipResource>(blob, AudioPcmFormat{44100, 1, 16, 2}, 4);
    }

    void TestLifecycleAndStaleEvents()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module != nullptr);
        assert(module->Initialize() == AudioResult::Success);

        VoiceHandle first;
        auto clip = MakeClip();
        assert(module->CreateVoice(clip, first) == AudioResult::Success);
        assert(first.IsValid());
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(first, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Submitted);
        assert(module->StartVoice(first) == AudioResult::Success);
        assert(module->StartVoice(first) == AudioResult::AlreadyInState);
        assert(module->GetVoiceState(first, state) == AudioResult::Success && state == AudioVoiceState::Playing);
        assert(module->StopVoice(first) == AudioResult::Success);
        assert(module->StopVoice(first) == AudioResult::AlreadyInState);
        assert(module->GetVoiceState(first, state) == AudioResult::Success && state == AudioVoiceState::Stopping);

        fake->Emit(first, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        assert(module->GetVoiceState(first, state) == AudioResult::Success && state == AudioVoiceState::Stopping);
        module->Tick();
        assert(module->GetVoiceState(first, state) == AudioResult::Success && state == AudioVoiceState::Drained);
        fake->Emit(first, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->GetDiagnostics().StaleEventCount == 0);
        assert(module->DestroyVoice(first) == AudioResult::Success);
        assert(module->GetVoiceState(first, state) == AudioResult::StaleHandle);

        VoiceHandle second;
        assert(module->CreateVoice(MakeClip(), second) == AudioResult::Success);
        assert(second.Index == first.Index && second.Generation != first.Generation);
        fake->Emit(first, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->GetDiagnostics().StaleEventCount == 1);
        assert(module->GetVoiceState(second, state) == AudioResult::Success && state == AudioVoiceState::Submitted);
        assert(module->Shutdown() == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::AlreadyInState);
        assert(fake->ShutdownCount == 1);
    }

    void TestPartialFailureRollback()
    {
        {
            auto backend = MakeUnique<FakeAudioBackend>();
            FakeAudioBackend* fake = backend.get();
            fake->bFailCreate = true;
            auto module = Private::CreateAudioServiceForBackend(std::move(backend));
            assert(module->Initialize() == AudioResult::Success);
            VoiceHandle handle;
            assert(module->CreateVoice(MakeClip(), handle) == AudioResult::BackendFailure);
            assert(fake->CreateCount == 1 && fake->DestroyCount == 0);
            assert(module->GetDiagnostics().ActiveVoiceCount == 0);
            assert(module->Shutdown() == AudioResult::Success);
        }
        {
            auto backend = MakeUnique<FakeAudioBackend>();
            FakeAudioBackend* fake = backend.get();
            fake->bFailSubmit = true;
            auto module = Private::CreateAudioServiceForBackend(std::move(backend));
            assert(module->Initialize() == AudioResult::Success);
            VoiceHandle handle;
            assert(module->CreateVoice(MakeClip(), handle) == AudioResult::BackendFailure);
            assert(fake->CreateCount == 1 && fake->SubmitCount == 1 && fake->DestroyCount == 1);
            assert(module->GetDiagnostics().ActiveVoiceCount == 0);
            assert(module->Shutdown() == AudioResult::Success);
        }
        {
            auto backend = MakeUnique<FakeAudioBackend>();
            FakeAudioBackend* fake = backend.get();
            fake->bFailStart = true;
            auto module = Private::CreateAudioServiceForBackend(std::move(backend));
            assert(module->Initialize() == AudioResult::Success);
            VoiceHandle handle;
            assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
            assert(module->StartVoice(handle) == AudioResult::BackendFailure);
            assert(fake->DestroyCount == 1);
            AudioVoiceState state;
            assert(module->GetVoiceState(handle, state) == AudioResult::StaleHandle);
            assert(module->Shutdown() == AudioResult::Success);
        }
    }

    void TestRollbackDestroyFailureQuarantinesVoiceAndPinsClip()
    {
        {
            auto backend = MakeUnique<FakeAudioBackend>();
            FakeAudioBackend* fake = backend.get();
            fake->bFailSubmit = true;
            fake->bFailDestroy = true;
            auto module = Private::CreateAudioServiceForBackend(std::move(backend));
            assert(module->Initialize() == AudioResult::Success);

            auto clip = MakeClip();
            TWeakPtr<AudioClipResource> weakClip = clip;
            VoiceHandle failedHandle;
            assert(module->CreateVoice(clip, failedHandle) == AudioResult::BackendFailure);
            const VoiceHandle quarantinedHandle = fake->LastHandle;
            clip.reset();
            assert(!weakClip.expired());
            assert(module->GetDiagnostics().QuarantinedVoiceCount == 1);

            fake->bFailSubmit = false;
            fake->bFailDestroy = false;
            VoiceHandle nextHandle;
            assert(module->CreateVoice(MakeClip(), nextHandle) == AudioResult::Success);
            assert(nextHandle.Index != quarantinedHandle.Index);
            fake->bFailDestroy = true;
            assert(module->Shutdown() == AudioResult::BackendFailure);
            assert(!weakClip.expired());
            assert(module->GetDiagnostics().QuarantinedVoiceCount == 2);
            assert(fake->Operations[fake->Operations.size() - 1] == FakeAudioOperation::Shutdown);
        }

        {
            auto backend = MakeUnique<FakeAudioBackend>();
            FakeAudioBackend* fake = backend.get();
            fake->bFailStart = true;
            fake->bFailDestroy = true;
            auto module = Private::CreateAudioServiceForBackend(std::move(backend));
            assert(module->Initialize() == AudioResult::Success);

            auto clip = MakeClip();
            TWeakPtr<AudioClipResource> weakClip = clip;
            VoiceHandle failedHandle;
            assert(module->CreateVoice(clip, failedHandle) == AudioResult::Success);
            clip.reset();
            assert(module->StartVoice(failedHandle) == AudioResult::BackendFailure);
            assert(!weakClip.expired());
            AudioVoiceState state = AudioVoiceState::Destroyed;
            assert(module->GetVoiceState(failedHandle, state) == AudioResult::Success);
            assert(state == AudioVoiceState::Quarantined);

            fake->bFailStart = false;
            fake->bFailDestroy = false;
            VoiceHandle nextHandle;
            assert(module->CreateVoice(MakeClip(), nextHandle) == AudioResult::Success);
            assert(nextHandle.Index != failedHandle.Index);
            fake->bFailDestroy = true;
            assert(module->Shutdown() == AudioResult::BackendFailure);
            assert(!weakClip.expired());
            assert(module->GetDiagnostics().QuarantinedVoiceCount == 2);
        }
    }

    void TestStopFlushPartialFailureQuarantinesVoice()
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
        assert(module->GetDiagnostics().QuarantinedVoiceCount == 1);
        assert(module->Shutdown() == AudioResult::Success);
    }

    bool TestInitializeFailureShutsDownBackendBeforeRetry()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        fake->bFailInitialize = true;
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::BackendFailure);
        if (fake->Operations.size() != 2 ||
            fake->Operations[0] != FakeAudioOperation::Initialize ||
            fake->Operations[1] != FakeAudioOperation::Shutdown)
        {
            std::cerr << "failed Initialize did not immediately shut down backend\n";
            return false;
        }

        fake->bFailInitialize = false;
        assert(module->Initialize() == AudioResult::Success);
        if (fake->Operations.size() != 3 ||
            fake->Operations[2] != FakeAudioOperation::Initialize)
        {
            std::cerr << "service did not retry Initialize from a clean backend state\n";
            module->Shutdown();
            return false;
        }
        assert(module->Shutdown() == AudioResult::Success);
        return true;
    }
}

int main()
{
    TestLifecycleAndStaleEvents();
    TestPartialFailureRollback();
    TestRollbackDestroyFailureQuarantinesVoiceAndPinsClip();
    TestStopFlushPartialFailureQuarantinesVoice();
    if (!TestInitializeFailureShutsDownBackendBeforeRetry())
    {
        return 1;
    }
    std::cout << "AudioModuleLifecycleTest passed\n";
    return 0;
}
