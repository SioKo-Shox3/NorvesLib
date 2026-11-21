// 固定ステップで調べた体と各足の接地点・表面を、足音や足跡などで共有する。
#pragma once

#include "Component.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Scene/PhysicsHandles.h"
#include "Scene/PhysicsQueryTypes.h"
#include "Scene/SurfaceTypes.h"

namespace NorvesLib::Core::Component
{
    /// 1 本のレイの接地の結果。
    struct GroundContactSample
    {
        bool bHit = false;
        Math::Vector3 Point;
        Math::Vector3 Normal{0.0f, 1.0f, 0.0f};
        Scene::SurfaceSample Surface;
    };

    /// 体と足ごとの接地の状態。StepSerial は公開スナップショットの番号で、0 は未更新。
    struct GroundContactState
    {
        static constexpr uint32_t MaxFeet = 4;
        GroundContactSample Foot[MaxFeet];
        GroundContactSample Body;
        uint32_t FootCount = 0;
        uint64_t StepSerial = 0;
    };

    /// 足 IK は接地レイの結果を公開しないため、体と各足から専用のレイで表面を調べる。
    class GroundContactComponent : public Component
    {
        REFLECTION_CLASS(GroundContactComponent, Component)

    public:
        GroundContactComponent();
        explicit GroundContactComponent(const FieldInitializer* initializer);
        explicit GroundContactComponent(const IUnknown* sourceObject);
        ~GroundContactComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        const GroundContactState& GetGroundContactState() const { return m_State; }
        bool SetIgnoredColliders(Container::Span<const Scene::ColliderHandle> colliders);
        bool SetIgnoredBodies(Container::Span<const Scene::BodyHandle> bodies);

        PROPERTY(uint32_t, FootCount)
        PROPERTY(Container::String, FootJoint0)
        PROPERTY(Container::String, FootJoint1)
        PROPERTY(Container::String, FootJoint2)
        PROPERTY(Container::String, FootJoint3)
        PROPERTY(Math::Vector3, FootLocalOffset0)
        PROPERTY(Math::Vector3, FootLocalOffset1)
        PROPERTY(Math::Vector3, FootLocalOffset2)
        PROPERTY(Math::Vector3, FootLocalOffset3)
        PROPERTY(float, RayStartHeight)
        PROPERTY(float, RayLength)

    private:
        void SetDefaults();
        void TickContacts(float dt);

        GroundContactState m_State;
        Scene::PhysicsQueryFilter m_Filter;
        // 明示されたコライダーを先頭に詰め、残った枠に自己ヒットを記憶する。
        size_t m_ExplicitColliderCount = 0;
    };
}
