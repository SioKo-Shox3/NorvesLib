#pragma once

// ---------------------------------------------------------------------------
// バナナの配置（mega-bananas と、mega-stress の混在の負荷が共有する）
//
// 手前に山（塚）を 3 つ積み、その周りの床にポアソン円盤で敷き詰める。山は物理を使わず、置いたバナナの形の上に
// 初めて触れる高さへ載せる。配置は本数だけで決まり、実行ごとに同じ（マスクを渡したときは、マスクにも依る）。
// 敷き詰めの位置は、マスク（IBananaFieldMask）が塞いだ所を飛ばし、足りない分は矩形を広げて補う。
// ---------------------------------------------------------------------------

#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Container/Containers.h"
#include "Core/Public/Math/Vector3.h"

#include <cstdint>

namespace Game::GameModes
{

    /// バナナ（Scripts/GenerateSceneModels.py が作り、Assets/AssetSets/MegaBananasModels.json が焼く）
    inline constexpr const char *kBananaMeshPath = "Assets/Models/Generated/banana/banana.glb";
    inline constexpr const char *kBananaFetchHint = "python Scripts/GenerateSceneModels.py --ids banana";
    /// 生成した GLB の内蔵画像の順（GLB の textures と同じ並び）: 0 = ベースカラー、1 = AO・粗さ・金属（ORM）、2 = 法線
    inline constexpr const char *kBananaAlbedoSuffix = ".img0.png";
    inline constexpr const char *kBananaOrmSuffix = ".img1.png";
    inline constexpr const char *kBananaNormalSuffix = ".img2.png";

    /// 配置に使うバナナの形（クック済みメッシュの頂点から作る）
    struct BananaShape
    {
        /// ローカル X（ローカル Y を長手とするバナナの太さの向き）の最小・最大。床に横たえた高さを求める
        float ThicknessMin = 0.0f;
        float ThicknessMax = 0.0f;
        /// 山の高さを調べるための、頂点を間引いた点（バナナのローカル座標・倍率 1）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> SamplePoints;
        /// 同じ用途の、より密な点（置く高さの確定と格子への書き込み用）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> DensePoints;
    };

    /// 敷き詰めの位置を塞ぐマスク（床の x・z で問う）
    class IBananaFieldMask
    {
    public:
        virtual ~IBananaFieldMask() = default;

        /// この位置にバナナを置かないなら true
        virtual bool IsBlocked(float x, float z) const = 0;
    };

    /// メッシュの頂点から、配置に使う形を作る
    void BuildBananaShape(const NorvesLib::Core::Asset::CookedMeshData &mesh, BananaShape &outShape);

    /**
     * @brief count 本を、手前に山・その周りに敷き詰めで散布の入力へ足す
     * @param pMask 敷き詰めの位置を塞ぐマスク。nullptr なら塞がない（mega-bananas の配置）
     * @param outPileCount 山に置いた本数
     * @return 足した本数
     */
    uint32_t PlaceBananas(NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &instances,
                          const BananaShape &shape, uint32_t count, const IBananaFieldMask *pMask, uint32_t &outPileCount);

} // namespace Game::GameModes
