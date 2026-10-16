#pragma once

// ---------------------------------------------------------------------------
// 地形の高さ場（seed と関数だけで決まる、手続き生成の高さ）
//
// クッカー（AssetCook の --generate terrain-tile:<x>,<z>）が地形のメッシュを焼くときと、Game が任意の (x,z) の
// 高さ・法線を引くとき（岩や建物を地面に据える・草を撒く）が、同じ関数を通る。どちらかだけ値を変えると、
// 焼いた地形と Game が引く高さが食い違う。
//
// 座標は m、+Y が上。高さ 0 が中央の盆地の床。形は中央の平らな盆地、その周りの砂丘となだらかな起伏、
// 輪の上に並ぶメサ（傾いた頂と切り立った崖の台地。縁は崩れ、浸食の切れ込みと途中の段を持つ）とその脇のビュート、
// 外周の稜線の 4 つを重ねて作る。
//
// もう 1 つの作り方は、実在の地形（DEM）を切り出した格子（Scripts/Terrain/CropDem.py が書く height.json と height.r32）
// から引く方法（LoadGrid）。格子の点をそのまま頂点にし、点の間は焼いたメッシュと同じ対角線（UsesMainDiagonal）で
// 升目を 2 つの三角形に分けて線形に補間する（元の値を越える補間はしない）。範囲の外は端を延ばさず kOutOfRangeHeight を返す。
// クッカーは --generate terrain-tile:dem:<x>,<z> で、この格子を kDemTileCells 升目ごとの区画に分けて焼く。
// ---------------------------------------------------------------------------

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering::Terrain
{

    /// 格子の升目 (column, row) を 2 つの三角形に分ける対角線の向き。true なら (column, row)〜(column + 1, row + 1)、
    /// false なら (column + 1, row)〜(column, row + 1)。隣り合う升目で交互に向きを変え、斜面の筋が一方向に揃わないようにする。
    /// 番号は全体の格子の番号（区画の中の番号ではない）。焼いたメッシュの三角形と、格子の高さ場の補間が同じ規則を使う
    inline constexpr bool UsesMainDiagonal(uint32_t column, uint32_t row)
    {
        return ((column + row) & 1u) == 0u;
    }

    /// 格子の高さ場で範囲の外を引いたときの高さ（NaN。std::isnan で判定する）。法線は 3 成分とも NaN になる
    inline constexpr float kOutOfRangeHeight = std::numeric_limits<float>::quiet_NaN();

    /// 高さ場の形を決める値。同じ値なら、どの実行・どのプロセスでも同じ高さになる
    struct TerrainHeightFieldParams
    {
        /// 乱数の種（メサの配置・ノイズの位相・砂丘の向き）
        uint32_t Seed = 1u;
        /// 中央の平らな盆地の半径[m]（この内側は高さ 0）
        float BasinRadius = 100.0f;
        /// 盆地の縁から周りの起伏へ移る幅[m]
        float BasinBlend = 90.0f;
        /// 盆地の縁から稜線の麓まで、外へ向かってゆるく上がる高さ[m]（盆地をくぼみにする）
        float BowlHeight = 14.0f;
        /// なだらかな起伏（数百 m の波長）の振幅[m]
        float RollingAmplitude = 10.0f;
        /// 砂丘の波長[m]と振幅[m]
        float DuneWavelength = 70.0f;
        float DuneAmplitude = 5.0f;
        /// メサを並べる輪の内側・外側の半径[m]と、メサの数（各メサの脇に、種しだいで小さなビュートが 1 つ立つ）
        float MesaRingInner = 330.0f;
        float MesaRingOuter = 680.0f;
        uint32_t MesaCount = 7u;
        /// 外周の稜線が立ち上がり始める距離・頂に達する距離[m]（中心から。角は正方形に寄せて測る）
        float RidgeStart = 700.0f;
        float RidgeCrest = 920.0f;
        /// 稜線の高さ[m]（基準の高さと、尾根の凹凸の振幅）
        float RidgeHeight = 110.0f;
        float RidgeRoughness = 70.0f;
    };

    /// 高さ場の法線（単位ベクトル、+Y が上）
    struct TerrainNormal
    {
        float X = 0.0f;
        float Y = 1.0f;
        float Z = 0.0f;
    };

    /// 格子の高さ場の頂点の並び。頂点 (column, row) の位置は (FirstX + column × Spacing, FirstZ + row × Spacing)。
    /// 手続きの作り方では Columns・Rows が 0
    struct TerrainGridLayout
    {
        uint32_t Columns = 0;
        uint32_t Rows = 0;
        double FirstX = 0.0;
        double FirstZ = 0.0;
        double Spacing = 0.0;
    };

    /// 高さ場。作った後は読み取りだけで、複数のスレッドから同時に引いてよい
    class TerrainHeightField
    {
    private:
        /// 格子の高さ場の中身（cpp にある）
        struct GridData;
        /// 格子の作り方を LoadGrid の外から呼ばせないための印
        struct GridTag
        {
            explicit GridTag() = default;
        };

    public:
        static constexpr uint32_t kMaxMesas = 16u;

        /// 手続きの作り方（seed と関数だけで決まる）
        explicit TerrainHeightField(const TerrainHeightFieldParams &params);

        /// 格子の作り方。LoadGrid だけが呼ぶ
        TerrainHeightField(GridTag tag, Container::TSharedPtr<const GridData> grid);

        /// 切り出した格子（Scripts/Terrain/CropDem.py が書く height.json と、その中の file が指す float32 の格子）を読んで
        /// 高さ場を作る。descriptorPath はアセットの根（Assets/）からの道。格子のファイルは呼ぶたびに丸ごと読むので、
        /// 1 回だけ呼んで返った高さ場を共有する（共有した高さ場は複数のスレッドから同時に引いてよい）。
        /// 読めない・中身が約束と違うときは空を返し、outError に理由を書く
        static Container::TSharedPtr<const TerrainHeightField> LoadGrid(Container::AnsiStringView descriptorPath,
                                                                        Container::AnsiString &outError);

        /// (x,z) の高さ[m]。格子の作り方で範囲の外なら kOutOfRangeHeight
        float SampleHeight(float x, float z) const;

        /// (x,z) の法線。高さの中心差分（間隔 sampleSpacing[m]）から求める。格子の作り方では、差分の片側が範囲の外なら
        /// その側は (x,z) の高さを使う片側の差分にし、(x,z) が範囲の外なら 3 成分とも NaN
        TerrainNormal SampleNormal(float x, float z, float sampleSpacing = 0.5f) const;

        /// 格子から作ったか
        bool IsGrid() const
        {
            return m_Grid != nullptr;
        }

        /// (x,z) が高さを引ける範囲か（手続きの作り方では常に true。格子では外周の頂点を含む内側）
        bool Contains(float x, float z) const;

        /// 格子の頂点の並び（手続きの作り方では Columns・Rows が 0）
        TerrainGridLayout GetGridLayout() const;

        /// 格子の頂点 (column, row) の高さ（ファイルの値そのもの）。格子の作り方で、番号が並びの内側のときだけ呼ぶ
        float GetGridVertexHeight(uint32_t column, uint32_t row) const;

        /// 格子のファイルの中身（float32 の little-endian、行優先）。手続きの作り方では空
        Container::Span<const uint8_t> GetGridBytes() const;

        /// 手続きの作り方の値（格子の作り方では既定の値のままで、意味を持たない）
        const TerrainHeightFieldParams &GetParams() const
        {
            return m_Params;
        }

    private:
        /// メサ（大きな台地）と、その脇に立つビュート（小さな孤立丘）。どちらも同じ形の式で、値の幅だけが違う
        struct Mesa
        {
            float CenterX = 0.0f;
            float CenterZ = 0.0f;
            /// 頂の半径[m]と、楕円の縦横比・向き[rad]
            float Radius = 0.0f;
            float Aspect = 1.0f;
            float Angle = 0.0f;
            /// 頂の高さ[m]（周りの地面からの高さ）
            float Height = 0.0f;
            /// 輪郭に足す 2 つの副の楕円（中心の長軸・短軸の向きの位置[m]、主の半径に対する大きさ（0 なら無い）、縦横比）。
            /// 主の楕円との和を取り、輪郭に大きな張り出しと入り込みを作る
            float LobeAlong[2] = {0.0f, 0.0f};
            float LobeAcross[2] = {0.0f, 0.0f};
            float LobeScale[2] = {0.0f, 0.0f};
            float LobeAspect[2] = {1.0f, 1.0f};
            /// 崖の幅[m]（頂の縁から足元まで。狭いほど切り立つ）
            float CliffWidth = 7.0f;
            /// 輪郭の崩れの大きさ（半径に対する割合）
            float EdgeRoughness = 0.28f;
            /// 頂の傾き（頂の中心から Angle と直交する向きへ 1 m 進むごとの高さの変化[m]）
            float TopTilt = 0.0f;
            /// 崖の途中の 2 つの段（高さの割合・崖から張り出す幅[m]）。段は縁の一部（ノイズが正の所）にだけ付く
            float BenchFraction[2] = {0.0f, 0.0f};
            float BenchWidth[2] = {0.0f, 0.0f};
            /// 崖錐（崖の下のなだらかな裾）の高さの割合と、崖から裾の端までの幅[m]
            float TalusFraction = 0.32f;
            float TalusWidth = 70.0f;
        };

        float SampleMesas(float x, float z) const;
        float SampleGridHeight(float x, float z) const;
        TerrainNormal SampleGridNormal(float x, float z, float sampleSpacing) const;

        /// 格子の作り方のときだけ持つ（手続きの作り方では空）
        Container::TSharedPtr<const GridData> m_Grid;
        TerrainHeightFieldParams m_Params;
        Mesa m_Mesas[kMaxMesas];
        uint32_t m_MesaCount = 0;
        /// 砂丘の進む向き（単位ベクトル）
        float m_DuneDirX = 1.0f;
        float m_DuneDirZ = 0.0f;
    };

    /// 遠景の地形の記述（Scripts/Terrain/HorizonDem.py が書く horizon.json）の 1 つのメッシュ。
    /// メッシュのファイルは頂点ごとに float32 × 8（位置・法線・UV）、続けて三角形ごとに uint32 × 3（little-endian、見出しなし）。
    /// 法線が NaN の頂点は近景の格子の外周の頂点で、焼くときに近景の格子の SampleNormal で埋める
    struct TerrainHorizonMesh
    {
        /// メッシュの名前（英数字と _ だけ。クック済みメッシュの論理パスに使う）
        Container::AnsiString Name;
        /// メッシュのファイルのアセットの根からの道
        Container::AnsiString FilePath;
        /// ファイルの SHA-256（16 進の小文字）
        Container::AnsiString Sha256;
        uint32_t VertexCount = 0;
        uint32_t TriangleCount = 0;
    };

    /// 遠景の地形の記述を読み、メッシュの並びを outMeshes に入れる。descriptorPath はアセットの根からの道。
    /// 読めない・形式が約束と違うときは false を返し、outError に理由を書く
    bool LoadTerrainHorizonMeshes(Container::AnsiStringView descriptorPath,
                                  Container::VariableArray<TerrainHorizonMesh> &outMeshes, Container::AnsiString &outError);

    /// 遠景の地形の記述の形式の名前（記述の format）
    inline constexpr const char *kTerrainHorizonFormat = "norveslib.terrain-horizon.v1";

    /// 地表の分類の地図（Scripts/Terrain/TerrainMasks.py が書く surface.png）の、ワールドでの置き場と大きさ。
    /// 地図の u は X の最小から最大へ、v は Z の最小から最大へ（画像の 1 行目が Z の最小 = 北）
    struct TerrainSurfaceMaskLayout
    {
        float MinX = 0.0f;
        float MaxX = 0.0f;
        float MinZ = 0.0f;
        float MaxZ = 0.0f;
        uint32_t Width = 0;  // 画像の列の数
        uint32_t Height = 0; // 画像の行の数
    };

    /// 地表の分類の地図の記述（surface.json）を読み、地図の外縁を outLayout に入れる。descriptorPath はアセットの根からの道。
    /// 読めない・形式や大きさが約束と違う（外縁の幅が列の数 × 画素の広さと合わない）ときは false を返し、outError に理由を書く
    bool LoadTerrainSurfaceMaskLayout(Container::AnsiStringView descriptorPath, TerrainSurfaceMaskLayout &outLayout,
                                      Container::AnsiString &outError);

    /// 地表の分類の地図のある点の 5 つの分類の重み（0〜1。和は 1）
    struct TerrainSurfaceWeights
    {
        /// 露岩
        float Rock = 0.0f;
        /// 崖錐
        float Talus = 0.0f;
        /// 地表（細粒）
        float Fine = 0.0f;
        /// 地表（礫）
        float Gravel = 0.0f;
        /// 砂（沢底と吹きだまり。4 つの和の残り）
        float Sand = 0.0f;
    };

    /// 地表の分類の地図の中身を CPU で引く（草・低木を撒く所を決めるのに使う）。作った後は読み取りだけで、
    /// 複数のスレッドから同時に引いてよい
    class TerrainSurfaceMask
    {
    public:
        /// 地図の記述（surface.json）と、その file が指す PNG を読む。descriptorPath はアセットの根からの道。
        /// 読めない・中身が約束と違う（PNG の大きさが記述と違う）ときは空を返し、outError に理由を書く
        static Container::TSharedPtr<const TerrainSurfaceMask> Load(Container::AnsiStringView descriptorPath,
                                                                    Container::AnsiString &outError);

        /// (x,z) の重みを、画素の中心の間で線形に補間して outWeights に入れる。地図の外なら false（outWeights は変えない）
        bool Sample(float x, float z, TerrainSurfaceWeights &outWeights) const;

        const TerrainSurfaceMaskLayout &GetLayout() const
        {
            return m_Layout;
        }

        /// Load だけが使う
        TerrainSurfaceMask(const TerrainSurfaceMaskLayout &layout, Container::VariableArray<uint8_t> &&pixels);

    private:
        TerrainSurfaceMaskLayout m_Layout;
        /// RGBA8、1 行目が Z の最小（北）
        Container::VariableArray<uint8_t> m_Pixels;
    };

    /// 遠景の地図（Scripts/Terrain/FarMacro.py が書く far.png。R・G = 法線の X・Z、B = 岩肌・A = 土の重み）の記述（far.json）を読み、
    /// 地図の外縁を outLayout に入れる。並びと検査は LoadTerrainSurfaceMaskLayout と同じで、チャンネルの名前だけが違う
    bool LoadTerrainFarMapLayout(Container::AnsiStringView descriptorPath, TerrainSurfaceMaskLayout &outLayout,
                                 Container::AnsiString &outError);

    /// 荒野のシーン（wasteland）の地形の値。クッカーと Game が同じ値を使う
    namespace WastelandTerrain
    {
        /// 地形の一辺の半分[m]（原点が中心の 2 km 四方）
        inline constexpr float kHalfExtent = 1000.0f;
        /// メッシュの格子の間隔[m]
        inline constexpr float kGridSpacing = 1.0f;
        /// 区画の一辺の格子の数（区画ごとに 1 つのメッシュに焼く）
        inline constexpr uint32_t kTileCells = 500u;
        /// 区画の並び（一辺あたり）。kTileCells × kTilesPerSide × kGridSpacing = 2 × kHalfExtent
        inline constexpr uint32_t kTilesPerSide = 4u;
        /// テクスチャの 1 枚が覆う広さ[m]（UV はワールドの XZ をこの値で割る）
        inline constexpr float kTextureTileMeters = 4.0f;
        /// 乱数の種
        inline constexpr uint32_t kSeed = 20261014u;

        /// 区画 (tileX, tileZ) のクック済みメッシュの論理パスの接頭辞。後ろに "<tileX>_<tileZ>.generated" を付ける
        inline constexpr const char *kCookedMeshLogicalPathPrefix = "Assets/Models/WastelandTerrain/WastelandTerrain_";

        /// Valley of the Gods の DEM を切り出した格子の記述（TerrainHeightField::LoadGrid に渡す。アセットの根からの道）。
        /// クッカーと Game が同じファイルを読む
        inline constexpr const char *kDemGridDescriptorPath = "Terrain/ValleyOfTheGods/height.json";

        /// DEM の格子から焼く区画の一辺の升目の数（区画ごとに 1 つのメッシュに焼く）。区画は格子の最初の頂点から並べ、
        /// 東端・南端の区画は残りの升目だけを持つ。Scripts/Terrain/WriteTerrainAssetSet.py もこの値を読む
        inline constexpr uint32_t kDemTileCells = 500u;

        /// DEM の区画 (tileX, tileZ) のクック済みメッシュの論理パスの接頭辞。後ろに "<tileX>_<tileZ>.generated" を付ける
        inline constexpr const char *kDemCookedMeshLogicalPathPrefix = "Assets/Models/WastelandTerrain/WastelandDem_";

        /// 格子の一辺の頂点の数から、その向きの DEM の区画の数を求める（升目の数を kDemTileCells で割って切り上げる）
        inline constexpr uint32_t DemTileCount(uint32_t vertexCount)
        {
            return vertexCount < 2u ? 0u : (vertexCount - 2u) / kDemTileCells + 1u;
        }

        /// DEM の区画 tile の、その向きの最初の升目の番号と升目の数。区画が範囲の外なら升目の数が 0
        inline constexpr void DemTileCellRange(uint32_t tile, uint32_t vertexCount, uint32_t &outFirstCell,
                                               uint32_t &outCellCount)
        {
            outFirstCell = tile * kDemTileCells;
            const uint32_t totalCells = vertexCount < 2u ? 0u : vertexCount - 1u;
            outCellCount = tile < DemTileCount(vertexCount)
                               ? (totalCells - outFirstCell < kDemTileCells ? totalCells - outFirstCell : kDemTileCells)
                               : 0u;
        }

        /// DEM の格子の外側を覆う遠景の地形の記述（LoadTerrainHorizonMeshes に渡す。アセットの根からの道）。
        /// 一覧（Assets/AssetSets/WastelandTerrain.json）では source_path をこの記述にした terrain-tile:dem:<n>,0 が
        /// 記述の n 番目のメッシュを表す（Scripts/Terrain/WriteTerrainAssetSet.py が書く）
        inline constexpr const char *kHorizonDescriptorPath = "Terrain/ValleyOfTheGods/horizon.json";

        /// 遠景のメッシュのクック済みメッシュの論理パスの接頭辞。後ろに "<名前>.generated" を付ける
        inline constexpr const char *kHorizonCookedMeshLogicalPathPrefix = "Assets/Models/WastelandTerrain/WastelandHorizon_";

        /// DEM の格子から焼いた地表の分類の地図の記述（LoadTerrainSurfaceMaskLayout に渡す。アセットの根からの道）
        inline constexpr const char *kSurfaceMaskDescriptorPath = "Terrain/ValleyOfTheGods/surface.json";
        /// 地表の分類の地図のテクスチャの論理パス（一覧 Assets/AssetSets/WastelandTerrain.json が線形の BC7 で焼く）
        inline constexpr const char *kSurfaceMaskTexturePath = "Assets/Terrain/ValleyOfTheGods/surface.png";

        /// 遠景の地図の記述（LoadTerrainFarMapLayout に渡す。アセットの根からの道）
        inline constexpr const char *kFarMapDescriptorPath = "Terrain/ValleyOfTheGods/far.json";
        /// 遠景の地図のテクスチャの論理パス（一覧 Assets/AssetSets/WastelandTerrain.json が線形の BC7 で焼く）
        inline constexpr const char *kFarMapTexturePath = "Assets/Terrain/ValleyOfTheGods/far.png";

        /// 荒野の地形の高さ場の値
        TerrainHeightFieldParams MakeParams();
    } // namespace WastelandTerrain

} // namespace NorvesLib::Core::Rendering::Terrain
