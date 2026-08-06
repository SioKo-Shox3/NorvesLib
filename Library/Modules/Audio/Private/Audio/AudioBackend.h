#pragma once

#include "Audio/AudioTypes.h"
#include "Container/Span.h"
#include "Thread/Atomic.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio::Private
{
    enum class AudioBackendEventKind : uint8_t
    {
        Drained,
        VoiceError,
    };

    enum class AudioBackendEventPublishResult : uint8_t
    {
        Published,
        Coalesced,
        StaleHandle,
        StaleEpoch,
        Closed,
    };

    /**
     * Stable per-voice terminal mailbox. It is allocated before backend voice
     * creation and callback publication is lock-free and allocation-free.
     */
    class AudioBackendEventMailbox
    {
    public:
        void Configure(VoiceHandle handle, uint64_t shutdownEpoch) noexcept
        {
            m_bAccepting.Store(false, std::memory_order_release);
            InvalidatePendingEvent();
            m_HandleKey.Store(PackHandle(handle), std::memory_order_relaxed);
            m_ShutdownEpoch.Store(shutdownEpoch, std::memory_order_relaxed);
            m_bAccepting.Store(true, std::memory_order_release);
        }

        void Close() noexcept
        {
            m_bAccepting.Store(false, std::memory_order_release);
            InvalidatePendingEvent();
        }

        [[nodiscard]] AudioBackendEventPublishResult TryPublish(
            VoiceHandle handle,
            uint64_t shutdownEpoch,
            AudioBackendEventKind kind) noexcept
        {
            if (!m_bAccepting.Load(std::memory_order_acquire))
            {
                return AudioBackendEventPublishResult::Closed;
            }
            if (m_HandleKey.Load(std::memory_order_relaxed) != PackHandle(handle))
            {
                return AudioBackendEventPublishResult::StaleHandle;
            }
            if (m_ShutdownEpoch.Load(std::memory_order_relaxed) != shutdownEpoch)
            {
                return AudioBackendEventPublishResult::StaleEpoch;
            }

            uint64_t state = m_State.Load(std::memory_order_acquire);
            if ((state & StateMask) != Empty)
            {
                return AudioBackendEventPublishResult::Coalesced;
            }
            if (!m_State.CompareExchangeStrong(
                    state,
                    state | Writing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return AudioBackendEventPublishResult::Coalesced;
            }

            m_EventHandle = handle;
            m_EventShutdownEpoch = shutdownEpoch;
            m_EventKind = kind;
            uint64_t writingState = state | Writing;
            if (!m_State.CompareExchangeStrong(
                    writingState,
                    state | Ready,
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                return m_bAccepting.Load(std::memory_order_acquire)
                    ? AudioBackendEventPublishResult::StaleHandle
                    : AudioBackendEventPublishResult::Closed;
            }
            return AudioBackendEventPublishResult::Published;
        }

        [[nodiscard]] bool TryConsume(VoiceHandle& outHandle,
                                      uint64_t& outShutdownEpoch,
                                      AudioBackendEventKind& outKind) noexcept
        {
            uint64_t state = m_State.Load(std::memory_order_acquire);
            if ((state & StateMask) != Ready)
            {
                return false;
            }
            if (!m_State.CompareExchangeStrong(
                    state,
                    (state & ~StateMask) | Reading,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return false;
            }

            outHandle = m_EventHandle;
            outShutdownEpoch = m_EventShutdownEpoch;
            outKind = m_EventKind;
            m_State.Store(state & ~StateMask, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool HasPendingEvent() const noexcept
        {
            return (m_State.Load(std::memory_order_acquire) & StateMask) != Empty;
        }

    private:
        static constexpr uint64_t Empty = 0;
        static constexpr uint64_t Writing = 1;
        static constexpr uint64_t Ready = 2;
        static constexpr uint64_t Reading = 3;
        static constexpr uint64_t StateMask = 3;
        static constexpr uint64_t VersionIncrement = 4;

        [[nodiscard]] static constexpr uint64_t PackHandle(VoiceHandle handle) noexcept
        {
            return (static_cast<uint64_t>(handle.Index) << 32) | handle.Generation;
        }

        void InvalidatePendingEvent() noexcept
        {
            const uint64_t state = m_State.Load(std::memory_order_relaxed);
            m_State.Store((state & ~StateMask) + VersionIncrement, std::memory_order_release);
        }

        Thread::Atomic<uint64_t> m_State{Empty};
        Thread::Atomic<uint64_t> m_HandleKey{0};
        Thread::Atomic<uint64_t> m_ShutdownEpoch{0};
        Thread::Atomic<bool> m_bAccepting{false};
        VoiceHandle m_EventHandle;
        uint64_t m_EventShutdownEpoch = 0;
        AudioBackendEventKind m_EventKind = AudioBackendEventKind::Drained;
    };

    class IAudioBackendEventSink
    {
    public:
        virtual ~IAudioBackendEventSink() = default;
        virtual void EnqueueBackendEvent(AudioBackendEventMailbox& mailbox,
                                         VoiceHandle handle,
                                         uint64_t shutdownEpoch,
                                         AudioBackendEventKind kind) noexcept = 0;
    };

    /**
     * Stop is complete only after both consumption is stopped and queued buffers
     * have been flushed. The flags expose a partial adapter failure to the domain.
     */
    struct AudioBackendStopResult
    {
        AudioResult Result = AudioResult::BackendFailure;
        bool bVoiceStopped = false;
        bool bBuffersFlushed = false;
    };

    class IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;
        virtual AudioResult Initialize(IAudioBackendEventSink& eventSink, uint64_t shutdownEpoch) = 0;
        virtual AudioResult CreateVoice(VoiceHandle handle,
                                        const AudioPcmFormat& format,
                                        AudioBackendEventMailbox& mailbox) = 0;
        virtual AudioResult SubmitVoice(VoiceHandle handle, Core::Container::Span<const uint8_t> pcmBytes) = 0;
        virtual AudioResult StartVoice(VoiceHandle handle) = 0;
        virtual AudioBackendStopResult StopVoice(VoiceHandle handle) = 0;
        virtual void QuiesceCallbacks() = 0;
        virtual AudioResult DestroyVoice(VoiceHandle handle) = 0;
        virtual void Shutdown() = 0;
    };
} // namespace NorvesLib::Modules::Audio::Private
