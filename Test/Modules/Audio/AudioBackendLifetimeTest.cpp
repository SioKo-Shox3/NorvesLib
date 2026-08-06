#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioModule.h"
#include "Asset/AssetBlob.h"
#include "Container/PointerTypes.h"
#include "CoreTypes.h"
#include "Thread/Thread.h"

#include <cassert>
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
        const Core::Asset::AssetBlob::ByteArray pcm{0, 0, 1, 0, 2, 0, 3, 0};
        const auto blob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(pcm.data(), pcm.size()),
            Core::Container::AnsiString("memory/lifetime"));
        return MakeShared<AudioClipResource>(blob, AudioPcmFormat{48000, 2, 16, 4}, 2);
    }

    void TestClipPinnedAndCallbackDeferredToTick()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);

        auto clip = MakeClip();
        TWeakPtr<AudioClipResource> weakClip = clip;
        VoiceHandle handle;
        assert(module->CreateVoice(clip, handle) == AudioResult::Success);
        clip.reset();
        assert(!weakClip.expired());
        assert(fake->SubmittedData != nullptr && fake->SubmittedSize == 8);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->StopVoice(handle) == AudioResult::Success);

        Thread::Thread callbackThread([fake, handle]()
        {
            fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        });
        callbackThread.Join();

        AudioVoiceState state;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success && state == AudioVoiceState::Stopping);
        module->Tick();
        assert(module->GetVoiceState(handle, state) == AudioResult::Success && state == AudioVoiceState::Drained);
        assert(!weakClip.expired());
        assert(module->DestroyVoice(handle) == AudioResult::Success);
        assert(weakClip.expired());
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestShutdownDrainsCallbacksBeforeBackend()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        fake->bEmitDrainedDuringQuiesce = true;
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::Success);

        const AudioDiagnostics diagnostics = module->GetDiagnostics();
        assert(diagnostics.ActiveVoiceCount == 0);
        assert(diagnostics.PendingEventCount == 0);
        assert(diagnostics.InFlightCallbackCount == 0);
        assert(fake->QuiesceCount == 1 && fake->DestroyCount == 1 && fake->ShutdownCount == 1);
        assert(fake->Operations.size() >= 4);
        assert(fake->Operations[fake->Operations.size() - 3] == FakeAudioOperation::QuiesceCallbacks);
        assert(fake->Operations[fake->Operations.size() - 2] == FakeAudioOperation::Destroy);
        assert(fake->Operations[fake->Operations.size() - 1] == FakeAudioOperation::Shutdown);

        fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->GetDiagnostics().LateEventCount == diagnostics.LateEventCount + 1);
    }

    void TestShutdownRejectsDelayedCallbackAndLeavesQueueEmpty()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        Thread::Atomic<bool> releaseCallback{false};
        Thread::Atomic<bool> callbackComplete{false};
        fake->pReleaseDelayedCallback = &releaseCallback;
        fake->pDelayedCallbackComplete = &callbackComplete;
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);

        Thread::Thread callbackThread([fake, handle, &releaseCallback, &callbackComplete]()
        {
            while (!releaseCallback.Load())
            {
                std::this_thread::yield();
            }
            fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
            callbackComplete.Store(true);
        });

        assert(module->Shutdown() == AudioResult::Success);
        callbackThread.Join();
        const AudioDiagnostics diagnostics = module->GetDiagnostics();
        assert(diagnostics.PendingEventCount == 0);
        assert(diagnostics.InFlightCallbackCount == 0);
        assert(diagnostics.LateEventCount == 1);
    }
}

int main()
{
    TestClipPinnedAndCallbackDeferredToTick();
    TestShutdownDrainsCallbacksBeforeBackend();
    TestShutdownRejectsDelayedCallbackAndLeavesQueueEmpty();
    std::cout << "AudioBackendLifetimeTest passed\n";
    return 0;
}
