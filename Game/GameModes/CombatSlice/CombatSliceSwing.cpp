#include "CombatSliceSwing.h"

#include "CombatSliceTypes.h"
#include "Component/SocketAttachmentComponent.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Logging/LogMacros.h"
#include "Math/Quaternion.h"
#include "Object/Entity.h"
#include "Object/World.h"

#include <algorithm>
#include <cmath>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    namespace M = NorvesLib::Math;
    namespace P = CombatSliceSwingParams;

    namespace
    {
        constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;

        void SetYaw(Entity& dog, float yawDegrees)
        {
            dog.SetRotation(M::Quaternion(M::Vector3::UnitY, yawDegrees * DegreesToRadians));
        }
    }

    bool CombatSliceSwing::Start()
    {
        if (m_State != State::Idle && m_State != State::Finished)
        {
            return false;
        }
        m_ElapsedSeconds = 0.0;
        m_State = State::WaitingForAttachment;
        return true;
    }

    void CombatSliceSwing::Tick(GameMode::GameModeContext& ctx, const CombatSliceSceneIds& ids, float worldDt)
    {
        if (m_State == State::Idle || m_State == State::Finished || m_State == State::Failed)
        {
            return;
        }

        auto* dog = ctx.WorldRef.FindEntityByObjectId(ids.Dog);
        auto* sword = ctx.WorldRef.FindEntityByObjectId(ids.Sword);
        auto* hitbox = sword ? sword->GetComponent<Combat::HitboxComponent>() : nullptr;
        auto* attachment = sword ? sword->GetComponent<Component::SocketAttachmentComponent>() : nullptr;
        const auto fail = [&](const char* reason)
        {
            if (hitbox)
            {
                hitbox->EndAttack();
            }
            m_State = State::Failed;
            NORVES_LOG_ERROR("CombatSlice", "%s", reason);
        };

        if (!dog || !hitbox || !attachment || hitbox->IsPendingDestroy() || attachment->IsPendingDestroy())
        {
            fail("犬、剣、または剣のコンポーネントを取得できません。");
            return;
        }

        if (m_State == State::WaitingForAttachment)
        {
            if (attachment->GetAttachState() != Component::AttachState::Attached)
            {
                return;
            }
            m_YawDegrees = P::StartYawDegrees;
            SetYaw(*dog, m_YawDegrees);
            if (!hitbox->BeginAttack())
            {
                fail("剣の攻撃判定を開始できません。");
                return;
            }
            // 開始フレームは現在の姿勢だけを判定し、次のフレームから掃引する。
            m_State = State::Sweeping;
            return;
        }

        if (attachment->GetAttachState() != Component::AttachState::Attached)
        {
            fail("攻撃中に剣が取り付け状態でなくなりました。");
            return;
        }
        if (!hitbox->IsWindowOpen())
        {
            fail("攻撃中に剣の判定窓が閉じました。");
            return;
        }
        if (m_State == State::FinalPose)
        {
            // 最終姿勢の判定が LateTick で終わった次のフレームに閉じる。
            hitbox->EndAttack();
            ++m_CompletedSwingCount;
            m_State = State::Finished;
            return;
        }
        if (!std::isfinite(worldDt) || worldDt < 0.0f)
        {
            fail("World の経過時間が不正です。");
            return;
        }

        m_ElapsedSeconds += static_cast<double>(worldDt);
        const double progress = std::clamp(m_ElapsedSeconds / static_cast<double>(P::DurationSeconds), 0.0, 1.0);
        m_YawDegrees = static_cast<float>(static_cast<double>(P::StartYawDegrees) +
            static_cast<double>(P::EndYawDegrees - P::StartYawDegrees) * progress);
        SetYaw(*dog, m_YawDegrees);
        if (m_ElapsedSeconds >= static_cast<double>(P::DurationSeconds))
        {
            m_State = State::FinalPose;
        }
    }

    bool CombatSliceSwing::IsSwinging() const
    {
        return m_State == State::Sweeping || m_State == State::FinalPose;
    }

    bool CombatSliceSwing::IsFinished() const
    {
        return m_State == State::Finished;
    }

    bool CombatSliceSwing::IsWindowOpen() const
    {
        return IsSwinging();
    }

    bool CombatSliceSwing::IsFailed() const
    {
        return m_State == State::Failed;
    }

    uint32_t CombatSliceSwing::GetCompletedSwingCount() const
    {
        return m_CompletedSwingCount;
    }

    float CombatSliceSwing::GetYawDegrees() const
    {
        return m_YawDegrees;
    }

    void CombatSliceSwing::Reset(GameMode::GameModeContext& ctx, const CombatSliceSceneIds& ids)
    {
        auto* sword = ctx.WorldRef.FindEntityByObjectId(ids.Sword);
        auto* hitbox = sword ? sword->GetComponent<Combat::HitboxComponent>() : nullptr;
        if (hitbox)
        {
            hitbox->EndAttack();
        }
        m_YawDegrees = P::StartYawDegrees;
        if (auto* dog = ctx.WorldRef.FindEntityByObjectId(ids.Dog))
        {
            SetYaw(*dog, m_YawDegrees);
        }
        m_ElapsedSeconds = 0.0;
        m_State = State::Idle;
    }
} // namespace Game::GameModes
