#pragma once
// 旧cookedから作者restを推測しない、新明示rig入力の所有結果。
#include "Animation/RigV1Types.h"
#include <filesystem>
namespace NorvesLib::Core::AssetImport
{
    struct LoadedImportSettings;
}
namespace NorvesLib::Core::Skeletal
{
    struct RigAuthoringData
    {
        SkeletalGltfData Geometry;
        Container::VariableArray<SkeletalRestTransform> LocalRest;
        RigTopology Topology;
        Container::AnsiString SourceLabel;
        double ResolvedImportScale = 1;
    };
    class RigAuthoringCpu
    {
      public:
        [[nodiscard]] const RigAuthoringData* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        Container::TSharedPtr<const RigAuthoringData> m_Data;
        friend bool DecodeRigAuthoringNativePath(Container::Span<const uint8_t>, const std::filesystem::path&,
                                                 RigAuthoringCpu&, RigV1Report&, const RigV1Limits&,
                                                 const AssetImport::LoadedImportSettings*,
                                                 const SkeletalGltfDecodeOptions*);
    };
    // 成功時だけoutを置換する。原source/設定は呼出中不変。外部file全体のatomic snapshotではない。
    [[nodiscard]] bool DecodeRigAuthoringNativePath(Container::Span<const uint8_t> source,
                                                    const std::filesystem::path& path, RigAuthoringCpu& out,
                                                    RigV1Report& report, const RigV1Limits& limits = {},
                                                    const AssetImport::LoadedImportSettings* settings = nullptr,
                                                    const SkeletalGltfDecodeOptions* options = nullptr);
} // namespace NorvesLib::Core::Skeletal
