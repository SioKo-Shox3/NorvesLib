#pragma once

#include "Component/Component.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/DamageTypes.h"

namespace NorvesLib::Modules::Gameplay::Combat
{
    /// 倍率適用後のダメージを呼び出し順に処理し、体力とスタッガを管理する。
    class HealthComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(HealthComponent, Core::Component::Component)

    public:
        HealthComponent();
        void Initialize() override;

        /// 通知順は OnDamaged → OnStaggerBroken → OnDefeated。撃破と崩れは同時に起こりうる。
        DamageResult ApplyDamage(const DamageInfo& damage);
        void ResetHealth();
        void ResetStagger();

        void SetInvulnerable(bool bInvulnerable) { m_bInvulnerable = bInvulnerable; }
        bool IsInvulnerable() const { return m_bInvulnerable; }
        bool IsDefeated() const { return m_bDefeated; }
        float GetStaggerMeter() const { return m_StaggerMeter; }

        Core::MulticastDelegate<const HealthComponent&, const DamageResult&> OnDamaged;
        Core::MulticastDelegate<const HealthComponent&> OnDefeated;
        Core::MulticastDelegate<const HealthComponent&> OnStaggerBroken;

        PROPERTY(float, MaxHealth)
        PROPERTY(float, Health)
        PROPERTY(float, StaggerThreshold)

    private:
        // 初期体力が 0 でも通知・撃破ラッチは発生せず、ラッチは ApplyDamage だけが立てる。
        void NormalizeHealthProperties();

        float m_StaggerMeter = 0.0f;
        bool m_bInvulnerable = false;
        bool m_bDefeated = false;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
