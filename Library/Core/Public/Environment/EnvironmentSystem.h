// GameThread の環境状態、固定刻みの遷移と描画への公開を管理する。
#pragma once

#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"
#include "Environment/EnvironmentState.h"
#include "Particle/VfxEffectSystem.h"
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
     * 次のフレームで上書きされる。有効中の天気と太陽は SetTarget または SetImmediate で変える。
     * 解決には Enable で受けた場面の土台を使い、状態にある欄だけを重ねて大気定数などを保持する。
     * Enable で RainTuning.json を読み、上書き後の雨量に応じた霧の密度の増分を Resolve で足す。
     * Enable 後の最初の PublishTo で雨の調整値を一度渡す。
     * 近距離の雨は雨量と連動する効果として管理し、毎フレーム PublishTo の後で更新する。
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

        void Enable(const EnvironmentState& initial, const EnvironmentSceneBase& base);
        void Disable();
        bool IsEnabled() const;

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

        /// 有効で雨量 > 0 の間だけ近距離の雨の効果のインスタンスを 1 つ持ち、
        /// 雨量・風・調整値から求めた値を変わったときに渡す。
        /// 雨量 0・無効・効果が読めないときは破棄する。毎フレーム PublishTo の後に呼ぶ。
        void UpdateRainNear(Particle::VfxEffectSystem& effects);

        /// 状態を変える命令のたびに、変化の種類と上書き後の状態を通知する。
        MulticastDelegate<EEnvironmentChange, const EnvironmentState&>& OnChanged();

    private:
        enum class RainNearParameter : uint32_t
        {
            BoxHalfWidth,
            BoxHeight,
            VelocityMin,
            VelocityMax,
            Color,
            Size,
            Acceleration,
            Fraction,
            Shutter,
            FadeDistance,
            Count
        };

        struct RainNearParameterState
        {
            Particle::VfxValue Value;
            bool bSent = false;
        };

        struct RainNearModules
        {
            uint32_t Box = 0;
            uint32_t Velocity = 0;
            uint32_t Color = 0;
            uint32_t Size = 0;
            uint32_t Acceleration = 0;
            uint32_t Wind = 0;
            uint32_t Activation = 0;
            uint32_t Renderer = 0;
        };

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

        bool LoadRainNearEffect(Container::String& error);
        void SetRainNearParameter(Particle::VfxEffectSystem& effects, RainNearParameter parameter,
            uint32_t moduleIndex, const char* name, const Particle::VfxValue& value);
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
        Particle::VfxEffectAsset m_RainNearAsset;
        RainNearModules m_RainNearModules;
        Particle::VfxEffectHandle m_RainNearHandle;
        RainNearParameterState m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::Count)];
        float m_RainNearWindCoupling = 0.0f;
        float m_RainNearWindScale = 0.0f;
        bool m_bRainNearAssetReady = false;
        bool m_bRainNearRecreatePending = false;
        bool m_bRainNearCreateWarning = false;
        bool m_bRainNearParameterWarning = false;
        /// プリセットの根。既定の空文字列は作業ディレクトリを表す。
        Container::String m_PresetRoot;
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
