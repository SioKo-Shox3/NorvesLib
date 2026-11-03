#include "Gameplay/Combat/HealthComponent.h"

#include "Logging/LogMacros.h"

#include <cmath>

namespace NorvesLib::Modules::Gameplay::Combat
{
    IMPLEMENT_CLASS(HealthComponent, Core::Component::Component)

    HealthComponent::HealthComponent()
    {
        MaxHealth = 100.0f;
        Health = 100.0f;
        StaggerThreshold = 0.0f;
    }

    void HealthComponent::Initialize()
    {
        Super::Initialize();
        NormalizeHealthProperties();
    }

    void HealthComponent::NormalizeHealthProperties()
    {
        if (!std::isfinite(MaxHealth.Get()) || MaxHealth.Get() <= 0.0f)
        {
            NORVES_LOG_WARNING("Health", "不正な最大体力を既定値 100 に補正します。");
            MaxHealth = 100.0f;
        }

        const float health = Health.Get();
        if (!std::isfinite(health) || health < 0.0f || health > MaxHealth.Get())
        {
            NORVES_LOG_WARNING("Health", "不正な体力を最大体力の範囲内に補正します。");
            // NaN は最大体力へ、負の値は 0 へ、上限を超える値は最大体力へ戻す。
            Health = health < 0.0f ? 0.0f : MaxHealth.Get();
        }
    }

    DamageResult HealthComponent::ApplyDamage(const DamageInfo& damage)
    {
        DamageResult result{EDamageOutcome::RejectedInvalid, 0.0f, Health.Get(), Health.Get(), false};
        if (!std::isfinite(damage.Amount) || damage.Amount < 0.0f ||
            !std::isfinite(damage.StaggerAmount) || damage.StaggerAmount < 0.0f)
        {
            return result;
        }
        if (m_bDefeated)
        {
            result.Outcome = EDamageOutcome::IgnoredAlreadyDefeated;
            return result;
        }
        if (m_bInvulnerable)
        {
            result.Outcome = EDamageOutcome::IgnoredInvulnerable;
            return result;
        }

        NormalizeHealthProperties();
        result.HealthBefore = Health.Get();
        result.HealthAfter = damage.Amount >= result.HealthBefore ? 0.0f : result.HealthBefore - damage.Amount;
        result.AppliedAmount = result.HealthBefore - result.HealthAfter;
        Health = result.HealthAfter;

        if (StaggerThreshold.Get() > 0.0f)
        {
            // 保持する float の値で比較し、丸めによって閾値に達した場合も見逃さない。
            const float stagger = m_StaggerMeter + damage.StaggerAmount;
            result.bStaggerBroken = stagger >= StaggerThreshold.Get();
            m_StaggerMeter = result.bStaggerBroken ? 0.0f : stagger;
        }

        // 量が 0 の呼び出しは、初期体力が 0 の場合も Applied とする。
        const bool bDefeated = damage.Amount > 0.0f && result.HealthAfter == 0.0f;
        result.Outcome = bDefeated ? EDamageOutcome::Defeated : EDamageOutcome::Applied;
        m_bDefeated = bDefeated;

        // 通知中に追加ダメージを受けても撃破が重複しないよう、状態は通知前に確定する。
        OnDamaged.Broadcast(*this, result);
        if (result.bStaggerBroken)
        {
            OnStaggerBroken.Broadcast(*this);
        }
        if (bDefeated)
        {
            OnDefeated.Broadcast(*this);
        }
        return result;
    }

    void HealthComponent::ResetHealth()
    {
        NormalizeHealthProperties();
        Health = MaxHealth.Get();
        m_bDefeated = false;
        ResetStagger();
    }

    void HealthComponent::ResetStagger()
    {
        m_StaggerMeter = 0.0f;
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
