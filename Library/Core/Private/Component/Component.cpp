#include "Component/Component.h"
#include "Object/WorldObject.h"
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
        bEnabled = true;
        bTickEnabled = true;
    }

    Component::Component(const FieldInitializer *initializer)
        : Object(initializer)
    {
        ComponentId = s_NextComponentId++;
        bEnabled = true;
        bTickEnabled = true;
    }

    Component::Component(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        ComponentId = s_NextComponentId++;
        bEnabled = true;
        bTickEnabled = true;
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

    // ========================================
    // オーナー管理（Outer経由）
    // ========================================

    WorldObject *Component::GetOwner()
    {
        return ObjectUtility::CastTo<WorldObject>(GetOuter());
    }

    const WorldObject *Component::GetOwner() const
    {
        return ObjectUtility::CastTo<WorldObject>(GetOuter());
    }

    uint64_t Component::GetOwnerId() const
    {
        const auto *owner = GetOwner();
        return owner ? owner->GetObjectId() : 0;
    }

    void Component::Enable()
    {
        bEnabled = true;
    }

    void Component::Disable()
    {
        bEnabled = false;
    }

} // namespace NorvesLib::Core::Component
