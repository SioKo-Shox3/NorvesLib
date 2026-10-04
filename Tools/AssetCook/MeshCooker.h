#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    struct MeshCookResult
    {
        Core::Container::VariableArray<uint8_t> NvmeshBytes;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;
        // 書き出した NVMESH の主版(0 か 1)。1 のときだけ下の LOD の階層の項目に値が入る。
        uint32_t FormatMajor = 0;
        uint32_t LODLevelCount = 1;
        // LOD の階層の焼き込み(溶接・クラスタ化・簡略化・書き出し・自己検証)にかかった時間
        uint32_t DagMilliseconds = 0;
        // 安全な簡略化が見つからず、簡略化せずに残したグループの数(階層が粗くなりにくくなる。0 が望ましい)
        uint32_t DagRejectedGroups = 0;
    };

    struct SkeletalCookResult
    {
        Core::Container::VariableArray<uint8_t> NvskelBytes;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t JointCount = 0;
        uint32_t ClipCount = 0;
    };

    [[nodiscard]] bool IsSupportedMeshCookFormat(Core::Container::AnsiStringView format) noexcept;

    [[nodiscard]] bool CookGltfToNvmesh(const uint8_t* sourceBytes,
                                        size_t sourceSize,
                                        Core::Container::AnsiStringView format,
                                        Core::Container::AnsiStringView sourcePath,
                                        Core::Container::AnsiStringView logicalPath,
                                        MeshCookResult& outResult,
                                        Core::Container::AnsiString& error);

    [[nodiscard]] bool IsSupportedSkeletalCookFormat(Core::Container::AnsiStringView format) noexcept;

    [[nodiscard]] bool CookGltfToNvskel(const uint8_t* sourceBytes,
                                        size_t sourceSize,
                                        Core::Container::AnsiStringView format,
                                        Core::Container::AnsiStringView sourcePath,
                                        SkeletalCookResult& outResult,
                                        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
