#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry::StartupBigSphere
{
    /**
     * @brief 起動画面の大きな球（石畳の高さマップで変位した緯度経度の球）の仕様
     *
     * 実行時の生成（Game）とクッカー（AssetCook の --generate displaced-sphere）が同じ値で球を作るための共有値。
     * どちらかだけを変えると、クック済みの球と実行時の生成の球の形・UV・変位が食い違う。
     */

    /** @brief 球の半径（m） */
    inline constexpr float kRadius = 1.0f;

    /**
     * @brief LOD0の格子（経度×緯度）
     *
     * 頂点の間隔は約6.1 mmで、変位の凹凸（石1つ約10〜15 cm、目地の幅約1〜3 cm）を形に持つ。
     */
    inline constexpr uint32_t kSegments = 1024u;
    inline constexpr uint32_t kRings = 512u;

    /** @brief 経度方向（1周）・緯度方向（極から極）のテクスチャの繰り返し回数（1枚が約2.1 m四方） */
    inline constexpr float kTexCoordRepeatU = 3.0f;
    inline constexpr float kTexCoordRepeatV = 1.5f;

    /**
     * @brief 変位の深さ（高さ0の点を球面から内側へ動かす距離。m）
     *
     * 石畳の法線マップの傾きが高さマップの勾配×深さと最小二乗で一致する深さ。
     */
    inline constexpr float kDisplacementDepth = 0.03f;

    /** @brief 高さマップのミップを作り始める段（4096画素なら512画素） */
    inline constexpr uint32_t kHeightFieldFirstMip = 3u;

    /** @brief 高さマップの論理パス（Assets/ からの相対） */
    inline constexpr const char *kHeightMapRelativePath = "Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png";

    /** @brief クック済みの球のメッシュの論理パス（AssetSets の一覧と Game が同じ名前で引く） */
    inline constexpr const char *kCookedMeshLogicalPath = "Assets/Models/BigCobbleSphere/BigCobbleSphere.generated";

    /**
     * @brief 変位した球のLOD0の頂点の間隔（UV単位。MegaMeshMaterial::DisplacementUVSpacing へ渡す）
     *
     * 法線マップは、頂点が持つ粗い傾きを差し引いて細部だけを載せるために、この間隔を使う。
     */
    constexpr float DisplacementUVSpacing()
    {
        const float columnSpacing = kTexCoordRepeatU / static_cast<float>(kSegments);
        const float rowSpacing = kTexCoordRepeatV / static_cast<float>(kRings);
        return columnSpacing > rowSpacing ? columnSpacing : rowSpacing;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry::StartupBigSphere
