#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;

    /// スプラットの層の名前と、消費者が引く表面の名前の対応
    struct TerrainSurfaceLayer
    {
        Container::AnsiString LayerName;
        Container::AnsiString SurfaceName;
        float TileMeters = 4.0f;
        float Wettability = 0.0f;
        float PlaceholderColor[3] = {0.30f, 0.27f, 0.22f};
        float PlaceholderRoughness = 0.9f;
        Container::AnsiString AlbedoPath;
        Container::AnsiString NormalPath;
        /// R = AO、G = 粗さ、B = 金属度、A = 高さ
        Container::AnsiString ArmPath;
    };

    /// 地形の材質の記述。添字 = SurfaceId（スプラットの層の番号）
    struct TerrainMaterialDesc
    {
        Container::VariableArray<TerrainSurfaceLayer> Layers;
        int32_t CliffLayer = -1;
        float CliffSlopeDegrees[2] = {38.0f, 55.0f};
        float HeightBlendDepth = 0.2f;
        float UvRotationWavelength = 23.0f;
        float UvRotationDegrees = 180.0f;
        /// 表面の名前。範囲外は空
        Container::AnsiStringView GetSurfaceName(uint8_t surfaceId) const;
    };

    /// 層の名前から仮の表面の名前を引く。未知の名前は Soil
    Container::AnsiStringView GetProvisionalSurfaceName(Container::AnsiStringView layerName);
    /// 高さ場の層の名前から、仮の表で地形の材質の記述を作る
    TerrainMaterialDesc MakeProvisionalTerrainMaterialDesc(const TerrainHeightField& field);

    /// アセットの根からの JSON の表を層の名前で引き、高さ場のスプラットの順に並べる。
    /// 表に無い層は仮の表面名と既定値を使う。テクスチャのパスは省略または空にできる。
    /// 読めない・形式が不正なときは false と理由を返し、outDesc を仮の記述にする
    bool LoadTerrainMaterialDesc(Container::AnsiStringView tablePath, const TerrainHeightField& field,
                                 TerrainMaterialDesc& outDesc, Container::AnsiString& outError);
} // namespace NorvesLib::Core::Rendering::Terrain
