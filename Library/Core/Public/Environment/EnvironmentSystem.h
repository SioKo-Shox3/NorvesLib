// GameThread の環境状態、固定刻みの遷移と描画への公開を管理する。
#pragma once

#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"
#include "Environment/EnvironmentState.h"
#include "Environment/SunPath.h"
#include "Environment/WeatherSchedule.h"
#include "Rendering/RainModel.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Core::Environment
{
    enum class EEnvironmentChange : uint8_t
    {
        Enabled,
        Disabled,
        TargetSet,
        Immediate,
        Snapped,
        Loaded,
        TimeSkipped,
        OverridePushed,
        OverridePopped
    };

    using EnvironmentOverrideScope = uint32_t;

    /**
     * @brief GameThread 専用の気象の正本。
     *
     * 既定では無効で、無効の間は空・霧・雲の層・風を書き込まず、シーンの直接設定が効く。
     * Enable 後はポーズ中も毎フレーム、空・霧・雲の層・風・環境を書き出すため、シーンの直接設定は
     * 次のフレームで上書きされる。Manual では天気と太陽を SetTarget または SetImmediate で変える。
     * Clock では GameTimeSeconds / 3600 を 24 時間に巻き、太陽は SunPath、天候は種から作る予定表で決める。
     * ClockTimeScale は Clock の Tick の進みにだけ掛け、手動の太陽は上書きの層で重ねる。
     * Clock が太陽を動かす間だけ、SceneRevision の太陽の量子化の幅を 1 度にする。
     * 解決には Enable で受けた場面の土台を使い、状態にある欄だけを重ねて大気定数などを保持する。
     * Enable で RainTuning.json を読み、上書き後の雨量に応じた霧の密度の増分を Resolve で足す。
     * Enable 後の最初の PublishTo で雨の調整値を一度渡す。
     * 霧と雨の調整値はシーンの revision に入らない。
     * Disable 後の最初の PublishTo は環境の値（EnvironmentParameters）だけを既定値へ一度戻す。
     * 空・霧・雲の層・風は書き戻さず、同じフレームで次のシーンが書いた値を残す。
     *
     * Tick には TimeSystem の World チャンネルの dt を渡し、ポーズの門の中でのみ呼ぶ。
     * World の倍率は遷移と濡れにも適用され、FramePacket::TotalTime の壁時計は使わない。
     * SetImmediate・SnapToTarget・LoadState・SkipTime は描画履歴を切る DiscontinuityId を増やす。
     * SetTarget と上書きの層は増やさない。無効中の状態変更命令は無視して警告する。
     */
    class EnvironmentSystem
    {
    public:
        static constexpr uint64_t StepsPerSecond = 30;
        static constexpr double MaxTickDeltaSeconds = 1.0;
        static constexpr double MaxSkipSeconds = 86400.0;
        /// 時計が太陽を動かすときに SceneRevision へ渡す量子化の幅（度）。
        static constexpr float ClockSunRevisionQuantizationDegrees = 1.0f;
        /// 手動の太陽の上書きに使う上書きの層の識別子。
        static constexpr EnvironmentOverrideScope ManualSunOverrideScope = 0xFFFFFFFFu;

        void Enable(const EnvironmentState& initial, const EnvironmentSceneBase& base);
        void Disable();
        bool IsEnabled() const;
        /// 有効で、太陽の指定が Clock なら true。
        bool IsClockActive() const;
        /// 時計の時刻（時、[0, 24)）。GameTimeSeconds / 3600 を 24 時間に巻いた値。
        double GetClockHours() const;
        /// 手動の太陽を上書きの層で重ねる。Clock の太陽より勝つ。DiscontinuityId は進めない。
        void SetManualSunOverride(float altitudeDegrees, float azimuthDegrees);
        /// 手動の太陽の上書きを外す。
        void ClearManualSunOverride();

        void SetTarget(const EnvironmentTarget& target);
        /// プリセットの根を設定する。無効中も受け付け、有効・無効の切り替え後も保持する。空なら作業ディレクトリを使う。
        void SetPresetRoot(const Container::String& rootDirectory);
        /// プリセットの根を返す。空なら作業ディレクトリを使う。
        const Container::String& GetPresetRoot() const;
        /// 名前のプリセットを読み、遷移の目標として SetTarget へ渡す。DiscontinuityId は進めない。
        bool ApplyPreset(const Container::String& name);
        void SetImmediate(const EnvironmentTarget& target);
        void SnapToTarget();
        /// JSON から読んだ状態（FromJson）を当てる口。DiscontinuityId を 1 進める。
        void LoadState(const EnvironmentState& state);
        void SkipTime(double seconds);
        void Tick(float worldDeltaSeconds);

        /// 上書きの層を掛けた後の状態。読み手はこれを読む。
        const EnvironmentState& GetState() const;
        /// 上書きの層を掛ける前の状態。
        const EnvironmentState& GetBaseState() const;
        const EnvironmentSceneBase& GetSceneBase() const;
        /// 現在の雨の調整値を返す。
        const Rendering::RainTuning& GetRainTuning() const;
        /// 上書きの層を掛けた状態を場面の土台へ重ね、霧の密度に雨の増分を足した描画用の値。
        EnvironmentResolved Resolve() const;

        bool PushOverride(EnvironmentOverrideScope scope, const EnvironmentTarget& target);
        bool PopOverride(EnvironmentOverrideScope scope);

        void PublishTo(Rendering::RenderWorld& renderWorld);

        /// 状態を変える命令のたびに、変化の種類と上書き後の状態を通知する。
        MulticastDelegate<EEnvironmentChange, const EnvironmentState&>& OnChanged();

    private:
        struct FieldTransition
        {
            bool bActive = false;
            float From = 0.0f;
            float To = 0.0f;
            uint64_t StartStep = 0;
            float DurationSeconds = 0.0f;
            ETransitionCurve Curve = ETransitionCurve::SmoothStep;
        };

        struct OverrideEntry
        {
            EnvironmentOverrideScope Scope;
            EnvironmentTarget Target;
        };

        void LoadClockData();
        void RebuildWeatherTrack();
        void ApplyClock();
        bool EvaluateClockWeather(double gameTimeSeconds, EnvironmentTarget& outTarget) const;
        float ScheduledRainAt(double gameTimeSeconds) const;
        void ClearTransitions();
        float EvaluateField(EEnvironmentField field, uint64_t step) const;
        void RefreshFields();
        void RebuildEffective();
        void StepOnce();
        void AdvanceNanoseconds(uint64_t nanoseconds);
        void Notify(EEnvironmentChange change);

        bool m_bEnabled = false;
        bool m_bRestorePending = false;
        bool m_bRainTuningPublishPending = false;
        Rendering::RainTuning m_RainTuning = Rendering::MakeDefaultRainTuning();
        /// プリセットの根。既定の空文字列は作業ディレクトリを表す。
        Container::String m_PresetRoot;
        SunPathSettings m_SunPath;
        WeatherScheduleSettings m_WeatherSchedule;
        Container::VariableArray<EnvironmentTarget> m_WeatherPresets;
        WeatherTrack m_WeatherTrack;
        bool m_bWeatherReady = false;
        EnvironmentSceneBase m_SceneBase;
        EnvironmentState m_BaseState;
        EnvironmentState m_EffectiveState;
        uint64_t m_TotalNanoseconds = 0;
        uint64_t m_StepCount = 0;
        FieldTransition m_Transitions[EnvironmentFieldCount] = {};
        Container::VariableArray<OverrideEntry> m_Overrides;
        MulticastDelegate<EEnvironmentChange, const EnvironmentState&> m_OnChanged;
    };
} // namespace NorvesLib::Core::Environment
