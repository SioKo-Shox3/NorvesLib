#pragma once

#include "Container/VariableArray.h"
#include "Input/GamepadTypes.h"
#include "Input/HapticsEnvelopeMath.h"
#include "Input/HapticsOutputState.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core::Input
{
    struct HapticsEffectDefinition
    {
        Identity Id;
        double Duration = 1;
        bool Loop = false;
        int32_t Priority = 0;
        Container::VariableArray<HapticsKeyframe> Low;
        Container::VariableArray<HapticsKeyframe> High;
        HapticsEffectView View() const
        {
            return {Duration, Loop, Priority, {Low.data(), Low.size()}, {High.data(), High.size()}};
        }
    };
    struct HapticsSettings
    {
        bool Enabled = true;
        float Strength = .5f;
        EHapticsMixMode MixMode = EHapticsMixMode::Maximum;
    };
    class IHapticsOutput
    {
    public:
        virtual ~IHapticsOutput() = default;
        virtual bool IsConnected(uint8_t slot) const noexcept = 0;
        virtual bool SetVibration(uint8_t slot, float low, float high) noexcept = 0;
    };

    // GameThread専用。効果/keyを所有し、sinkはUpdate/Flush呼出中だけ借用する。
    // sink wrapperは交換可能だが、寿命中の実backend/slot対応は同一に保つ。
    // 実backend交換は旧serviceの停止/Flushとdevice停止を済ませ、新serviceを使う。
    // ACK外で任意出力を書き換えない。ownerの強制停止はvoice取消/Flushと組にする。
    // sinkからの操作再入は拒否する。callbackから本体を破棄しない。
    // 設定/Stop/focus/pause変更は次のUpdate/Flushで送信。ownerは終了前にもFlushする。
    class HapticsService
    {
    public:
        static constexpr size_t MaximumVoices = 64;
        HapticsService() = default;
        HapticsService(const HapticsService&) = delete;
        HapticsService& operator=(const HapticsService&) = delete;
        HapticsService(HapticsService&&) = delete;
        HapticsService& operator=(HapticsService&&) = delete;

        // 無効値/重複IDは旧設定を保持。allocation例外も旧設定を保持して伝播する。
        // 成功は全voiceを取消。送信ACKは保持し、残留出力を次のFlushで止める。
        bool Configure(Container::Span<const HapticsEffectDefinition> effects);
        // 効果と全体設定を同じcandidateとして反映する。失敗時は両方を維持する。
        bool Configure(Container::Span<const HapticsEffectDefinition> effects, const HapticsSettings& settings);
        // 借用定義は次のConfigure/破棄まで。外部で保持する場合はコピーする。
        const Container::VariableArray<HapticsEffectDefinition>& GetEffects() const { return m_Effects; }
        uint64_t Play(Identity effect, uint8_t slot, float gain = 1);
        bool Stop(uint64_t handle);
        bool StopAll() noexcept;
        size_t GetActiveVoiceCount() const noexcept { return m_VoiceCount; }
        bool IsBusy() const noexcept { return m_bBusy; }
        bool SetSettings(const HapticsSettings& settings);
        HapticsSettings GetSettings() const { return m_Settings; }
        // 喪失/停止で再生を取消し、復帰時に昔の単発/loopを自動再開しない。
        bool SetFocused(bool focused) noexcept;
        bool SetPaused(bool paused) noexcept;
        bool Update(double unscaledDeltaSeconds, IHapticsOutput& output) noexcept;
        // 時間を進めず現在の要求を送る。制御変更直後の即時停止にも使用する。
        bool FlushOutputs(IHapticsOutput& output) noexcept;

    private:
        struct Voice
        {
            uint64_t Handle = 0;
            size_t EffectIndex = 0;
            uint8_t Slot = 0;
            float Gain = 1;
            double Elapsed = 0;
            bool New = true;
        };
        bool Process(double delta, bool advance, IHapticsOutput& output) noexcept;
        static bool IsValidSettings(const HapticsSettings& settings);
        Container::VariableArray<HapticsEffectDefinition> m_Effects;
        Voice m_Voices[MaximumVoices]{};
        size_t m_VoiceCount = 0;
        HapticsOutputState m_Outputs[GamepadSlotCount];
        HapticsSettings m_Settings;
        bool m_bFocused = true;
        bool m_bPaused = false;
        bool m_bBusy = false;
    };
} // namespace NorvesLib::Core::Input
