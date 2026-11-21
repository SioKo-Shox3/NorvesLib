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
        /// 同じ PostPhysics 群の消費者（既定の優先度 0）より先に接地の状態を更新する。
        static constexpr int16_t kTickPriority = -1;

        GroundContactComponent();
        explicit GroundContactComponent(const FieldInitializer* initializer);
        explicit GroundContactComponent(const IUnknown* sourceObject);
        ~GroundContactComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        const GroundContactState& GetGroundContactState() const { return m_State; }
        /// 水しぶき・足跡などの消費者が、自分の脚の関節名から接地の状態の足の番号を引く。該当する足がなければ -1。
        int32_t FindFootIndex(Identity joint) const;

        /// 自分の Entity のコライダーは物理側で除く。ここでは追加で除くコライダーを最大 4 個指定する。
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
        const Container::String& GetFootJointName(uint32_t index) const;
        void SetDefaults();
        void TickContacts(float dt);

        GroundContactState m_State;
        Scene::PhysicsQueryFilter m_Filter;
    };
}
