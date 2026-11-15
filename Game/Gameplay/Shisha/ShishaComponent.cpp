#include "Gameplay/Shisha/ShishaComponent.h"

#include "Gameplay/Shisha/ShishaDefeatResponses.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"

namespace Game::Gameplay
{
    IMPLEMENT_CLASS(ShishaComponent, NorvesLib::Modules::Gameplay::Enemy::EnemyComponent)

    ShishaComponent::ShishaComponent()
    {
        DefinitionPath = "Gameplay/enemies/shisha_human_01.enemy.json";
        WitherSourceKey = NorvesLib::Core::Container::String();
        WitherRecoverSeconds = 8.0f;
        WitherRecoverTarget = 0.0f;
    }

    void ShishaComponent::Initialize()
    {
        Super::Initialize();
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        if (world && !world->FindComponentTick(StaticClass()))
        {
            world->RegisterComponentTick(StaticClass(), EnemyComponent::MakeTickRegistration());
        }
    }

    void ShishaComponent::BeginPlay()
    {
        Super::BeginPlay();
        if (!bBegunPlay || m_bDefeatResponseSubscribed)
        {
            return;
        }

        OnDefeatCompleted.Add(this, &ShishaComponent::OnShishaDefeatCompleted);
        m_bDefeatResponseSubscribed = true;
        for (const auto& id : GetDefinition().DefeatResponses)
        {
            if (!FindShishaDefeatResponse(id))
            {
                LOG_WARNING("SHISHA_DEFEAT_RESPONSE_UNKNOWN id=%s enemy=%s 未知の撃破の応答です",
                            id.c_str(), GetEnemyId().c_str());
            }
        }
    }

    void ShishaComponent::EndPlay()
    {
        if (m_bDefeatResponseSubscribed)
        {
            OnDefeatCompleted.Remove(this, &ShishaComponent::OnShishaDefeatCompleted);
            m_bDefeatResponseSubscribed = false;
        }
        Super::EndPlay();
    }

    void ShishaComponent::OnShishaDefeatCompleted(const NorvesLib::Core::Container::String& enemyId)
    {
        for (const auto& id : GetDefinition().DefeatResponses)
        {
            if (const auto* response = FindShishaDefeatResponse(id))
            {
                response->Run(*this);
            }
            else
            {
                LOG_WARNING("SHISHA_DEFEAT_RESPONSE_UNKNOWN id=%s enemy=%s 未知の撃破の応答です",
                            id.c_str(), enemyId.c_str());
            }
        }
    }
} // namespace Game::Gameplay
