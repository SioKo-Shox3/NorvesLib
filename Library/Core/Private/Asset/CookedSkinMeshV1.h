#pragma once
// per-mesh IBM/Mと完全な材質を所有する分離mesh。描画側の材質stagingは別責務。
#include "Asset/CookedSkeletonV1.h"
#include "Asset/CookedMaterialFormat.h"
namespace NorvesLib::Core
{
    class SkinnedMeshResource;
}
namespace NorvesLib::Core::Skeletal
{
    struct SkinMaterialV1
    {
        Asset::CookedMaterialRecord Record;
        Container::AnsiString Textures[4];
    };
    struct SkinSlotV1
    {
        Container::AnsiString Name;
        uint32_t MaterialIndex = 0;
    };
    struct SkinMeshV1Data
    {
        RigTopology Topology;
        Container::AnsiString SkeletonPath;
        uint64_t SkeletonContentHash = 0, SkeletonRestHash = 0, SkeletonRootHash = 0;
        Container::VariableArray<SkeletalVertex> Vertices;
        Container::VariableArray<uint32_t> Indices;
        Container::VariableArray<Container::FixedArray<float, 16>> InverseBindMatrices;
        Container::FixedArray<float, 16> MeshTransform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        Container::VariableArray<SkeletalSubMesh> SubMeshes;
        Container::VariableArray<SkinSlotV1> Slots;
        Container::VariableArray<SkinMaterialV1> Materials;
        uint64_t ContentHash = 0;
    };
    class SkinMeshV1
    {
      public:
        [[nodiscard]] const SkinMeshV1Data* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        friend class NorvesLib::Core::SkinnedMeshResource;
        Container::TSharedPtr<const SkinMeshV1Data> m_Data;
        friend bool BuildSkinMeshV1(const RigAuthoringCpu&, const SkeletonV1&, const Container::AnsiString&,
                                    Container::Span<const SkinMaterialV1>, SkinMeshV1&, RigV1Report&,
                                    const RigV1Limits&);
        friend bool ParseSkinMeshV1(Container::Span<const uint8_t>, SkinMeshV1&, RigV1Report&, const RigV1Limits&);
    };
    // 材質列は生成slot順。implicit素材も明示的に1 record渡し、暗黙の見た目fallbackを作らない。
    [[nodiscard]] bool BuildSkinMeshV1(const RigAuthoringCpu&, const SkeletonV1&,
                                       const Container::AnsiString& skeletonPath,
                                       Container::Span<const SkinMaterialV1> materials, SkinMeshV1& out, RigV1Report&,
                                       const RigV1Limits& = {});
    [[nodiscard]] bool WriteSkinMeshV1(const SkinMeshV1&, Container::VariableArray<uint8_t>& out, RigV1Report&,
                                       const RigV1Limits& = {});
    [[nodiscard]] bool ParseSkinMeshV1(Container::Span<const uint8_t>, SkinMeshV1& out, RigV1Report&,
                                       const RigV1Limits& = {});
    [[nodiscard]] bool MatchSkinMeshSkeletonV1(const SkinMeshV1&, const SkeletonV1&, RigV1Report&);
    [[nodiscard]] bool IsSplitLogicalPath(const Container::AnsiString&) noexcept;
} // namespace NorvesLib::Core::Skeletal
