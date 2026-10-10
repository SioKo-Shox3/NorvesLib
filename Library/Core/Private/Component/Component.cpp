#include "Component/Component.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(Component, Object)

    // 静的カウンター初期化
    uint64_t Component::s_NextComponentId = 1;

    Component::Component()
        : Object()
    {
        ComponentId = s_NextComponentId++;
        getbEnabled().Set(true);
        getbTickEnabled().Set(true);
    }

    Component::Component(const FieldInitializer *initializer)
        : Object(initializer)
    {
        ComponentId = s_NextComponentId++;
        getbEnabled().Set(true);
        getbTickEnabled().Set(true);
    }

    Component::Component(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        ComponentId = s_NextComponentId++;
        getbEnabled().Set(true);
        getbTickEnabled().Set(true);
    }

    Component::~Component()
    {
    }

    void Component::Initialize()
    {
        Object::Initialize();
    }

    void Component::Finalize()
    {
        if (bBegunPlay)
        {
            EndPlay();
        }

        Object::Finalize();
    }

    void Component::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }

        bBegunPlay = true;
    }

    void Component::EndPlay()
    {
        if (!bBegunPlay)
        {
            return;
        }

        bBegunPlay = false;
    }

    void Component::Tick(float deltaTime)
    {
        (void)deltaTime;
    }

    void Component::OnTickGroup(ETickGroup group, float deltaTime)
    {
        if (group == (m_bHasDispatchPrimaryGroup ? m_DispatchPrimaryGroup : GetTickGroup()))
        {
            Tick(deltaTime);
        }
    }

    void Component::FixedTick(float fixedDeltaTime)
    {
        (void)fixedDeltaTime;
    }

    // ========================================
    // オーナー管理（Outer経由）
    // ========================================

    Entity *Component::GetOwner()
    {
        return CastTo<Entity>(GetOuter());
    }

    const Entity *Component::GetOwner() const
    {
        return CastTo<Entity>(GetOuter());
    }

    uint64_t Component::GetOwnerId() const
    {
        const auto *owner = GetOwner();
        return owner ? owner->GetObjectId() : 0;
    }

    Ecs::EcsRowRef Component::GetEcsRow() const
    {
        const Entity *owner = GetOwner();
        World *world = owner ? owner->GetWorld() : nullptr;
        if (!world || m_EcsEntity.IsNull())
        {
            return {};
        }
        return Ecs::EcsRowRef{&world->GetEcsWorld(), m_EcsEntity};
    }

    TickGroupConfiguration Component::LoadTickConfiguration() const
    {
        Ecs::EcsTickConfig column{};
        if (!Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsTickConfig>().Type, 0,
                                    static_cast<uint32_t>(sizeof(column)), &column))
        {
            return m_TickConfiguration;
        }
        // 列の値は検証済みの設定から写したものなので、群 → 優先度 → mask の順に戻せる
        TickGroupConfiguration configuration;
        configuration.SetGroup(static_cast<ETickGroup>(column.Group));
        configuration.SetPriority(column.Priority);
        configuration.SetMask(column.Mask);
        return configuration;
    }

    void Component::StoreTickConfiguration(const TickGroupConfiguration &configuration)
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsTickConfig>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsTickConfig column{};
            column.Group = static_cast<uint8_t>(configuration.GetGroup());
            column.Priority = configuration.GetPriority();
            column.Mask = configuration.GetMask();
            Ecs::EcsWriteColumnPart(row, type, 0, static_cast<uint32_t>(sizeof(column)), &column);
            return;
        }
        m_TickConfiguration = configuration;
    }

    void Component::Enable()
    {
        if (getbEnabled().Get())
        {
            return;
        }

        getbEnabled().Set(true);
        MarkRenderStateDirty();
    }

    void Component::Disable()
    {
        if (!getbEnabled().Get())
        {
            return;
        }

        getbEnabled().Set(false);
        MarkRenderStateDirty();
    }

} // namespace NorvesLib::Core::Component
