#include "Component/MeshComponent.h"
#include "Object/WorldObject.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MeshComponent, Component)

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
    }

    // ========================================
    // カスタムデータ
    // ========================================

    void MeshComponent::SetCustomData(uint32_t index, float value)
    {
        if (index < 4)
        {
            m_CustomData[index] = value;
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
    // エミッシブ
    // ========================================

    void MeshComponent::SetEmissiveColor(float r, float g, float b)
    {
        m_EmissiveColor[0] = r;
        m_EmissiveColor[1] = g;
        m_EmissiveColor[2] = b;
    }

    void MeshComponent::SetEmissiveStrength(float strength)
    {
        m_EmissiveStrength = strength;
    }

    // ========================================
    // バウンディング
    // ========================================

    const Rendering::BoundingBox &MeshComponent::GetLocalBounds() const
    {
        static Rendering::BoundingBox defaultBounds = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        return defaultBounds;
    }

    // ========================================
    // SceneProxy生成
    // ========================================

    bool MeshComponent::BuildMeshProxy(Rendering::MeshProxy &outProxy) const
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
        outProxy.MaterialCount = static_cast<uint32_t>(m_Materials.size());
        for (uint32_t i = 0; i < m_Materials.size() && i < Rendering::MAX_MATERIAL_SLOTS; ++i)
        {
            outProxy.Materials[i] = m_Materials[i];
        }
        // マテリアル数が0の場合はデフォルトマテリアルを1つ入れる
        if (outProxy.MaterialCount == 0)
        {
            outProxy.MaterialCount = 1;
            outProxy.Materials[0] = Rendering::MaterialHandle{1}; // デフォルト
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

        // エミッシブデータ
        for (uint32_t i = 0; i < 3; ++i)
        {
            outProxy.EmissiveColor[i] = m_EmissiveColor[i];
        }
        outProxy.EmissiveStrength = m_EmissiveStrength;

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
        m_WorldBounds.CenterX = m_WorldTransform.m30 + localSphere.CenterX;
        m_WorldBounds.CenterY = m_WorldTransform.m31 + localSphere.CenterY;
        m_WorldBounds.CenterZ = m_WorldTransform.m32 + localSphere.CenterZ;

        // スケールの概算
        float scaleX = std::sqrt(m_WorldTransform.m00 * m_WorldTransform.m00 +
                                 m_WorldTransform.m01 * m_WorldTransform.m01 +
                                 m_WorldTransform.m02 * m_WorldTransform.m02);
        float scaleY = std::sqrt(m_WorldTransform.m10 * m_WorldTransform.m10 +
                                 m_WorldTransform.m11 * m_WorldTransform.m11 +
                                 m_WorldTransform.m12 * m_WorldTransform.m12);
        float scaleZ = std::sqrt(m_WorldTransform.m20 * m_WorldTransform.m20 +
                                 m_WorldTransform.m21 * m_WorldTransform.m21 +
                                 m_WorldTransform.m22 * m_WorldTransform.m22);
        float maxScale = scaleX > scaleY ? (scaleX > scaleZ ? scaleX : scaleZ)
                                         : (scaleY > scaleZ ? scaleY : scaleZ);

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

        // オーナーのワールドトランスフォームを取得
        const Math::Vector3 &pos = owner->GetPosition();
        const Math::Quaternion &rot = owner->GetRotation();
        const Math::Vector3 &scl = owner->GetScale();

        // クォータニオンからの回転行列計算
        float xx = rot.x * rot.x;
        float yy = rot.y * rot.y;
        float zz = rot.z * rot.z;
        float xy = rot.x * rot.y;
        float xz = rot.x * rot.z;
        float yz = rot.y * rot.z;
        float wx = rot.w * rot.x;
        float wy = rot.w * rot.y;
        float wz = rot.w * rot.z;

        // 回転 * スケール
        outMatrix.m00 = (1.0f - 2.0f * (yy + zz)) * scl.x;
        outMatrix.m01 = (2.0f * (xy - wz)) * scl.y;
        outMatrix.m02 = (2.0f * (xz + wy)) * scl.z;
        outMatrix.m03 = 0.0f;

        outMatrix.m10 = (2.0f * (xy + wz)) * scl.x;
        outMatrix.m11 = (1.0f - 2.0f * (xx + zz)) * scl.y;
        outMatrix.m12 = (2.0f * (yz - wx)) * scl.z;
        outMatrix.m13 = 0.0f;

        outMatrix.m20 = (2.0f * (xz - wy)) * scl.x;
        outMatrix.m21 = (2.0f * (yz + wx)) * scl.y;
        outMatrix.m22 = (1.0f - 2.0f * (xx + yy)) * scl.z;
        outMatrix.m23 = 0.0f;

        // 位置
        outMatrix.m30 = pos.x;
        outMatrix.m31 = pos.y;
        outMatrix.m32 = pos.z;
        outMatrix.m33 = 1.0f;
    }

} // namespace NorvesLib::Core::Component
