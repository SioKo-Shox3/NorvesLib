#include "Physics/ColliderComponent.h"

#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        constexpr uint32_t kCallbackChannelShift = 30;
        constexpr uint32_t kCallbackLocalIndexMask = (1u << kCallbackChannelShift) - 1u;

        PhysicsModule* FindRegisteredPhysicsModule()
        {
            return dynamic_cast<PhysicsModule*>(
                FindPhysicsModule(Core::Module::GetModuleRegistry()));
        }

        bool TryAdvanceGeneration(uint32_t& generation)
        {
            if (generation == UINT32_MAX)
            {
                return false;
            }

            ++generation;
            return true;
        }

        PhysicsCallbackHandle MakeCallbackHandle(uint32_t channel, uint32_t index, uint32_t generation)
        {
            return PhysicsCallbackHandle{(channel << kCallbackChannelShift) | index, generation};
        }

        bool DecodeCallbackHandle(PhysicsCallbackHandle handle, uint32_t expectedChannel, uint32_t& outIndex)
        {
            if (!handle.IsValid() || (handle.Index >> kCallbackChannelShift) != expectedChannel)
            {
                return false;
            }

            outIndex = handle.Index & kCallbackLocalIndexMask;
            return true;
        }
    } // namespace

    IMPLEMENT_CLASS(ColliderComponent, Core::Component::Component)

    EPhysicsResult ColliderComponent::SetSphere(float radius)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetColliderSphere(*this, radius) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult ColliderComponent::SetBox(const Math::Vector3& halfExtents)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetColliderBox(*this, halfExtents) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult ColliderComponent::SetCapsule(float radius, float halfHeight)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetColliderCapsule(*this, radius, halfHeight) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult ColliderComponent::SetTrigger(bool bTrigger)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetColliderTrigger(*this, bTrigger) : EPhysicsResult::NotRegistered;
    }

    Core::Scene::ColliderHandle ColliderComponent::GetColliderHandle() const
    {
        return m_ColliderHandle;
    }

    EPhysicsResult ColliderComponent::AddOnOverlapBegin(
        Core::Delegate<void, const PhysicsContactEvent&> callback,
        PhysicsCallbackHandle& outHandle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }
        outHandle = PhysicsCallbackHandle{};
        if (!callback.IsBound())
        {
            return EPhysicsResult::InvalidArgument;
        }

        uint32_t index = 0;
        if (!m_FreeOverlapBeginCallbackIndices.empty())
        {
            index = m_FreeOverlapBeginCallbackIndices.back();
            m_FreeOverlapBeginCallbackIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_OverlapBeginCallbacks.size());
            m_OverlapBeginCallbacks.emplace_back();
        }

        CallbackSlot& slot = m_OverlapBeginCallbacks[index];
        slot.Callback = callback;
        slot.bOccupied = true;
        outHandle = MakeCallbackHandle(0, index, slot.Generation);
        return EPhysicsResult::Success;
    }

    EPhysicsResult ColliderComponent::RemoveOnOverlapBegin(PhysicsCallbackHandle handle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }

        uint32_t index = 0;
        if (!DecodeCallbackHandle(handle, 0, index) || index >= m_OverlapBeginCallbacks.size())
        {
            return EPhysicsResult::NotRegistered;
        }

        CallbackSlot& slot = m_OverlapBeginCallbacks[index];
        if (!slot.bOccupied || slot.Generation != handle.Generation)
        {
            return EPhysicsResult::NotRegistered;
        }

        slot.Callback.Clear();
        slot.bOccupied = false;
        if (!TryAdvanceGeneration(slot.Generation))
        {
            slot.bRetired = true;
            return EPhysicsResult::Success;
        }
        m_FreeOverlapBeginCallbackIndices.push_back(index);
        return EPhysicsResult::Success;
    }

    EPhysicsResult ColliderComponent::AddOnOverlapEnd(
        Core::Delegate<void, const PhysicsContactEvent&> callback,
        PhysicsCallbackHandle& outHandle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }
        outHandle = PhysicsCallbackHandle{};
        if (!callback.IsBound())
        {
            return EPhysicsResult::InvalidArgument;
        }

        uint32_t index = 0;
        if (!m_FreeOverlapEndCallbackIndices.empty())
        {
            index = m_FreeOverlapEndCallbackIndices.back();
            m_FreeOverlapEndCallbackIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_OverlapEndCallbacks.size());
            m_OverlapEndCallbacks.emplace_back();
        }

        CallbackSlot& slot = m_OverlapEndCallbacks[index];
        slot.Callback = callback;
        slot.bOccupied = true;
        outHandle = MakeCallbackHandle(1, index, slot.Generation);
        return EPhysicsResult::Success;
    }

    EPhysicsResult ColliderComponent::RemoveOnOverlapEnd(PhysicsCallbackHandle handle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }

        uint32_t index = 0;
        if (!DecodeCallbackHandle(handle, 1, index) || index >= m_OverlapEndCallbacks.size())
        {
            return EPhysicsResult::NotRegistered;
        }

        CallbackSlot& slot = m_OverlapEndCallbacks[index];
        if (!slot.bOccupied || slot.Generation != handle.Generation)
        {
            return EPhysicsResult::NotRegistered;
        }

        slot.Callback.Clear();
        slot.bOccupied = false;
        if (!TryAdvanceGeneration(slot.Generation))
        {
            slot.bRetired = true;
            return EPhysicsResult::Success;
        }
        m_FreeOverlapEndCallbackIndices.push_back(index);
        return EPhysicsResult::Success;
    }

    EPhysicsResult ColliderComponent::AddOnHit(
        Core::Delegate<void, const PhysicsContactEvent&> callback,
        PhysicsCallbackHandle& outHandle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }
        outHandle = PhysicsCallbackHandle{};
        if (!callback.IsBound())
        {
            return EPhysicsResult::InvalidArgument;
        }

        uint32_t index = 0;
        if (!m_FreeHitCallbackIndices.empty())
        {
            index = m_FreeHitCallbackIndices.back();
            m_FreeHitCallbackIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_HitCallbacks.size());
            m_HitCallbacks.emplace_back();
        }

        CallbackSlot& slot = m_HitCallbacks[index];
        slot.Callback = callback;
        slot.bOccupied = true;
        outHandle = MakeCallbackHandle(2, index, slot.Generation);
        return EPhysicsResult::Success;
    }

    EPhysicsResult ColliderComponent::RemoveOnHit(PhysicsCallbackHandle handle)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        const EPhysicsResult registration = module
            ? module->ValidateCollider(*this)
            : EPhysicsResult::NotRegistered;
        if (registration != EPhysicsResult::Success)
        {
            return registration;
        }

        uint32_t index = 0;
        if (!DecodeCallbackHandle(handle, 2, index) || index >= m_HitCallbacks.size())
        {
            return EPhysicsResult::NotRegistered;
        }

        CallbackSlot& slot = m_HitCallbacks[index];
        if (!slot.bOccupied || slot.Generation != handle.Generation)
        {
            return EPhysicsResult::NotRegistered;
        }

        slot.Callback.Clear();
        slot.bOccupied = false;
        if (!TryAdvanceGeneration(slot.Generation))
        {
            slot.bRetired = true;
            return EPhysicsResult::Success;
        }
        m_FreeHitCallbackIndices.push_back(index);
        return EPhysicsResult::Success;
    }

    void ColliderComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        PhysicsModule* module = FindRegisteredPhysicsModule();
        m_LastRegistrationResult = module
            ? module->RegisterCollider(*this)
            : EPhysicsResult::NotRegistered;
    }

    void ColliderComponent::Finalize()
    {
        if (m_ColliderHandle.IsValid())
        {
            PhysicsModule* module = FindRegisteredPhysicsModule();
            if (!module)
            {
                return;
            }

            const EPhysicsResult result = module->UnregisterCollider(*this);
            if (result != EPhysicsResult::Success)
            {
                return;
            }

            m_LastRegistrationResult = result;
        }

        m_ColliderHandle = Core::Scene::ColliderHandle{};
        m_OverlapBeginCallbacks.clear();
        m_OverlapEndCallbacks.clear();
        m_HitCallbacks.clear();
        m_FreeOverlapBeginCallbackIndices.clear();
        m_FreeOverlapEndCallbackIndices.clear();
        m_FreeHitCallbackIndices.clear();
        Core::Component::Component::Finalize();
    }
} // namespace NorvesLib::Modules::Physics
