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
        // NVMESH v1.1 のページの詰め方(FormatMajor が 1 のときだけ)。根のページ(常駐)の大きさと、
        // 最も大きいグループがページの中で占めるバイト数(通常のページの上限は 128 KiB)
        uint32_t PageCount = 0;
        uint32_t RootPageBytes = 0;
        uint32_t RootPageClusterCount = 0;
        uint32_t RootPageMinLODLevel = 0;
        uint32_t MaxPageBytes = 0;
        uint32_t LargestGroupBytes = 0;
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
                                        Core::Container::AnsiString& error,
                                        uint32_t fallbackMinTriangles = 0);

    // 起動画面の大きな球（石畳の高さマップ cobblestone_floor_09_disp_4k.png で変位した緯度経度の球）を作り、
    // NVMESH v1 に焼く（--generate displaced-sphere）。heightMapBytes は 16 ビットのグレーの PNG の中身。
    // 球の仕様は Rendering/MegaGeometry/StartupBigSphereSpec.h（実行時の生成と共有）。
    [[nodiscard]] bool CookDisplacedSphereToNvmesh(const uint8_t* heightMapBytes,
                                                   size_t heightMapSize,
                                                   Core::Container::AnsiStringView format,
                                                   Core::Container::AnsiStringView logicalPath,
                                                   MeshCookResult& outResult,
                                                   Core::Container::AnsiString& error,
                                                   uint32_t fallbackMinTriangles = 0);

    [[nodiscard]] bool IsSupportedSkeletalCookFormat(Core::Container::AnsiStringView format) noexcept;

    [[nodiscard]] bool CookGltfToNvskel(const uint8_t* sourceBytes,
                                        size_t sourceSize,
                                        Core::Container::AnsiStringView format,
                                        Core::Container::AnsiStringView sourcePath,
                                        SkeletalCookResult& outResult,
                                        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
