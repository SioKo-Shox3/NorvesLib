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

    void TestPcmPayloadPinnedAcrossClipUnload()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);

        auto pcmOwner = MakeShared<Core::Asset::AssetBlob::ByteArray>(
            Core::Asset::AssetBlob::ByteArray{0, 0, 1, 0, 2, 0, 3, 0});
        TWeakPtr<const Core::Asset::AssetBlob::ByteArray> weakPcmOwner = pcmOwner;
        auto clip = MakeShared<AudioClipResource>(
            Core::Asset::AssetBlob::FromOwnedBytes(pcmOwner),
            AudioPcmFormat{48000, 2, 16, 4},
            2);
        VoiceHandle handle;
        assert(module->CreateVoice(clip, handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);

        clip->Unload();
        pcmOwner.reset();
        assert(!weakPcmOwner.expired());
        assert(fake->SubmittedData != nullptr && fake->SubmittedSize == 8);

        assert(module->StopVoice(handle) == AudioResult::Success);
        fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->DestroyVoice(handle) == AudioResult::Success);
        assert(weakPcmOwner.expired());
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestConcurrentCallbacksCoalesceWithoutLosingTerminalState()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->StopVoice(handle) == AudioResult::Success);

        constexpr uint32_t EventsPerThread = 32;
        Thread::Atomic<bool> bStart{false};
        const auto emitEvents = [fake, handle, &bStart]()
        {
            while (!bStart.Load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (uint32_t index = 0; index < EventsPerThread; ++index)
            {
                fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
            }
        };
        Thread::Thread callbackA(emitEvents);
        Thread::Thread callbackB(emitEvents);
        Thread::Thread callbackC(emitEvents);
        Thread::Thread callbackD(emitEvents);
        bStart.Store(true, std::memory_order_release);
        callbackA.Join();
        callbackB.Join();
        callbackC.Join();
        callbackD.Join();

        const AudioDiagnostics queuedDiagnostics = module->GetDiagnostics();
        assert(queuedDiagnostics.PendingEventCount == 1);
        assert(queuedDiagnostics.DroppedEventCount == EventsPerThread * 4 - 1);
        module->Tick();
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Drained);
        assert(module->DestroyVoice(handle) == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestEventQueueOverflowIsCounted()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->StopVoice(handle) == AudioResult::Success);

        constexpr size_t EmittedEventCount = 512;
        for (size_t index = 0; index < EmittedEventCount; ++index)
        {
            fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        }
        const AudioDiagnostics queuedDiagnostics = module->GetDiagnostics();
        const uint64_t droppedEventCount = queuedDiagnostics.DroppedEventCount;
        assert(droppedEventCount > 0);
        assert(queuedDiagnostics.PendingEventCount + droppedEventCount == EmittedEventCount);

        module->Tick();
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Drained);
        assert(module->DestroyVoice(handle) == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestTerminalEventForAnotherVoiceSurvivesQueueSaturation()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);

        VoiceHandle saturatedHandle;
        assert(module->CreateVoice(MakeClip(), saturatedHandle) == AudioResult::Success);
        assert(module->StartVoice(saturatedHandle) == AudioResult::Success);
        assert(module->StopVoice(saturatedHandle) == AudioResult::Success);

        auto pcmOwner = MakeShared<Core::Asset::AssetBlob::ByteArray>(
            Core::Asset::AssetBlob::ByteArray{0, 0, 1, 0, 2, 0, 3, 0});
        TWeakPtr<const Core::Asset::AssetBlob::ByteArray> weakPcmOwner = pcmOwner;
        auto clip = MakeShared<AudioClipResource>(
            Core::Asset::AssetBlob::FromOwnedBytes(pcmOwner),
            AudioPcmFormat{48000, 2, 16, 4},
            2);
        VoiceHandle terminalHandle;
        assert(module->CreateVoice(clip, terminalHandle) == AudioResult::Success);
        assert(module->StartVoice(terminalHandle) == AudioResult::Success);
        clip->Unload();
        pcmOwner.reset();
        assert(!weakPcmOwner.expired());
        assert(module->StopVoice(terminalHandle) == AudioResult::Success);

        constexpr size_t SaturatingEventCount = 512;
        for (size_t index = 0; index < SaturatingEventCount; ++index)
        {
            fake->Emit(saturatedHandle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        }
        assert(module->GetDiagnostics().DroppedEventCount > 0);
        fake->Emit(terminalHandle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();

        AudioVoiceState terminalState = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(terminalHandle, terminalState) == AudioResult::Success);
        assert(terminalState == AudioVoiceState::Drained);
        assert(module->DestroyVoice(terminalHandle) == AudioResult::Success);
        assert(weakPcmOwner.expired());

        AudioVoiceState saturatedState = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(saturatedHandle, saturatedState) == AudioResult::Success);
        assert(saturatedState == AudioVoiceState::Drained);
        assert(module->DestroyVoice(saturatedHandle) == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestStaleEpochCallbackDoesNotAdvanceVoice()
    {
        auto backend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* fake = backend.get();
        auto module = Private::CreateAudioServiceForBackend(std::move(backend));
        assert(module->Initialize() == AudioResult::Success);
        VoiceHandle handle;
        assert(module->CreateVoice(MakeClip(), handle) == AudioResult::Success);
        assert(module->StartVoice(handle) == AudioResult::Success);
        assert(module->StopVoice(handle) == AudioResult::Success);

        fake->Emit(handle, fake->ShutdownEpoch + 1, Private::AudioBackendEventKind::Drained);
        module->Tick();
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Stopping);
        assert(module->GetDiagnostics().LateEventCount == 1);

        fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
        module->Tick();
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Drained);
        assert(module->DestroyVoice(handle) == AudioResult::Success);
        assert(module->Shutdown() == AudioResult::Success);
    }

    void TestVoiceErrorIsTerminalAndReleasesClip()
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
        assert(module->StartVoice(handle) == AudioResult::Success);

        fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::VoiceError);
        module->Tick();
        AudioVoiceState state = AudioVoiceState::Destroyed;
        assert(module->GetVoiceState(handle, state) == AudioResult::Success);
        assert(state == AudioVoiceState::Drained);
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
    TestPcmPayloadPinnedAcrossClipUnload();
    TestConcurrentCallbacksCoalesceWithoutLosingTerminalState();
    TestEventQueueOverflowIsCounted();
    TestTerminalEventForAnotherVoiceSurvivesQueueSaturation();
    TestStaleEpochCallbackDoesNotAdvanceVoice();
    TestVoiceErrorIsTerminalAndReleasesClip();
    TestShutdownDrainsCallbacksBeforeBackend();
    TestShutdownRejectsDelayedCallbackAndLeavesQueueEmpty();
    std::cout << "AudioBackendLifetimeTest passed\n";
    return 0;
}
