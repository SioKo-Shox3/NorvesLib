#pragma once

#include "Audio/AudioBackend.h"

#include "Container/VariableArray.h"
#include "Thread/Atomic.h"

#include <thread>

namespace NorvesLib::Test::Audio
{
    struct FakeAudioMailboxBinding
    {
        Modules::Audio::VoiceHandle Handle;
        Modules::Audio::Private::AudioBackendEventMailbox* Mailbox = nullptr;
    };

    enum class FakeAudioOperation : uint8_t
    {
        Initialize,
        Create,
        Submit,
        Start,
        Stop,
        QuiesceCallbacks,
        Destroy,
        Shutdown,
    };

    class FakeAudioBackend final : public Modules::Audio::Private::IAudioBackend
    {
    public:
        Modules::Audio::AudioResult Initialize(
            Modules::Audio::Private::IAudioBackendEventSink& eventSink,
            uint64_t shutdownEpoch) override
        {
            Operations.push_back(FakeAudioOperation::Initialize);
            EventSink = &eventSink;
            ShutdownEpoch = shutdownEpoch;
            return bFailInitialize ? Modules::Audio::AudioResult::BackendFailure : Modules::Audio::AudioResult::Success;
        }

        Modules::Audio::AudioResult CreateVoice(
            Modules::Audio::VoiceHandle handle,
            const Modules::Audio::AudioPcmFormat&,
            Modules::Audio::Private::AudioBackendEventMailbox& mailbox) override
        {
            Operations.push_back(FakeAudioOperation::Create);
            LastHandle = handle;
            Mailboxes.push_back(FakeAudioMailboxBinding{handle, &mailbox});
            ++CreateCount;
            return bFailCreate ? Modules::Audio::AudioResult::BackendFailure : Modules::Audio::AudioResult::Success;
        }

        Modules::Audio::AudioResult SubmitVoice(
            Modules::Audio::VoiceHandle,
            Core::Container::Span<const uint8_t> pcmBytes) override
        {
            Operations.push_back(FakeAudioOperation::Submit);
            SubmittedData = pcmBytes.data();
            SubmittedSize = pcmBytes.size();
            ++SubmitCount;
            return bFailSubmit ? Modules::Audio::AudioResult::BackendFailure : Modules::Audio::AudioResult::Success;
        }

        Modules::Audio::AudioResult StartVoice(Modules::Audio::VoiceHandle) override
        {
            Operations.push_back(FakeAudioOperation::Start);
            ++StartCount;
            return bFailStart ? Modules::Audio::AudioResult::BackendFailure : Modules::Audio::AudioResult::Success;
        }

        Modules::Audio::Private::AudioBackendStopResult StopVoice(
            Modules::Audio::VoiceHandle) override
        {
            Operations.push_back(FakeAudioOperation::Stop);
            ++StopCount;
            if (bFailStop)
            {
                return {Modules::Audio::AudioResult::BackendFailure, false, false};
            }
            if (bFailFlush)
            {
                return {Modules::Audio::AudioResult::BackendFailure, true, false};
            }
            return {Modules::Audio::AudioResult::Success, true, true};
        }

        void QuiesceCallbacks() override
        {
            Operations.push_back(FakeAudioOperation::QuiesceCallbacks);
            ++QuiesceCount;
            if (pReleaseDelayedCallback != nullptr)
            {
                pReleaseDelayedCallback->Store(true);
                while (pDelayedCallbackComplete != nullptr && !pDelayedCallbackComplete->Load())
                {
                    std::this_thread::yield();
                }
            }
            if (bEmitDrainedDuringQuiesce && EventSink != nullptr)
            {
                Emit(LastHandle, ShutdownEpoch, Modules::Audio::Private::AudioBackendEventKind::Drained);
            }
        }

        Modules::Audio::AudioResult DestroyVoice(Modules::Audio::VoiceHandle) override
        {
            Operations.push_back(FakeAudioOperation::Destroy);
            ++DestroyCount;
            return bFailDestroy ? Modules::Audio::AudioResult::BackendFailure : Modules::Audio::AudioResult::Success;
        }

        void Shutdown() override
        {
            Operations.push_back(FakeAudioOperation::Shutdown);
            ++ShutdownCount;
        }

        void Emit(Modules::Audio::VoiceHandle handle,
                  uint64_t epoch,
                  Modules::Audio::Private::AudioBackendEventKind kind)
        {
            if (EventSink == nullptr)
            {
                return;
            }
            for (auto binding = Mailboxes.rbegin(); binding != Mailboxes.rend(); ++binding)
            {
                if (binding->Handle == handle && binding->Mailbox != nullptr)
                {
                    EventSink->EnqueueBackendEvent(*binding->Mailbox, handle, epoch, kind);
                    return;
                }
            }
        }

        bool bFailInitialize = false;
        bool bFailCreate = false;
        bool bFailSubmit = false;
        bool bFailStart = false;
        bool bFailStop = false;
        bool bFailFlush = false;
        bool bFailDestroy = false;
        bool bEmitDrainedDuringQuiesce = false;
        uint32_t CreateCount = 0;
        uint32_t SubmitCount = 0;
        uint32_t StartCount = 0;
        uint32_t StopCount = 0;
        uint32_t QuiesceCount = 0;
        uint32_t DestroyCount = 0;
        uint32_t ShutdownCount = 0;
        uint64_t ShutdownEpoch = 0;
        Modules::Audio::VoiceHandle LastHandle;
        const uint8_t* SubmittedData = nullptr;
        size_t SubmittedSize = 0;
        Modules::Audio::Private::IAudioBackendEventSink* EventSink = nullptr;
        Thread::Atomic<bool>* pReleaseDelayedCallback = nullptr;
        Thread::Atomic<bool>* pDelayedCallbackComplete = nullptr;
        Core::Container::VariableArray<FakeAudioMailboxBinding> Mailboxes;
        Core::Container::VariableArray<FakeAudioOperation> Operations;
    };
} // namespace NorvesLib::Test::Audio
