#pragma once

#include "GeometryInspection.h"
#include "ImageInspection.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    enum class InspectionAlphaMode { Opaque, Mask, Blend };
    struct MaterialInspection
    {
        double BaseColor[4] = {1,1,1,1};
        double Metallic = 1;
        double Roughness = 1;
        double Emissive[3] = {};
        double EmissiveStrength = 1;
        double NormalScale = 1;
        double OcclusionStrength = 1;
        double AlphaCutoff = 0.5;
        InspectionAlphaMode AlphaMode = InspectionAlphaMode::Opaque;
        bool bDoubleSided = false;
    };
    struct ModelInspection
    {
        GeometryInspection Geometry;
        Core::Container::VariableArray<ImageInspection> Images;
        Core::Container::VariableArray<MaterialInspection> Materials;
        bool bHasMaterial = false;
        uint32_t MaterialIndex = 0;
    };
    // 現行static profile（単一mesh/primitive、indexed TRIANGLES）のmesh-local診断。
    // sidecar・node/world変換・cook・書込は行わない。全image/materialを順に検査する。
    // 失敗時outを保持し、成功結果は入力や内部JSON/bufferへ借用しない。
    [[nodiscard]] bool InspectGltfModel(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView sourcePath, ModelInspection& outInspection,
        Core::Container::AnsiString& error);
    [[nodiscard]] bool InspectGltfModelNativePath(const uint8_t* sourceBytes, size_t sourceSize,
        const std::filesystem::path& sourcePath, ModelInspection& outInspection,
        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
