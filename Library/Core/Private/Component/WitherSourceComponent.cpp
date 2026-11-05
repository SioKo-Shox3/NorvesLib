// 枯れ度の源を再生中だけ登録し、所有者のワールド XZ 位置へ追従させる。
#include "Component/WitherSourceComponent.h"

#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Vegetation/Vegetation.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(WitherSourceComponent, Component)

    WitherSourceComponent::WitherSourceComponent()
        : Component()
    {
        SetDefaults();
    }

    WitherSourceComponent::WitherSourceComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    WitherSourceComponent::WitherSourceComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    void WitherSourceComponent::SetDefaults()
    {
        SetShape(Vegetation::DrynessShape::Circle);
        SetRadiusA(4.0f);
        SetRadiusB(4.0f);
        SetYaw(0.0f);
        SetFalloff(1.0f);
        SetDefaultLevel(1.0f);
        SetRecoverySeconds(3.0f);
        SetTickEnabled(false);
    }

    void WitherSourceComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();

        Engine::Engine* engine = Engine::GEngine;
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!engine || !world)
        {
            LOG_WARNING("WITHER_SOURCE_COMPONENT_SKIPPED status=MissingEngineOrWorld エンジンまたは所有者の World がありません");
            return;
        }

        const auto position = owner->GetPosition();
        auto& drynessMap = engine->GetVegetation().GetDrynessMap();
        Vegetation::DrynessSourceId sourceId = Vegetation::InvalidDrynessSourceId;
        switch (GetShape())
        {
        case Vegetation::DrynessShape::Circle:
            sourceId = drynessMap.AddCircle(position.x, position.z, GetRadiusA(), GetFalloff(), GetDefaultLevel());
            break;
        case Vegetation::DrynessShape::Ellipse:
            sourceId = drynessMap.AddEllipse(position.x, position.z, GetRadiusA(), GetRadiusB(), GetYaw(),
                                            GetFalloff(), GetDefaultLevel());
            break;
        }
        if (sourceId == Vegetation::InvalidDrynessSourceId)
        {
            LOG_WARNING("WITHER_SOURCE_COMPONENT_SKIPPED status=AddFailed 枯れ度の源を登録できません");
            return;
        }

        m_RegisteredEngine = engine;
        m_SourceId = sourceId;
        if (!world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "WitherSource.Follow";
            tick.Run = [](Component& component, float)
            { static_cast<WitherSourceComponent&>(component).FollowOwner(); };
            tick.TimeChannel = Engine::TimeChannel::World;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
    }

    void WitherSourceComponent::FollowOwner()
    {
        Entity* owner = GetOwner();
        if (m_RegisteredEngine && m_SourceId != Vegetation::InvalidDrynessSourceId && owner)
        {
            const auto position = owner->GetPosition();
            m_RegisteredEngine->GetVegetation().GetDrynessMap().SetSourceCenter(m_SourceId, position.x, position.z);
        }
    }

    void WitherSourceComponent::EndPlay()
    {
        if (m_RegisteredEngine && m_SourceId != Vegetation::InvalidDrynessSourceId)
        {
            m_RegisteredEngine->GetVegetation().GetDrynessMap().Remove(m_SourceId);
        }
        m_RegisteredEngine = nullptr;
        m_SourceId = Vegetation::InvalidDrynessSourceId;
        SetTickEnabled(false);
        Component::EndPlay();
    }

    bool WitherSourceComponent::Recover()
    {
        return SetLevel(0.0f, GetRecoverySeconds());
    }

    bool WitherSourceComponent::SetLevel(float level, float seconds)
    {
        if (!m_RegisteredEngine || m_SourceId == Vegetation::InvalidDrynessSourceId)
        {
            return false;
        }
        return m_RegisteredEngine->GetVegetation().GetDrynessMap().SetTarget(m_SourceId, level, seconds);
    }
} // namespace NorvesLib::Core::Component
