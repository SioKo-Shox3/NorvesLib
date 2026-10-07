#pragma once

// 材質ごとの形の解決（VisibilityResolve）への入力を、材質の表の 1 件から作る関数。
// テストが、表の件の各テクスチャの枠が用途どおりの位置から引かれること（アルベド・法線・金属度・粗さ・AO・ORM・高さ）と、
// MegaGeometry の印・スカラー値の扱いを確かめるために公開する。

#include "Rendering/RenderResources.h"
#include "Rendering/VisibilityMaterialTable.h"
#include "Rendering/VisibilityResolvePass.h"

namespace NorvesLib::Core::Rendering::VisibilityResolveGeometry
{
    /**
     * @brief 材質の表の 1 件から、GBufferPass の材質の descriptor が張るのと同じテクスチャ・スカラー値・VT の要求のパラメータを引く
     *
     * textures が null のときは、テクスチャはすべて null（VT の要求のパラメータは 0）。スカラー値は、そのテクスチャの
     * 指定（ハンドル）が無いときに採る。MegaGeometry は解決不能なハンドルも定数へ戻す。MegaGeometry の印（MATERIAL_FLAG_MEGA_GEOMETRY）は bMegaGeometry へ写す。
     */
    VisibilityResolveMaterial MakeResolveMaterial(const TextureResources* textures,
                                                  const VisibilityBuffer::MaterialEntry& entry,
                                                  const TextureResources::VirtualTextureFeedbackTarget& feedbackTarget);
} // namespace NorvesLib::Core::Rendering::VisibilityResolveGeometry
