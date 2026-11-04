#include "Gameplay/Combat/HurtboxSetComponent.h"

#include "Gameplay/Combat/CombatRuntime.h"
#include "Gameplay/GameplayModule.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Combat
{
    IMPLEMENT_CLASS(HurtboxSetComponent, Core::Component::Component)

    HurtboxSetComponent::HurtboxSetComponent()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    HurtboxSetComponent::~HurtboxSetComponent()
    {
        Unregister();
    }

    bool HurtboxSetComponent::IsOwnerThread() const
    {
        if (m_Runtime != nullptr)
        {
            return m_Runtime->IsOwnerThread();
        }
        auto* module = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        return module != nullptr ? module->IsOwnerThread()
                                 : m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    void HurtboxSetComponent::Initialize()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        Super::Initialize();
        m_bCombatReady = true;
        TryRegister();
    }

    void HurtboxSetComponent::BeginPlay()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        Super::BeginPlay();
        m_bCombatReady = true;
        TryRegister();
    }

    void HurtboxSetComponent::EndPlay()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        Unregister();
        m_bCombatReady = false;
        Super::EndPlay();
    }

    void HurtboxSetComponent::Finalize()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        EndPlay();
        Super::Finalize();
    }

    bool HurtboxSetComponent::TryRegister()
    {
        if (!IsOwnerThread() || !m_bCombatReady)
        {
            return false;
        }
        auto* owner = GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        auto* module = world != nullptr ? GameplayModule::FindBoundModule(*world) : nullptr;
        return module != nullptr && module->GetCombatRuntime().Register(*this);
    }

    void HurtboxSetComponent::Unregister()
    {
        if (m_Runtime != nullptr)
        {
            m_Runtime->Unregister(*this);
        }
    }

    uint32_t HurtboxSetComponent::AddPart(const HurtboxPart& part)
    {
        if (!IsOwnerThread() || m_Parts.size() >= InvalidPartIndex)
        {
            return InvalidPartIndex;
        }
        auto* owner = GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (part.Binding.IsValid() && (world == nullptr || world->ResolveEntityHandle(part.Binding) == nullptr))
        {
            return InvalidPartIndex;
        }
        HurtboxPart added = part;
        added.bOwnerBinding = !part.Binding.IsValid();
        if (added.bOwnerBinding && owner != nullptr)
        {
            added.Binding = owner->GetEntityHandle();
        }
        added.PreviousTransform = {};
        added.bPreviousValid = false;
        const uint32_t index = static_cast<uint32_t>(m_Parts.size());
        m_Parts.push_back(added);
        m_JointBindings.push_back({});
        return index;
    }

    void HurtboxSetComponent::SetWeakJointThreshold(const CombatJointInfluenceThreshold& threshold)
    {
        if (IsOwnerThread())
        {
            m_WeakJointThreshold = threshold;
        }
    }

    uint64_t HurtboxSetComponent::GetActorId() const
    {
        const auto* owner = GetOwner();
        return m_bExplicitActorId || owner == nullptr ? m_ActorId : owner->GetObjectId();
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
