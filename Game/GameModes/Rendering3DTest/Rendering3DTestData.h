#pragma once

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Input/MayaCameraController.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Core/Public/Rendering/RenderTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "Core/Public/Thread/Mutex.h"

namespace NorvesLib::Core
{
    namespace Component
    {
        class MeshComponent;
        class BoardComponent;
        class BillboardComponent;
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
     * @brief Boulder の非同期ロード共有状態
     *
     * コールバックが Data 本体ではなくこの共有状態を値キャプチャすることで、
     * Routine の Leave 後にコールバックが到着しても use-after-free を起こさない。
     */
    struct BoulderAsyncState
    {
        NorvesLib::Thread::Atomic<bool> m_bCancelled{false}; ///< Leave で立てる。到着結果を破棄する
        NorvesLib::Thread::Atomic<bool> m_bCompleted{false}; ///< コールバック到着フラグ（Do が消費）
        NorvesLib::Core::Rendering::ModelHandle m_Handle;    ///< 結果ハンドル
        bool m_bLoaded = false;                              ///< 有効ハンドルが得られたか
    };

    /**
     * @brief 3Dレンダリングテストのデータクラス
     *
     * 球体と地面のEntityおよびメッシュハンドルを保持します。
     */
    struct Rendering3DTestData
    {
        // メッシュハンドル
        NorvesLib::Core::Rendering::MeshDataHandle m_SphereMeshHandle{100};
        NorvesLib::Core::Rendering::MeshDataHandle m_GroundMeshHandle{101};
        NorvesLib::Core::Rendering::MeshDataHandle m_LightSphereMeshHandle{102};

        // テクスチャハンドル
        NorvesLib::Core::Rendering::TextureHandle m_CheckerTextureHandle;
        NorvesLib::Core::Rendering::TextureHandle m_F6AtlasTextureHandle;

        // マテリアルハンドル
        NorvesLib::Core::Rendering::MaterialHandle m_SilverMaterial;      // Silver PBR マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_CobbleStoneMaterial; // 石畳マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_GroundMaterial;      // 地面マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_LightSphereMaterial; // 光源球体マテリアル
        // 非同期ロード用：マテリアル更新ペンディングリスト
        VariableArray<TSharedPtr<PendingMaterialUpdate>> m_PendingMaterialUpdates;

        // Entity参照（Worldが所有）
        NorvesLib::Core::Entity *m_pSphereObject = nullptr;
        NorvesLib::Core::Entity *m_pGroundObject = nullptr;
        NorvesLib::Core::Entity *m_pLightSphereObject = nullptr;
        NorvesLib::Core::Entity *m_pBoulderObject = nullptr;
        NorvesLib::Core::Entity *m_pBoulderPlaceholderObject = nullptr;
        VariableArray<NorvesLib::Core::Entity *> m_F4BoardObjects;
        VariableArray<NorvesLib::Core::Entity *> m_F9BillboardObjects;

        // MeshComponent参照（Entityが所有）
        NorvesLib::Core::Component::MeshComponent *m_pSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pGroundMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pLightSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pBoulderPlaceholderMeshComponent = nullptr;
        VariableArray<NorvesLib::Core::Component::BoardComponent *> m_F4BoardComponents;
        VariableArray<NorvesLib::Core::Component::BillboardComponent *> m_F9BillboardComponents;
        NorvesLib::Core::Component::MegaGeometryComponent *m_pBoulderMegaGeometryComponent = nullptr;

        // LightComponent参照（Entityが所有）
        NorvesLib::Core::Component::LightComponent *m_pDirectionalLightComponent = nullptr;
        NorvesLib::Core::Component::PointLightComponent *m_pPointLightComponent = nullptr;

        // ディレクショナルライト用Entity（位置は不要だがComponentホスト用）
        NorvesLib::Core::Entity *m_pDirectionalLightObject = nullptr;

        // 経過時間
        float m_ElapsedTime = 0.0f;

        // 球体回転速度（rad/s）
        float m_RotationSpeed = 0.5f;

        // Maya準拠カメラコントローラー（シーン所有）
        NorvesLib::Core::Input::MayaCameraController m_CameraController;

        // デバッグ描画モード（入力トグル用ミラー）
        NorvesLib::Core::Rendering::DebugViewMode m_DebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Normal;

        // メッシュ登録済みフラグ
        bool m_bMeshesRegistered = false;

        // ========================================
        // glTF Model（Boulder）
        // ========================================
        String m_ModelPath;
        uint32_t m_BoardSmokeCount = 0;
        uint32_t m_BillboardSmokeCount = 0;
        bool m_bLayerCompositeSmoke = false;
        NorvesLib::Core::Rendering::ModelHandle m_BoulderModelHandle;
        uint32_t m_BoulderLoadRequestId = 0;
        bool m_bBoulderModelLoaded = false;
        bool m_bBoulderModelLoadPending = false;
        TSharedPtr<BoulderAsyncState> m_BoulderAsyncState; ///< 非同期ロード共有状態
    };

} // namespace Game::GameModes
