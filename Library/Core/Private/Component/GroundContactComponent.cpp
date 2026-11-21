// 物理の公開スナップショットごとに体と各足の下を照会し、接地点の表面を記録する。
#include "Component/GroundContactComponent.h"

#include "Component/SkinnedMeshComponent.h"
#include "Container/VariableArray.h"
#include "Engine/Engine.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool AddIgnoredCollider(Scene::PhysicsQueryFilter& filter, Scene::ColliderHandle collider,
                                const Scene::SceneQuery* query = nullptr, size_t explicitCount = 0)
        {
            auto& slots = filter.IgnoreColliders;
            auto end = std::end(slots);
            if (!collider.IsValid() || std::find(std::begin(slots), end, collider) != end)
            {
                return false;
            }
            auto empty = std::find(std::begin(slots), end, Scene::ColliderHandle{});
            if (empty == end && query)
            {
                for (size_t i = explicitCount; i < std::size(slots); ++i)
                {
                    bool alive = true;
                    if (query->IsAlive(slots[i], alive) == Scene::EPhysicsSceneQueryResult::Success && !alive)
                    {
                        slots[i] = {};
                    }
                }
                empty = std::find(std::begin(slots) + explicitCount, end, Scene::ColliderHandle{});
            }
            if (empty == end)
            {
                return false;
            }
            *empty = collider;
            return true;
        }

        float NonnegativeDistance(float value)
        {
            return std::isfinite(value) && value > 0.0f ? value : 0.0f;
        }
    }

    IMPLEMENT_CLASS(GroundContactComponent, Component)

    GroundContactComponent::GroundContactComponent()
        : Component()
    {
        SetDefaults();
    }

    GroundContactComponent::GroundContactComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    GroundContactComponent::GroundContactComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    GroundContactComponent::~GroundContactComponent() = default;

    void GroundContactComponent::SetDefaults()
    {
        getFootCount() = 0;
        getFootJoint0() = "";
        getFootJoint1() = "";
        getFootJoint2() = "";
        getFootJoint3() = "";
        getFootLocalOffset0() = Math::Vector3::Zero;
        getFootLocalOffset1() = Math::Vector3::Zero;
        getFootLocalOffset2() = Math::Vector3::Zero;
        getFootLocalOffset3() = Math::Vector3::Zero;
        getRayStartHeight() = 0.5f;
        getRayLength() = 1.5f;
        SetTickGroup(ETickGroup::PostPhysics);
        SetTickPriority(kTickPriority);
        SetTickEnabled(true);
    }

    const Container::String& GroundContactComponent::GetFootJointName(uint32_t index) const
    {
        const Container::String* joints[] = {&FootJoint0.Get(), &FootJoint1.Get(), &FootJoint2.Get(), &FootJoint3.Get()};
        return *joints[index];
    }

    int32_t GroundContactComponent::FindFootIndex(Identity joint) const
    {
        if (!joint.IsValid())
        {
            return -1;
        }
        const uint32_t count = (std::min)(FootCount.Get(), GroundContactState::MaxFeet);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& name = GetFootJointName(i);
            if (!name.empty() && Identity(name) == joint)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    bool GroundContactComponent::SetIgnoredColliders(Container::Span<const Scene::ColliderHandle> colliders)
    {
        if (colliders.size() > std::size(m_Filter.IgnoreColliders))
        {
            return false;
        }
        Scene::PhysicsQueryFilter updated;
        size_t explicitCount = 0;
        for (const auto collider : colliders)
        {
            if (AddIgnoredCollider(updated, collider))
            {
                ++explicitCount;
            }
        }
        for (size_t i = m_ExplicitColliderCount; i < std::size(m_Filter.IgnoreColliders); ++i)
        {
            AddIgnoredCollider(updated, m_Filter.IgnoreColliders[i]);
        }
        std::copy_n(updated.IgnoreColliders, std::size(updated.IgnoreColliders), m_Filter.IgnoreColliders);
        m_ExplicitColliderCount = explicitCount;
        return true;
    }

    bool GroundContactComponent::SetIgnoredBodies(Container::Span<const Scene::BodyHandle> bodies)
    {
        if (bodies.size() > std::size(m_Filter.IgnoreBodies))
        {
            return false;
        }
        for (size_t i = 0; i < std::size(m_Filter.IgnoreBodies); ++i)
        {
            m_Filter.IgnoreBodies[i] = i < bodies.size() ? bodies[i] : Scene::BodyHandle{};
        }
        return true;
    }

    void GroundContactComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (world && !world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "GroundContact.Update";
            tick.Run = [](Component& component, float dt)
            { static_cast<GroundContactComponent&>(component).TickContacts(dt); };
            world->RegisterComponentTick(StaticClass(), tick);
        }
    }

    void GroundContactComponent::EndPlay()
    {
        m_State = GroundContactState{};
        for (size_t i = m_ExplicitColliderCount; i < std::size(m_Filter.IgnoreColliders); ++i)
        {
            m_Filter.IgnoreColliders[i] = {};
        }
        Component::EndPlay();
    }

    void GroundContactComponent::TickContacts(float)
    {
        auto* engine = Engine::GEngine;
        auto* owner = GetOwner();
        if (!engine || !owner || !owner->GetWorld())
        {
            return;
        }
        using Scene::EPhysicsSceneQueryResult;
        auto& query = engine->GetSceneQuery();
        uint64_t sequence = 0;
        if (query.GetPublishedSnapshotSequence(sequence) != EPhysicsSceneQueryResult::Success || sequence == 0)
        {
            // 復帰したときに同じ番号でも取り直すため、状態と番号を未更新に戻す。
            m_State = GroundContactState{};
            return;
        }
        if (sequence == m_State.StepSerial)
        {
            return;
        }

        GroundContactState next;
        next.StepSerial = sequence;
        next.FootCount = (std::min)(FootCount.Get(), GroundContactState::MaxFeet);
        const float length = NonnegativeDistance(RayLength);
        if (length == 0.0f)
        {
            m_State = next;
            return;
        }

        const float height = NonnegativeDistance(RayStartHeight);
        Scene::PhysicsQueryDesc desc;
        desc.Kind = Scene::EPhysicsQueryKind::RaycastClosest;
        desc.Ray.Direction = Math::Vector3(0.0f, -1.0f, 0.0f);
        desc.MaxDistance = static_cast<float>((std::min)(static_cast<double>(height) + length,
                                                        static_cast<double>((std::numeric_limits<float>::max)())));
        desc.MaxHits = 1;
        Container::VariableArray<Scene::PhysicsQueryHit> hits;
        const auto ownerHandle = owner->GetEntityHandle();
        // 学習した自己コライダーはフィルタに残るので、以後のステップでは各足のレイは通常 1 本で済む。
        auto sampleAt = [&](const Math::Vector3& position, GroundContactSample& sample)
        {
            desc.Ray.Origin = position + Math::Vector3(0.0f, height, 0.0f);
            for (size_t retries = 0; retries <= std::size(m_Filter.IgnoreColliders); ++retries)
            {
                desc.Filter = m_Filter;
                if (query.ExecuteQuery(desc, hits) != EPhysicsSceneQueryResult::Success || hits.empty())
                {
                    return;
                }
                const auto& hit = hits[0];
                if (hit.bHasEntity && hit.Entity == ownerHandle)
                {
                    if (!AddIgnoredCollider(m_Filter, hit.Collider, &query, m_ExplicitColliderCount))
                    {
                        return;
                    }
                    continue;
                }
                sample.bHit = true;
                sample.Point = hit.Point;
                sample.Normal = hit.Normal;
                Scene::SurfaceQuery surfaceQuery;
                surfaceQuery.Position = hit.Point;
                surfaceQuery.Normal = hit.Normal;
                surfaceQuery.Collider = hit.Collider;
                Scene::SurfaceSample surface;
                const auto result = query.QuerySurface(surfaceQuery, surface);
                if (result == EPhysicsSceneQueryResult::Success || result == EPhysicsSceneQueryResult::NoHit)
                {
                    sample.Surface = surface;
                }
                return;
            }
        };

        const Math::Transform world = owner->GetWorldTransform();
        sampleAt(world.position, next.Body);
        const auto* mesh = owner->GetComponent<SkinnedMeshComponent>();
        const bool useJoints = mesh && mesh->IsActive() && !mesh->IsPendingDestroy();
        const Math::Vector3 offsets[] = {FootLocalOffset0, FootLocalOffset1, FootLocalOffset2, FootLocalOffset3};
        for (uint32_t i = 0; i < next.FootCount; ++i)
        {
            Math::Transform foot;
            const auto& joint = GetFootJointName(i);
            if (useJoints && !joint.empty())
            {
                const auto index = mesh->FindJointIndex(Identity(joint));
                if (index >= 0 && mesh->TryGetJointWorldTransform(static_cast<uint32_t>(index), foot))
                {
                    sampleAt(foot.position, next.Foot[i]);
                    continue;
                }
            }
            sampleAt(world.TransformPoint(offsets[i]), next.Foot[i]);
        }
        m_State = next;
    }
}
