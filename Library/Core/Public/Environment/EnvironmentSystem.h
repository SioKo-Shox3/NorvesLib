// GameThread の環境状態、固定刻みの遷移と描画への公開を管理する。
#pragma once

#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"
#include "Environment/EnvironmentState.h"

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
     * 次のフレームで上書きされる。有効中の天気と太陽は SetTarget または SetImmediate で変える。
     * 解決には Enable で受けた場面の土台を使い、状態にある欄だけを重ねて大気定数などを保持する。
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

        void Enable(const EnvironmentState& initial, const EnvironmentSceneBase& base);
        void Disable();
        bool IsEnabled() const;

        void SetTarget(const EnvironmentTarget& target);
        void SetImmediate(const EnvironmentTarget& target);
        void SnapToTarget();
        void LoadState(const EnvironmentState& state);
        void SkipTime(double seconds);
        void Tick(float worldDeltaSeconds);

        /// 上書きの層を掛けた後の状態。読み手はこれを読む。
        const EnvironmentState& GetState() const;
        /// 上書きの層を掛ける前の状態。
        const EnvironmentState& GetBaseState() const;
        const EnvironmentSceneBase& GetSceneBase() const;
        /// 上書きの層を掛けた状態を場面の土台へ重ねて解決した描画用の値。
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

        void ClearTransitions();
        float EvaluateField(EEnvironmentField field, uint64_t step) const;
        void RefreshFields();
        void RebuildEffective();
        void StepOnce();
        void AdvanceNanoseconds(uint64_t nanoseconds);
        void Notify(EEnvironmentChange change);

        bool m_bEnabled = false;
        bool m_bRestorePending = false;
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
