#pragma once

#include "Math/GeometryTypes.h"
#include "Math/Transform.h"
#include "Scene/SceneQuery.h"

#include <cstdint>

namespace NorvesLib::Modules::Physics
{
    enum class EPhysicsResult : uint8_t
    {
        Success,
        InvalidArgument,
        WrongThread,
        NotRegistered,
        Duplicate,
        InvalidState,
    };

    struct PhysicsCallbackHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const PhysicsCallbackHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }
    };

    enum class EPhysicsBodyType : uint8_t
    {
        Static,
        Dynamic,
        Kinematic,
    };

    /// Entity を持たない静的コライダー（detached）の形状。イベントの通知先は持たない。
    enum class EStaticColliderShape : uint8_t
    {
        /// 球。
        Sphere,
        /// 箱。
        Box,
        /// カプセル。
        Capsule,
    };

    /// チャンクに登録する detached コライダーの記述。常に静的で、イベントの通知先を持たない。
    struct StaticColliderDesc
    {
        /// コライダーの形状。
        EStaticColliderShape Shape = EStaticColliderShape::Sphere;
        /// 球・カプセルの半径。
        float Radius = 0.5f;
        /// カプセルの線分の長さの半分。ColliderComponent の m_CapsuleHalfHeight と同じ意味。
        float CapsuleHalfHeight = 0.5f;
        /// 箱の半分の大きさ。
        Math::Vector3 HalfExtents = Math::Vector3(0.5f, 0.5f, 0.5f);
        /// ワールドの変換（位置・回転・拡大）。
        Math::Transform WorldTransform;
        /// コライダーが属する衝突レイヤーの集合。
        Core::Scene::PhysicsCollisionMask Layer = Core::Scene::DefaultPhysicsLayer;
        /// 衝突の対象にするレイヤーの集合。
        Core::Scene::PhysicsCollisionMask Mask = Core::Scene::AllPhysicsLayers;
        /// 表面の番号。G5-GR38 と同じ番号の空間を使う。
        uint8_t SurfaceId = Core::Scene::kInvalidPhysicsSurfaceId;
        /// 呼び手が設定する任意のデータ。
        uint64_t UserData = 0;
        /// detached はトリガーを持てないため、true は登録時に拒否する。
        bool bTrigger = false;
    };

    /// 常に静的でイベントの通知先を持たない detached コライダー群のチャンクを識別する鍵。
    struct StaticColliderChunkKey
    {
        /// 呼び手が決める 64 ビットの鍵。
        uint64_t Value = 0;

        /// 鍵の値が等しいかを返す。
        constexpr bool operator==(const StaticColliderChunkKey& other) const
        {
            return Value == other.Value;
        }
    };

    struct PhysicsContactEvent
    {
        Core::Scene::ColliderHandle Self;
        Core::Scene::ColliderHandle Other;
        Math::GeometryContact Contact;
        float NormalImpulse = 0.0f;
    };
} // namespace NorvesLib::Modules::Physics
