#pragma once

#include "Physics/PhysicsTypes.h"

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Core::Component
{
    class TerrainComponent;
}

namespace NorvesLib::Modules::Physics
{
    /// 地形の高さ場を静的なコライダーとして物理に接続する。GameThread 専用。
    /// コライダーは 1 Entity に 1 つなので、地形の所有者の子に専用の Entity を作る。
    /// 所有者のワールド変換は平行移動だけに対応し、回転・拡大は拒否する。
    /// 接続後に所有者を移動すると、子コライダーとワールド座標の地形検索の位置がずれる。
    /// 生成した Entity は地形の所有者と一緒に消える。失敗時の outColliderEntity は nullptr。
    /// 衝突の層とマスクは既定のまま。呼び出し側は SetCollisionLayer / SetCollisionMask で変更できる。
    EPhysicsResult AttachTerrainCollider(Core::World& world, Core::Component::TerrainComponent& terrain,
                                         Core::Entity*& outColliderEntity);
} // namespace NorvesLib::Modules::Physics
