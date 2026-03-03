#pragma once

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Rendering/RenderResourceManager.h"

namespace NorvesLib::Core
{
    class WorldObject;

    namespace Component
    {
        class MeshComponent;
    } // namespace Component
} // namespace NorvesLib::Core

namespace Game::GameModes
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief 3Dレンダリングテストのデータクラス
     *
     * 球体と地面のWorldObjectおよびメッシュハンドルを保持します。
     */
    struct Rendering3DTestData
    {
        // メッシュハンドル
        NorvesLib::Core::Rendering::MeshDataHandle m_SphereMeshHandle{100};
        NorvesLib::Core::Rendering::MeshDataHandle m_GroundMeshHandle{101};

        // WorldObject参照（Worldが所有）
        NorvesLib::Core::WorldObject *m_pSphereObject = nullptr;
        NorvesLib::Core::WorldObject *m_pGroundObject = nullptr;

        // MeshComponent参照（WorldObjectが所有）
        NorvesLib::Core::Component::MeshComponent *m_pSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pGroundMeshComponent = nullptr;

        // 経過時間
        float m_ElapsedTime = 0.0f;

        // 球体回転速度（rad/s）
        float m_RotationSpeed = 0.5f;

        // メッシュ登録済みフラグ
        bool m_bMeshesRegistered = false;
    };

} // namespace Game::GameModes
