#include "Component/MeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Rendering/RenderResources.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MeshComponent, Component)

    namespace
    {
        Rendering::BlendMode ResolveMaterialBlendMode(
            Rendering::MaterialHandle handle,
            const Rendering::MaterialResources *materials)
        {
            if (!handle.IsValid() || materials == nullptr)
            {
                return Rendering::BlendMode::Opaque;
            }

            const Rendering::MaterialResourceData *materialData = materials->GetData(handle);
            if (materialData == nullptr)
            {
                return Rendering::BlendMode::Opaque;
            }

            return materialData->Blend;
        }
    } // namespace

    MeshComponent::MeshComponent()
        : Component()
    {
        // PROPERTYマクロは値初期化するため、非ゼロデフォルト値を設定
        bVisible = true;
        bCastShadow = true;
        bReceiveShadow = true;
        RenderLayerProp = Rendering::RenderLayer::Default;
        ForcedLODLevel = -1;
    }

    MeshComponent::MeshComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        bVisible = true;
        bCastShadow = true;
        bReceiveShadow = true;
        RenderLayerProp = Rendering::RenderLayer::Default;
        ForcedLODLevel = -1;
    }

    MeshComponent::MeshComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        bVisible = true;
        bCastShadow = true;
        bReceiveShadow = true;
        RenderLayerProp = Rendering::RenderLayer::Default;
        ForcedLODLevel = -1;
    }

    MeshComponent::~MeshComponent()
    {
    }

    void MeshComponent::Initialize()
    {
        Component::Initialize();

        // トランスフォーム初期化
        m_WorldTransform = Math::Matrix4x4::Identity;
        m_PreviousWorldTransform = Math::Matrix4x4::Identity;

        m_bTransformDirty = true;
        m_bBoundsDirty = true;
    }

    void MeshComponent::Finalize()
    {
        m_Materials.clear();
        Component::Finalize();
    }

    void MeshComponent::BeginPlay()
    {
        Component::BeginPlay();
    }

    void MeshComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void MeshComponent::Tick(float deltaTime)
    {
        (void)deltaTime;

        // トランスフォームが変更されていれば更新
        if (m_bTransformDirty)
        {
            UpdateWorldTransform();
        }
    }

    // ========================================
    // メッシュ設定
    // ========================================

    void MeshComponent::SetMeshHandle(Rendering::MeshDataHandle handle)
    {
        MeshHandle = handle;
        MarkRenderStateDirty();
    }

    // ========================================
    // マテリアル設定
    // ========================================

    void MeshComponent::SetMaterial(uint32_t index, Rendering::MaterialHandle material)
    {
        if (index >= m_Materials.size())
        {
            m_Materials.resize(index + 1);
        }
        m_Materials[index] = material;
        MarkRenderStateDirty();
    }

    Rendering::MaterialHandle MeshComponent::GetMaterial(uint32_t index) const
    {
        if (index < m_Materials.size())
        {
            return m_Materials[index];
        }
        return Rendering::MaterialHandle{0};
    }

    void MeshComponent::ClearMaterials()
    {
        m_Materials.clear();
        MarkRenderStateDirty();
    }

    // ========================================
    // カスタムデータ
    // ========================================

    void MeshComponent::SetCustomData(uint32_t index, float value)
    {
        if (index < 4)
        {
            m_CustomData[index] = value;
            MarkRenderStateDirty();
        }
    }

    float MeshComponent::GetCustomData(uint32_t index) const
    {
        if (index < 4)
        {
            return m_CustomData[index];
        }
        return 0.0f;
    }

    // ========================================
    // バウンディング
    // ========================================

    const Rendering::BoundingBox &MeshComponent::GetLocalBounds() const
    {
        static Rendering::BoundingBox defaultBounds = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        return defaultBounds;
    }

    void MeshComponent::RefreshRenderTransformCache()
    {
        UpdateWorldTransform();
    }

    // ========================================
    // SceneProxy生成
    // ========================================

    bool MeshComponent::BuildMeshProxy(Rendering::MeshProxy &outProxy,
                                       const Rendering::MaterialResources* materials,
                                       const Rendering::MeshResources* meshes) const
    {
        // メッシュが無効なら生成しない
        if (!MeshHandle->IsValid())
        {
            return false;
        }

        // 可視でなければ生成しない
        if (!IsVisible())
        {
            return false;
        }

        outProxy = Rendering::MeshProxy{};

        // Proxyを構築
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.MeshHandle = MeshHandle;
        outProxy.LODLevel = CurrentLODLevel;

        // トランスフォーム
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.PreviousWorldTransform = m_PreviousWorldTransform;

        // バウンディング
        outProxy.WorldBounds = m_WorldBounds;

        // マテリアル
        const uint32_t materialCount = static_cast<uint32_t>(m_Materials.size());
        outProxy.MaterialCount = materialCount < Rendering::MAX_MATERIAL_SLOTS
                                     ? materialCount
                                     : Rendering::MAX_MATERIAL_SLOTS;
        for (uint32_t i = 0; i < outProxy.MaterialCount; ++i)
        {
            outProxy.Materials[i] = m_Materials[i];
            outProxy.MaterialBlendModes[i] = ResolveMaterialBlendMode(m_Materials[i], materials);
        }
        // マテリアル数が0の場合はデフォルトマテリアルを1つ入れる
        if (outProxy.MaterialCount == 0)
        {
            outProxy.MaterialCount = 1;
            outProxy.Materials[0] = Rendering::MaterialHandle::Invalid();
            outProxy.MaterialBlendModes[0] = Rendering::BlendMode::Opaque;
        }

        if (meshes != nullptr)
        {
            meshes->TryGetSubMeshRanges(outProxy.MeshHandle, outProxy.SubMeshes, outProxy.SubMeshCount);
        }

        // 描画フラグ
        outProxy.bVisible = bVisible;
        outProxy.bCastShadow = bCastShadow;
        outProxy.bReceiveShadow = bReceiveShadow;
        outProxy.LayerMask = RenderLayerProp;

        // カスタムデータ
        for (uint32_t i = 0; i < 4; ++i)
        {
            outProxy.CustomData[i] = m_CustomData[i];
        }

        return true;
    }

    // ========================================
    // 内部メソッド
    // ========================================

    void MeshComponent::UpdateWorldTransform()
    {
        // 前フレーム保存
        m_PreviousWorldTransform = m_WorldTransform;

        // オーナーからワールド行列を計算
        CalculateWorldMatrix(m_WorldTransform);

        m_bTransformDirty = false;
        m_bBoundsDirty = true;

        // バウンディング更新
        UpdateWorldBounds();
    }

    void MeshComponent::UpdateWorldBounds()
    {
        if (!m_bBoundsDirty)
        {
            return;
        }

        // ローカルバウンドからワールドバウンドを計算
        const auto &localBounds = GetLocalBounds();

        // センターとサイズを計算
        float centerX = (localBounds.MinX + localBounds.MaxX) * 0.5f;
        float centerY = (localBounds.MinY + localBounds.MaxY) * 0.5f;
        float centerZ = (localBounds.MinZ + localBounds.MaxZ) * 0.5f;

        float extentX = (localBounds.MaxX - localBounds.MinX) * 0.5f;
        float extentY = (localBounds.MaxY - localBounds.MinY) * 0.5f;
        float extentZ = (localBounds.MaxZ - localBounds.MinZ) * 0.5f;

        float radius = std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);

        // ワールド座標に変換
        Rendering::BoundingSphere localSphere;
        localSphere.CenterX = centerX;
        localSphere.CenterY = centerY;
        localSphere.CenterZ = centerZ;
        localSphere.Radius = radius;

        // MeshProxy::UpdateWorldBoundsと同様のロジック
        const Math::Vector3 translation = m_WorldTransform.GetTranslationRow();
        m_WorldBounds.CenterX = translation.x + localSphere.CenterX;
        m_WorldBounds.CenterY = translation.y + localSphere.CenterY;
        m_WorldBounds.CenterZ = translation.z + localSphere.CenterZ;

        // スケールの概算
        const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(m_WorldTransform);
        float maxScale = scale.x > scale.y ? (scale.x > scale.z ? scale.x : scale.z)
                                           : (scale.y > scale.z ? scale.y : scale.z);

        m_WorldBounds.Radius = localSphere.Radius * maxScale;

        m_bBoundsDirty = false;
    }

    void MeshComponent::SyncMeshProxy()
    {
        // 現時点ではWorld::SyncToSceneViewで毎フレームBuildMeshProxyを呼ぶ形式
        // 将来的にはダーティフラグベースの差分更新に移行
    }

    void MeshComponent::CalculateWorldMatrix(Math::Matrix4x4 &outMatrix) const
    {
        const auto *owner = GetOwner();
        if (!owner)
        {
            outMatrix = Math::Matrix4x4::Identity;
            return;
        }

        const Math::Transform& worldTransform = owner->GetWorldTransform();
        outMatrix = Math::MatrixUtils::CreateWorldRowVector(worldTransform.position,
                                                            worldTransform.rotation,
                                                            worldTransform.scale);
    }

} // namespace NorvesLib::Core::Component
