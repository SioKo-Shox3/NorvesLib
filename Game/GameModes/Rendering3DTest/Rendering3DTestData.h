#pragma once

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Rendering/RenderResourceManager.h"
#include "Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Core/Public/Thread/Mutex.h"

namespace NorvesLib::Core
{
    class WorldObject;

    namespace Component
    {
        class MeshComponent;
        class MegaGeometryComponent;
        class LightComponent;
        class PointLightComponent;
    } // namespace Component
} // namespace NorvesLib::Core

namespace Game::GameModes
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief 非同期マテリアル更新情報
     *
     * テクスチャの非同期読み込み完了時に、
     * どのマテリアルのどのスロットを更新するかを保持します。
     */
    struct PendingMaterialUpdate
    {
        NorvesLib::Core::Rendering::MaterialHandle TargetMaterial;
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0; ///< 残り読み込み中テクスチャ数
    };

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
        NorvesLib::Core::Rendering::MeshDataHandle m_LightSphereMeshHandle{102};

        // テクスチャハンドル
        NorvesLib::Core::Rendering::TextureHandle m_CheckerTextureHandle;

        // マテリアルハンドル
        NorvesLib::Core::Rendering::MaterialHandle m_SilverMaterial;      // Silver PBR マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_CobbleStoneMaterial; // 石畳マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_GroundMaterial;      // 地面マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_LightSphereMaterial; // 光源球体マテリアル
        // 非同期ロード用：マテリアル更新ペンディングリスト
        VariableArray<TSharedPtr<PendingMaterialUpdate>> m_PendingMaterialUpdates;

        // WorldObject参照（Worldが所有）
        NorvesLib::Core::WorldObject *m_pSphereObject = nullptr;
        NorvesLib::Core::WorldObject *m_pGroundObject = nullptr;
        NorvesLib::Core::WorldObject *m_pLightSphereObject = nullptr;
        NorvesLib::Core::WorldObject *m_pBoulderObject = nullptr;
        NorvesLib::Core::WorldObject *m_pBoulderPlaceholderObject = nullptr;

        // MeshComponent参照（WorldObjectが所有）
        NorvesLib::Core::Component::MeshComponent *m_pSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pGroundMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pLightSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pBoulderPlaceholderMeshComponent = nullptr;
        NorvesLib::Core::Component::MegaGeometryComponent *m_pBoulderMegaGeometryComponent = nullptr;

        // LightComponent参照（WorldObjectが所有）
        NorvesLib::Core::Component::LightComponent *m_pDirectionalLightComponent = nullptr;
        NorvesLib::Core::Component::PointLightComponent *m_pPointLightComponent = nullptr;

        // ディレクショナルライト用WorldObject（位置は不要だがComponentホスト用）
        NorvesLib::Core::WorldObject *m_pDirectionalLightObject = nullptr;

        // 経過時間
        float m_ElapsedTime = 0.0f;

        // 球体回転速度（rad/s）
        float m_RotationSpeed = 0.5f;

        // メッシュ登録済みフラグ
        bool m_bMeshesRegistered = false;

        // ========================================
        // glTF Model（Boulder）
        // ========================================
        NorvesLib::Core::Rendering::ModelHandle m_BoulderModelHandle;
        uint32_t m_BoulderLoadRequestId = 0;
        bool m_bBoulderModelLoaded = false;
        bool m_bBoulderModelLoadPending = false;
        bool m_bBoulderModelLoadCompleted = false;
    };

} // namespace Game::GameModes
