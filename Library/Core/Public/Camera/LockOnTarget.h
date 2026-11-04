#pragma once
#include "Math/Vector3.h"
#include <cstdint>
namespace NorvesLib::Core
{
    class World;
}
namespace NorvesLib::Core::Camera
{
    struct LockOnPoint
    {
        Math::Vector3 Position; // 描画空間での注視点。
        float Radius = 0;       // アームを伸ばす量に加える目標の半径。
    };

    // ロックオン目標の読み取り専用インターフェース。カメラは弱参照で保持し、毎フレーム解決する。
    // 弱参照が切れたとき、または解決に失敗したときはロックを外す。
    class ILockOnTarget
    {
      public:
        virtual ~ILockOnTarget() = default;
        // 解決できないときは out を変更せず false を返す。
        virtual bool TryResolve(const World& world, LockOnPoint& out) const = 0;
        // Entity に結び付かない目標は 0 を返す。
        virtual uint64_t GetEntityId() const { return 0; }
    };

    class EntityLockOnTarget final : public ILockOnTarget
    {
      public:
        EntityLockOnTarget(uint64_t entityId, float radius);
        bool TryResolve(const World& world, LockOnPoint& out) const override;
        uint64_t GetEntityId() const override { return m_EntityId; }

      private:
        uint64_t m_EntityId;
        float m_Radius;
    };

    class JointLockOnTarget final : public ILockOnTarget
    {
      public:
        JointLockOnTarget(uint64_t entityId, uint32_t jointIndex, float radius);
        bool TryResolve(const World& world, LockOnPoint& out) const override;
        uint64_t GetEntityId() const override { return m_EntityId; }
        uint32_t GetJointIndex() const { return m_JointIndex; }

      private:
        uint64_t m_EntityId;
        uint32_t m_JointIndex;
        float m_Radius;
    };
} // namespace NorvesLib::Core::Camera
