#pragma once

#include "Animation/SkeletalPoseBounds.h"
#include "Asset/CookedMaterialFormat.h"
#include "Rendering/SkinnedMeshTypes.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Skeletal
{
    class SkinMeshV1;
    struct SkinMeshV1Data;

    // 3 資産の SkinnedMesh の slot ごとの材質の元。GetMaterialSlots と同じ slot 順。
    struct SkeletalSlotMaterialSource
    {
        Container::String SlotName;          // GetMaterialSlots()[i].Name と同じ名前。
        uint32_t MaterialIndex = 0;          // MATS の材質番号。
        Asset::CookedMaterialRecord Record;  // 4 つの StringRef は 0。
        Container::AnsiString Textures[4];   // albedo・normal・arm・emissive の論理パス。
    };
} // namespace NorvesLib::Core::Skeletal
namespace NorvesLib::Core
{
    class SkinnedMeshResource : public Resource
    {
        REFLECTION_CLASS(SkinnedMeshResource, Resource)

    public:
        SkinnedMeshResource();
        explicit SkinnedMeshResource(const FieldInitializer* initializer);
        explicit SkinnedMeshResource(const IUnknown* sourceObject);
        ~SkinnedMeshResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        // CPU専用split資産。完全MATSを保持し、未実装の描画leaseを発行しない。
        // split設定後は旧setterを拒否し、Unloadでlegacy fallbackに戻らない。
        [[nodiscard]] bool SetSplitMesh(const Skeletal::SkinMeshV1& mesh);
        [[nodiscard]] bool IsSplitV1() const noexcept;
        [[nodiscard]] const Skeletal::SkinMeshV1Data* GetSplitMesh() const noexcept;
        // split の slot 順。旧経路と Unload の後は空。
        const Container::VariableArray<Skeletal::SkeletalSlotMaterialSource>& GetSlotMaterialSources() const noexcept;
        // 作った材質をこの世代に結び、値が変わったら離す。SetSplitMesh の成功時と Unload ごとに進む。
        uint64_t GetSplitMeshGeneration() const noexcept;
        void SetVertices(Container::VariableArray<Skeletal::SkeletalVertex>&& vertices);
        void SetIndices(Container::VariableArray<uint32_t>&& indices);
        // Load/Refresh時に両表とgeometryをまとめて検査する。既存leaseはrefreshまで不変。
        void SetSubmeshTables(Container::VariableArray<Skeletal::SkeletalSubMesh>&& submeshes,
                              Container::VariableArray<Skeletal::SkeletalMaterialSlot>&& slots);
        const Container::VariableArray<Skeletal::SkeletalSubMesh>& GetSubMeshes() const;
        const Container::VariableArray<Skeletal::SkeletalMaterialSlot>& GetMaterialSlots() const;
        void SetMeshNodeGlobalTransform(const Container::FixedArray<float, 16>& transform);

        Rendering::SkinnedMeshHandle GetRenderMeshHandle() const;
        const Container::TSharedPtr<Rendering::SkinnedMeshAssetLease>& GetRenderAssetLease() const;
        const Container::VariableArray<Skeletal::SkeletalVertex>& GetVertices() const;
        const Container::VariableArray<uint32_t>& GetIndices() const;
        const Container::FixedArray<float, 16>& GetMeshNodeGlobalTransform() const;
        void RefreshRenderAssetLease();
        void ReleaseRenderAssetLease();

        const Animation::MeshPoseBounds& GetPoseBounds() const noexcept { return m_PoseBounds; }
        uint64_t GetPoseRevision() const noexcept { return m_PoseRevision; }

    private:
        uint64_t m_PoseRevision = 0;
        Animation::MeshPoseBounds m_PoseBounds;
      Container::TSharedPtr<const Skeletal::SkinMeshV1Data> m_SplitMesh;
      uint64_t m_SplitMeshGeneration = 0;
      bool m_bSplitV1 = false;
      Container::TSharedPtr<Rendering::SkinnedMeshAssetLease> m_RenderAssetLease;
        uint64_t m_RenderAssetGeneration = 0;
      Container::VariableArray<Skeletal::SkeletalVertex> m_Vertices;
      Container::VariableArray<uint32_t> m_Indices;
      Container::VariableArray<Skeletal::SkeletalSubMesh> m_SubMeshes;
      Container::VariableArray<Skeletal::SkeletalMaterialSlot> m_MaterialSlots;
      Container::VariableArray<Skeletal::SkeletalSlotMaterialSource> m_SlotMaterialSources;
      Container::FixedArray<float, 16> m_MeshNodeGlobalTransform{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                                 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    };
} // namespace NorvesLib::Core
