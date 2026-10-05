#pragma once

// クック済みメッシュ（NVMESH v1.1）のページを、ファイルの範囲読みで読む窓口（IGeometryPageSource）を作る。
// Asset 層は Rendering を include しないので、窓口は Rendering 側に置く（Rendering -> Asset の向き）。

#include "Asset/AssetFileReader.h"
#include "Asset/AssetReadRequest.h"
#include "Asset/CookedMeshFormat.h"
#include "Container/PointerTypes.h"
#include "Rendering/MegaGeometry/GeometryPageSource.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief クック済みメッシュのページを、ジョブシステムの範囲読みで読む窓口を作る
     *
     * ページごとに、ファイルの範囲（ページの表の FileOffset・Size）だけを読み、ハッシュと中身を検査して、
     * 頂点（32 バイト × 頂点数）とクラスタのインデックスを並べたバイト列を返す（GeometryPageReadResult::Data）。
     * ページの表・件数は cooked から写すので、cooked は作った後で手放してよい。
     *
     * @param cooked v1.1（Pages が空でない）のクック済みメッシュ
     * @param reader パッケージ（または単体の .nvmesh）を読むファイル読み込み
     * @param request ファイルを指す読み込みの要求
     * @param baseOffset .nvmesh の先頭のファイル内の位置
     * @return v1.1 でない（ページが 2 つ未満）ときは null
     */
    Container::TSharedPtr<IGeometryPageSource> MakeCookedMeshPageSource(const Asset::CookedMeshData &cooked,
                                                                       const Asset::AssetFileReader &reader,
                                                                       const Asset::AssetReadRequest &request,
                                                                       uint64_t baseOffset);
} // namespace NorvesLib::Core::Rendering::MegaGeometry
