#include "Input/HapticsService.h"
#include "Input/HapticsPlaybackTime.h"
#include "Thread/Atomic.h"
#include <limits>

namespace NorvesLib::Core::Input
{
    namespace
    {
        uint64_t NextHapticsHandle()
        {
            // 別service/再構成後の古いhandleを誤適用せず、wrapしない。
            static NorvesLib::Thread::Atomic<uint64_t> next{1};
            auto value = next.Load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == std::numeric_limits<uint64_t>::max())
                {
                    return 0;
                }
                if (next.CompareExchangeWeak(value, value + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    return value;
                }
            }
        }
        struct HapticsCallGuard
        {
            explicit HapticsCallGuard(bool& busy) : Busy(busy) { Busy = true; }
            ~HapticsCallGuard() { Busy = false; }
            bool& Busy;
        };
    }
    bool HapticsService::IsValidSettings(const HapticsSettings& settings)
    {
        return std::isfinite(settings.Strength) && settings.Strength >= 0 && settings.Strength <= 1 &&
            (settings.MixMode == EHapticsMixMode::Maximum || settings.MixMode == EHapticsMixMode::AddClamp);
    }
    bool HapticsService::Configure(Container::Span<const HapticsEffectDefinition> effects)
    {
        return Configure(effects, m_Settings);
    }
    bool HapticsService::Configure(Container::Span<const HapticsEffectDefinition> effects, const HapticsSettings& settings)
    {
        if (m_bBusy || !IsValidSettings(settings))
        {
            return false;
        }
        HapticsCallGuard guard(m_bBusy);
        Container::VariableArray<HapticsEffectDefinition> candidate;
        candidate.reserve(effects.size());
        for (const auto& effect : effects)
        {
            if (!effect.Id.IsValid() || !IsValidHapticsEffect(effect.View()))
            {
                return false;
            }
            for (const auto& existing : candidate)
            {
                if (existing.Id == effect.Id)
                {
                    return false;
                }
            }
            candidate.push_back(effect);
        }
        m_Effects.swap(candidate);
        m_Settings = settings;
        m_VoiceCount = 0;
        return true;
    }
    uint64_t HapticsService::Play(Identity effect, uint8_t slot, float gain)
    {
        if (m_bBusy || !effect.IsValid() || !m_Settings.Enabled || !m_bFocused || m_bPaused || slot >= GamepadSlotCount ||
            !std::isfinite(gain) || gain < 0 || gain > 1 || m_VoiceCount >= MaximumVoices)
        {
            return 0;
        }
        for (size_t index = 0; index < m_Effects.size(); ++index)
        {
            if (m_Effects[index].Id == effect)
            {
                const uint64_t handle = NextHapticsHandle();
                if (handle == 0)
                {
                    return 0;
                }
                m_Voices[m_VoiceCount++] = {handle, index, slot, gain, 0, true};
                return handle;
            }
        }
        return 0;
    }
    bool HapticsService::Stop(uint64_t handle)
    {
        if (m_bBusy || handle == 0)
        {
            return false;
        }
        for (size_t index = 0; index < m_VoiceCount; ++index)
        {
            if (m_Voices[index].Handle == handle)
            {
                for (size_t tail = index + 1; tail < m_VoiceCount; ++tail)
                {
                    m_Voices[tail - 1] = m_Voices[tail];
                }
                --m_VoiceCount;
                return true;
            }
        }
        return false;
    }
    bool HapticsService::StopAll() noexcept
    {
        if (m_bBusy)
        {
            return false;
        }
        m_VoiceCount = 0;
        return true;
    }
    bool HapticsService::SetSettings(const HapticsSettings& settings)
    {
        if (m_bBusy || !IsValidSettings(settings))
        {
            return false;
        }
        m_Settings = settings;
        if (!settings.Enabled)
        {
            m_VoiceCount = 0;
        }
        return true;
    }
    bool HapticsService::SetFocused(bool focused) noexcept
    {
        if (m_bBusy)
        {
            return false;
        }
        m_bFocused = focused;
        if (!focused)
        {
            m_VoiceCount = 0;
        }
        return true;
    }
    bool HapticsService::SetPaused(bool paused) noexcept
    {
        if (m_bBusy)
        {
            return false;
        }
        m_bPaused = paused;
        if (paused)
        {
            m_VoiceCount = 0;
        }
        return true;
    }
    bool HapticsService::Update(double unscaledDeltaSeconds, IHapticsOutput& output) noexcept
    {
        return Process(unscaledDeltaSeconds, true, output);
    }
    bool HapticsService::FlushOutputs(IHapticsOutput& output) noexcept
    {
        return Process(0, false, output);
    }
    bool HapticsService::Process(double delta, bool advance, IHapticsOutput& output) noexcept
    {
        if (m_bBusy || !std::isfinite(delta) || delta < 0)
        {
            return false;
        }
        HapticsCallGuard guard(m_bBusy);
        bool connected[GamepadSlotCount]{};
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            connected[slot] = output.IsConnected(slot);
        }
        Voice next[MaximumVoices]{};
        size_t nextCount = 0;
        HapticsMixSample samples[GamepadSlotCount][MaximumVoices]{};
        size_t sampleCounts[GamepadSlotCount]{};
        for (size_t index = 0; index < m_VoiceCount; ++index)
        {
            auto voice = m_Voices[index];
            if (!connected[voice.Slot] || !m_Settings.Enabled || !m_bFocused || m_bPaused)
            {
                continue;
            }
            const auto effect = m_Effects[voice.EffectIndex].View();
            bool finished = false;
            if (advance && !voice.New && !AdvanceHapticsPlaybackTime(effect.Duration, effect.Loop,
                voice.Elapsed, delta, voice.Elapsed, finished))
            {
                return false;
            }
            if (finished)
            {
                continue;
            }
            // Playがそのframe内に発生した場合、前frameのdtを遡って加算しない。
            if (advance)
            {
                voice.New = false;
            }
            HapticsOutput value;
            if (!EvaluateHapticsEffect(effect, voice.Elapsed, value))
            {
                return false;
            }
            value.Low *= voice.Gain;
            value.High *= voice.Gain;
            samples[voice.Slot][sampleCounts[voice.Slot]++] = {value, effect.Priority, true};
            next[nextCount++] = voice;
        }
        HapticsOutput desired[GamepadSlotCount]{};
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            if (!MixHapticsSamples({samples[slot], sampleCounts[slot]}, m_Settings.MixMode,
                m_Settings.Strength, desired[slot]))
            {
                return false;
            }
        }
        // 全計算成功後だけvoice時刻をcommit。以降はnoexceptなsinkへの送信だけ。
        for (size_t index = 0; index < nextCount; ++index)
        {
            m_Voices[index] = next[index];
        }
        m_VoiceCount = nextCount;
        bool success = true;
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            auto& state = m_Outputs[slot];
            if (!connected[slot] && !state.MayBeActive() && !state.IsUncertain())
            {
                continue;
            }
            if (state.NeedsSend(desired[slot]))
            {
                const bool accepted = output.SetVibration(slot, desired[slot].Low, desired[slot].High);
                (void)state.RecordAttempt(desired[slot], accepted);
                if (!accepted)
                {
                    success = false;
                }
            }
        }
        return success;
    }
} // namespace NorvesLib::Core::Input
