#pragma once

#include "Math/GeometryTypes.h"
#include "Object/EntityHandle.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    enum class EPhysicsSceneQueryResult : uint8_t
    {
        Success,
        NoHit,
        Unavailable,
        NotReady,
        InvalidArgument,
        WrongThread,
        AlreadyBound,
        ProviderMismatch,
        IterationLimit
    };

    struct ColliderHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const ColliderHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        constexpr bool operator<(const ColliderHandle& other) const
        {
            return Index < other.Index || (Index == other.Index && Generation < other.Generation);
        }
    };

    struct BodyHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const BodyHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        constexpr bool operator<(const BodyHandle& other) const
        {
            return Index < other.Index || (Index == other.Index && Generation < other.Generation);
        }
    };

    struct PhysicsRaycastHit
    {
        ColliderHandle Collider;
        BodyHandle Body;
        EntityHandle Entity;
        bool bHasEntity = false;
        Math::Vector3 Point;
        Math::Vector3 Normal;
        float Distance = 0.0f;
        uint64_t UserData = 0;
    };

    struct PhysicsOverlapHit
    {
        ColliderHandle Collider;
        BodyHandle Body;
        EntityHandle Entity;
        bool bHasEntity = false;
        Math::GeometryContact Contact;
        uint64_t UserData = 0;
    };

    // レイヤーの名前と割当はゲーム側で管理する。エンジンは32ビットの集合だけを扱う。
    using PhysicsCollisionMask = uint32_t;
    inline constexpr PhysicsCollisionMask DefaultPhysicsLayer = 1u;
    inline constexpr PhysicsCollisionMask AllPhysicsLayers = UINT32_MAX;

    enum class EPhysicsQueryTriggerPolicy : uint8_t
    {
        Include,
        Exclude,
        Only
    };

    struct PhysicsQueryFilter
    {
        PhysicsCollisionMask LayerMask = AllPhysicsLayers;
        EPhysicsQueryTriggerPolicy TriggerPolicy = EPhysicsQueryTriggerPolicy::Include;
        ColliderHandle IgnoreColliders[4]{};
        BodyHandle IgnoreBodies[4]{};

        constexpr bool IsValid() const
        {
            return TriggerPolicy == EPhysicsQueryTriggerPolicy::Include
                || TriggerPolicy == EPhysicsQueryTriggerPolicy::Exclude
                || TriggerPolicy == EPhysicsQueryTriggerPolicy::Only;
        }

        constexpr bool Accepts(PhysicsCollisionMask layer, bool bTrigger, ColliderHandle collider, BodyHandle body) const
        {
            if (!IsValid() || (LayerMask & layer) == 0
                || (TriggerPolicy == EPhysicsQueryTriggerPolicy::Exclude && bTrigger)
                || (TriggerPolicy == EPhysicsQueryTriggerPolicy::Only && !bTrigger))
            {
                return false;
            }
            for (const ColliderHandle& ignored : IgnoreColliders)
            {
                if (ignored.IsValid() && ignored == collider)
                {
                    return false;
                }
            }
            for (const BodyHandle& ignored : IgnoreBodies)
            {
                if (ignored.IsValid() && ignored == body)
                {
                    return false;
                }
            }
            return true;
        }
    };

    constexpr bool CanPhysicsLayersInteract(PhysicsCollisionMask layerA, PhysicsCollisionMask maskA,
        PhysicsCollisionMask layerB, PhysicsCollisionMask maskB)
    {
        return (layerA & maskB) != 0 && (layerB & maskA) != 0;
    }

    enum class EPhysicsQueryKind : uint8_t
    {
        RaycastClosest,
        RaycastAll,
        OverlapSphere,
        OverlapBox,
        OverlapCapsule,
        SweepSphere,
        SweepCapsule
    };

    /**
     * @brief 問い合わせの値型。Kindに対応する形状だけを使う。
     * RaycastはRay、Overlap/SweepはSphere/Box/Capsuleを使い、SweepはDirectionへ並進する。
     * MaxDistanceは実距離、MaxHitsは要求ごとの結果数上限。0件要求は実行側で拒否する。
     * 形状や方向の検証は実行側で行う。所有ポインタや動的メモリは持たない。
     */
    struct PhysicsQueryDesc
    {
        EPhysicsQueryKind Kind = EPhysicsQueryKind::RaycastClosest;
        Math::Ray Ray;
        Math::Sphere Sphere;
        Math::OBB Box;
        Math::Capsule Capsule;
        Math::Vector3 Direction;
        float MaxDistance = 0.0f;
        PhysicsQueryFilter Filter;
        uint32_t MaxHits = UINT32_MAX;
        bool bReportStartOverlap = true;
        uint32_t MaxSweepIterations = 24;
    };

    /**
     * @brief 新クエリの結果。Normalは対象から問い合わせ側への押し出し向き。
     * 従来PhysicsOverlapHit.Contact.Normalとは逆向き。UserDataはゲーム所有の識別値。
     */
    struct PhysicsQueryHit
    {
        ColliderHandle Collider;
        BodyHandle Body;
        EntityHandle Entity;
        bool bHasEntity = false;
        uint64_t UserData = 0;
        float Distance = 0.0f;
        Math::Vector3 Point;
        Math::Vector3 Normal;
        float Depth = 0.0f;
        bool bStartPenetrating = false;
    };

    struct PhysicsQueryBatchResult
    {
        EPhysicsSceneQueryResult Result = EPhysicsSceneQueryResult::Unavailable;
        size_t FirstHit = 0;
        size_t HitCount = 0;
    };
} // namespace NorvesLib::Core::Scene
