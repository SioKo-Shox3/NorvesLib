#pragma once
// per-mesh IBM/Mと完全な材質を所有する分離mesh。描画側の材質stagingは別責務。
#include "Asset/CookedSkeletonV1.h"
#include "Asset/CookedMaterialFormat.h"
#include "Asset/SkinnedMeshLodTables.h"
#include "Asset/SurfaceSampleTable.h"
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
        RigImportProfile Profile = RigImportProfile::DirectTrs128;
        RigTopology Topology;
        Container::AnsiString SkeletonPath;
        uint64_t SkeletonContentHash = 0, SkeletonRestHash = 0, SkeletonRootHash = 0;
        Container::VariableArray<SkeletalVertex> Vertices;
        Container::VariableArray<uint32_t> Indices;
        // LOD1 以降の索引。表が無ければ空で、INDX 全体は Indices の後ろにこの列を続ける。
        Container::VariableArray<uint32_t> LodIndices;
        // LOD と部位の表。部位と LOD の範囲は INDX 全体の中の位置を指す。
        SkinLodTablesV1 LodTables;
        // 表面の標本の表。空なら SURF を書かない。
        Container::VariableArray<SurfaceSampleRecord> SurfaceSamples;
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
                                    const RigV1Limits&, RigImportProfile);
        friend bool ParseSkinMeshV1(Container::Span<const uint8_t>, SkinMeshV1&, RigV1Report&, const RigV1Limits&,
                                    RigImportProfile);
    };
    // 材質列は生成slot順。implicit素材も明示的に1 record渡し、暗黙の見た目fallbackを作らない。
    [[nodiscard]] bool BuildSkinMeshV1(const RigAuthoringCpu&, const SkeletonV1&,
                                       const Container::AnsiString& skeletonPath,
                                       Container::Span<const SkinMaterialV1> materials, SkinMeshV1& out, RigV1Report&,
                                       const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
    // 組み立て済みのメッシュに LOD と部位の表を足した新しい値を作る。allIndices は [LOD0 の全部位][LOD1 の全部位]… の順の INDX 全体、
    // subMeshes は LOD0 の範囲に合わせて IndexStart・IndexCount を直したサブメッシュ（頂点数・材質・境界は元の値）。
    // 頂点・材質・行列は source のまま。符号化して読み直し、検査に通ったときだけ out を置き換える。
    [[nodiscard]] bool AttachSkinMeshLodsV1(const SkinMeshV1& source, Container::VariableArray<uint32_t> allIndices,
                                           Container::VariableArray<SkeletalSubMesh> subMeshes, SkinLodTablesV1 tables,
                                           SkinMeshV1& out, RigV1Report&, const RigV1Limits& = {},
                                           RigImportProfile = RigImportProfile::DirectTrs128);
    // 表面の標本を足し、符号化して読み直した検査が通ったときだけ out を置き換える。
    [[nodiscard]] bool AttachSkinMeshSurfaceSamplesV1(
        const SkinMeshV1& source, Container::VariableArray<SurfaceSampleRecord> samples, SkinMeshV1& out, RigV1Report&,
        const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool WriteSkinMeshV1(const SkinMeshV1&, Container::VariableArray<uint8_t>& out, RigV1Report&,
                                       const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool ParseSkinMeshV1(Container::Span<const uint8_t>, SkinMeshV1& out, RigV1Report&,
                                       const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool MatchSkinMeshSkeletonV1(const SkinMeshV1&, const SkeletonV1&, RigV1Report&);
    [[nodiscard]] bool IsSplitLogicalPath(const Container::AnsiString&) noexcept;
} // namespace NorvesLib::Core::Skeletal
