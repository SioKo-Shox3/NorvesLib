// 当たりをエフェクトとヒットストップの要求に変換する。
#include "Gameplay/Combat/CombatResponse.h"

#include "Engine/Engine.h"
#include "Gameplay/Combat/CombatRuntime.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Logging/LogMacros.h"

#include <cmath>

namespace NorvesLib::Modules::Gameplay::Combat
{
    CombatResponse::CombatResponse(CombatRuntime& runtime)
        : m_Runtime(runtime)
    {
        m_Runtime.OnAnyHit.Add(this, &CombatResponse::HandleHit);
    }

    CombatResponse::~CombatResponse()
    {
        m_Runtime.OnAnyHit.Remove(this, &CombatResponse::HandleHit);
    }

    void CombatResponse::HandleHit(const CombatHitEvent& event)
    {
        if (m_Engine == nullptr)
        {
            return;
        }
        if (m_Settings.bHitEffects)
        {
            Core::Particle::HitEffectHit hit;
            hit.Position = event.Point;
            hit.Normal = event.Normal.Normalized();
            if (const auto* hurtbox = m_Runtime.FindHurtboxSet(event.HurtboxIndex))
            {
                for (const auto& part : hurtbox->GetParts())
                {
                    if (part.PartId == event.PartId)
                    {
                        hit.SurfaceName = part.SurfaceName.ToString();
                        break;
                    }
                }
            }
            m_Engine->GetParticleSystem().GetHitEffects().SubmitHit(hit);
        }
        if (m_Settings.bHitStop)
        {
            if (const auto* hitbox = m_Runtime.FindHitbox(event.HitboxIndex))
            {
                const auto& profile = hitbox->GetProfile();
                if (Core::Container::IsValid(profile))
                {
                    m_MaxHitStopHint = std::fmax(m_MaxHitStopHint, profile->HitStopSecondsHint);
                }
            }
        }
    }

    void CombatResponse::Flush()
    {
        const float seconds = std::fmin(m_MaxHitStopHint, m_Settings.MaxHitStopSeconds);
        Reset();
        if (m_Engine == nullptr || !m_Settings.bHitStop || seconds <= 0.0f)
        {
            return;
        }
        Core::Engine::TimeScaleRequest request;
        request.Channels = Core::Engine::TimeChannelBit(Core::Engine::TimeChannel::World);
        request.Scale = m_Settings.HitStopTimeScale;
        request.DurationSeconds = seconds;
        request.Tag = Core::Identity("CombatHitStop");
        Core::Engine::TimeScaleHandle handle;
        if (m_Engine->GetTimeSystem().PushScale(request, handle) != Core::Engine::TimeSystemResult::Success)
        {
            NORVES_LOG_WARNING("Gameplay", "ヒットストップの倍率を要求できませんでした。");
        }
    }

    void CombatResponse::Reset()
    {
        m_MaxHitStopHint = 0.0f;
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
