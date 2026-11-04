#include "Gameplay/Combat/CombatRuntime.h"

#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/GameplayModule.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Combat
{
    CombatRuntime::CombatRuntime(GameplayModule& module)
        : m_Module(module)
    {
    }

    CombatRuntime::~CombatRuntime()
    {
        ResetTransientState();
        for (auto& entry : m_Hitboxes)
        {
            entry.Component->m_Runtime = nullptr;
            entry.Component->m_RegistrationIndex = 0;
        }
        for (auto& entry : m_HurtboxSets)
        {
            entry.Component->m_Runtime = nullptr;
            entry.Component->m_RegistrationIndex = 0;
        }
    }

    bool CombatRuntime::IsOwnerThread() const
    {
        return m_Module.IsOwnerThread();
    }

    bool CombatRuntime::Register(HitboxComponent& component)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        auto* owner = component.GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (!component.m_bCombatReady || component.IsPendingDestroy() || owner == nullptr || owner->IsPendingDestroy() ||
            world == nullptr || GameplayModule::FindBoundModule(*world) != &m_Module ||
            !owner->GetEntityHandle().IsValid() || (component.m_Runtime != nullptr && component.m_Runtime != this))
        {
            return false;
        }
        if (component.m_Runtime == this)
        {
            for (const auto& entry : m_Hitboxes)
            {
                if (entry.Component == &component && entry.Owner == owner->GetEntityHandle())
                {
                    return true;
                }
            }
            Unregister(component);
        }
        if (m_NextRegistrationIndex == 0)
        {
            return false;
        }
        const uint64_t index = m_NextRegistrationIndex++;
        m_Hitboxes.push_back({&component, owner->GetEntityHandle(), index});
        component.m_Runtime = this;
        component.m_RegistrationIndex = index;
        if (component.m_bOwnerBinding)
        {
            component.m_Binding = owner->GetEntityHandle();
        }
        return true;
    }

    bool CombatRuntime::Register(HurtboxSetComponent& component)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        auto* owner = component.GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (!component.m_bCombatReady || component.IsPendingDestroy() || owner == nullptr || owner->IsPendingDestroy() ||
            world == nullptr || GameplayModule::FindBoundModule(*world) != &m_Module ||
            !owner->GetEntityHandle().IsValid() || (component.m_Runtime != nullptr && component.m_Runtime != this))
        {
            return false;
        }
        if (component.m_Runtime == this)
        {
            for (const auto& entry : m_HurtboxSets)
            {
                if (entry.Component == &component && entry.Owner == owner->GetEntityHandle())
                {
                    return true;
                }
            }
            Unregister(component);
        }
        if (m_NextRegistrationIndex == 0)
        {
            return false;
        }
        const uint64_t index = m_NextRegistrationIndex++;
        m_HurtboxSets.push_back({&component, owner->GetEntityHandle(), index});
        component.m_Runtime = this;
        component.m_RegistrationIndex = index;
        for (auto& part : component.m_Parts)
        {
            if (part.bOwnerBinding)
            {
                part.Binding = owner->GetEntityHandle();
            }
        }
        return true;
    }

    void CombatRuntime::Unregister(HitboxComponent& component)
    {
        if (!IsOwnerThread() || component.m_Runtime != this)
        {
            return;
        }
        for (auto it = m_Hitboxes.begin(); it != m_Hitboxes.end(); ++it)
        {
            if (it->Component == &component)
            {
                m_Hitboxes.erase(it);
                break;
            }
        }
        component.ResetWindow();
        component.m_Runtime = nullptr;
        component.m_RegistrationIndex = 0;
    }

    void CombatRuntime::Unregister(HurtboxSetComponent& component)
    {
        if (!IsOwnerThread() || component.m_Runtime != this)
        {
            return;
        }
        for (auto it = m_HurtboxSets.begin(); it != m_HurtboxSets.end(); ++it)
        {
            if (it->Component == &component)
            {
                m_HurtboxSets.erase(it);
                break;
            }
        }
        for (auto& part : component.m_Parts)
        {
            part.PreviousTransform = {};
            part.bPreviousValid = false;
        }
        component.m_Runtime = nullptr;
        component.m_RegistrationIndex = 0;
    }

    uint64_t CombatRuntime::AllocateAttackInstanceId()
    {
        if (!IsOwnerThread() || m_NextAttackInstanceId == 0)
        {
            return 0;
        }
        return m_NextAttackInstanceId++;
    }

    void CombatRuntime::ResetTransientState()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        for (auto& entry : m_Hitboxes)
        {
            entry.Component->ResetWindow();
        }
        for (auto& entry : m_HurtboxSets)
        {
            for (auto& part : entry.Component->m_Parts)
            {
                part.PreviousTransform = {};
                part.bPreviousValid = false;
            }
        }
        m_WorldTime = 0.0;
        m_FrameNumber = 0;
        // インスタンスと登録の通し番号は再利用しない。
    }

    void CombatRuntime::Evaluate(Core::World&, float)
    {
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
