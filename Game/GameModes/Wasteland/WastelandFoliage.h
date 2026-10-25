#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の草・低木
//
// 枯れ草の株とブラックブラッシュ・モルモンティーの低木を、地形全体へ撒く。撒くのはカメラの周り（間引いて消える距離まで）の
// 区画（24 m 四方）だけで、カメラが区画を跨いだら近づいた区画を作り、離れた区画を捨てる。区画の中の株は、地表の分類の地図
// （Scripts/Terrain/TerrainMasks.py の surface.png。露岩・崖錐・沢底の砂には生えない）と、数十 m・数 m の密度の斑と、
// 傾斜で決め、撒かない範囲（視点の足元・建物・道）を外す。株の位置・形・色は世界の升目の番号と種だけで決まり、
// どの実行・どのカメラの動きでも同じになる。
// 株の種類ごとに 1 つの束（FoliageBatch）にし、区画を束の区画（FoliageCell）にする。描画（FoliagePass）が区画ごとに
// 視錐台で外し、距離で株を間引いて 80〜120 m 先で消す。
// 色は手本の足元の写真（Assets/Reference/ValleyOfTheGods/ground_01.jpg）の植生に合わせた線形のアルベド。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Rendering/FoliageTypes.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

#include <cstdint>

namespace Game::GameModes::WastelandFoliage
{

    /// 草・低木を撒かない円（視点の足元・建物の足元）
    struct ExclusionCircle
    {
        float X = 0.0f;
        float Z = 0.0f;
        float Radius = 0.0f;
    };

    /// 草・低木を撒かない向きのある長方形（道）。Along は長さの向き（XZ の単位ベクトル）
    struct ExclusionRect
    {
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float AlongX = 0.0f;
        float AlongZ = 1.0f;
        float HalfLength = 0.0f;
        float HalfWidth = 0.0f;
    };

    /// 種類の数（束の並び。WastelandFoliage.cpp の PlantKind）
    inline constexpr uint32_t kPlantKindCount = 5;

    /**
     * @brief カメラの周りの区画へ草・低木を撒き、束の一覧を作る
     *
     * GameThread だけが使う。Update が返す束の配列（インスタンス・区画）は作り直すたびに新しい共有の値で、描画スレッドが
     * 前の値を描いている間も書き換えない。
     */
    class FoliageScatter
    {
    public:
        /// 株のメッシュ・色・間引きの距離を作り、高さ場と地図を持つ。地図が無い（読めない）ときは傾斜と密度の斑だけで撒く
        void Initialize(NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainHeightField> field,
                        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask> mask);

        /// 持っているものを全部手放す
        void Reset();

        bool IsInitialized() const
        {
            return m_pField != nullptr;
        }

        /// 撒かない範囲を替える。作った区画を捨て、次の Update で作り直す
        void SetExclusions(NorvesLib::Core::Container::VariableArray<ExclusionCircle> circles,
                           NorvesLib::Core::Container::VariableArray<ExclusionRect> rects);

        /// カメラの (x,z) の周りの区画をそろえる。区画の組が変わったら true を返し、inOutScene の束を作り直す
        /// （風の値はそのまま残す）。変わらなければ false で inOutScene に触らない
        bool Update(float cameraX, float cameraZ, NorvesLib::Core::Rendering::FoliageScene &inOutScene);
        /// 置いた物の書き出し（WastelandPlacementExport）: (x, z) を含む区画の株を Update と同じ規則で作り、種類ごとに
        /// outInstances へ足す。持っている区画と束の一覧は変えない
        void CollectCellAt(float x, float z,
                           NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageInstance> (
                               &outInstances)[kPlantKindCount]);
        /// 区画の一辺[m]（書き出しが全域の区画を数えるのに使う）
        static float GetCellSize();
        /// 種類 kind の束のひな形（メッシュと名前。インスタンスは空）
        const NorvesLib::Core::Rendering::FoliageBatch &GetTemplate(uint32_t kind) const
        {
            return m_Templates[kind];
        }

    private:
        /// 1 つの区画の株（種類ごと。FadeThreshold の小さい順）
        struct Cell
        {
            int32_t X = 0;
            int32_t Z = 0;
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageInstance> Instances[kPlantKindCount];
            /// 株の根元の高さの範囲[m]
            float MinRootY = 0.0f;
            float MaxRootY = 0.0f;
        };

        /// 区画 (cellX, cellZ) の株を作る
        void BuildCell(Cell &cell);
        /// (x,z) が撒かない範囲か
        bool IsExcluded(float x, float z) const;
        /// 持っている区画から束の一覧を作り直す
        void RebuildScene(NorvesLib::Core::Rendering::FoliageScene &inOutScene) const;

        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainHeightField> m_pField;
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask> m_pMask;
        /// 種類ごとの束のひな形（メッシュ・色・風・間引きの距離。インスタンスと区画は空）
        NorvesLib::Core::Rendering::FoliageBatch m_Templates[kPlantKindCount];
        NorvesLib::Core::Container::VariableArray<Cell> m_Cells;
        NorvesLib::Core::Container::VariableArray<ExclusionCircle> m_ExclusionCircles;
        NorvesLib::Core::Container::VariableArray<ExclusionRect> m_ExclusionRects;
        /// 最後にそろえたときのカメラの区画。区画が変わらなければ何もしない
        int32_t m_CenterCellX = 0;
        int32_t m_CenterCellZ = 0;
        bool m_bHasCenter = false;
        /// 最後の Update で作った区画のうち、地図（露岩・崖錐・沢底の流路）で外した候補の数（ログ用）
        uint32_t m_MaskRejectedCandidates = 0;
    };

    /// 風の向きと突風の値（束の一覧の Wind に入れる）
    NorvesLib::Core::Rendering::FoliageWindParameters MakeWind();

    /// 視点の名前に応じた風の時計のずらし[s]（grass_gust は grass と同じ構図で時刻だけを変えた 2 枚目）
    float WindTimeOffsetForView(const NorvesLib::Core::Container::String &viewName);

    /// 低木 1 株を比べる撮影（--wasteland-hide=all-but-plant）で置く株の根元の (x, z)[m]。南東の谷床で周り 12 m の傾きが
    /// 0.1° の平らな所。SceneCatalog.h の plant_near・plant_far がこの株を見る
    inline constexpr float kReferencePlantX = 360.0f;
    inline constexpr float kReferencePlantZ = 1965.0f;

    /// 比べる 1 株の束: 葉の付いた丸い低木（ブラックブラッシュ A）を 1 株だけ、根元を field の高さに置く。
    /// 正解の絵（Scripts/Terrain/BlenderReference.py --plant）と形を合わせるため、風で揺らさず、距離で間引かない
    NorvesLib::Core::Rendering::FoliageBatch MakeReferencePlant(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field);

    /// 比べる 1 株の書き出しの記録
    struct ReferencePlantExport
    {
        uint64_t VertexCount = 0;
        uint64_t TriangleCount = 0;
        /// 根元からの水平の最大の距離[m]と、根元からの高さの最大[m]（ワールドの頂点から）
        float CrownRadius = 0.0f;
        float Height = 0.0f;
    };

    /// 比べる 1 株を、foliage.vert と同じ置き方（インスタンスの位置・Y 軸まわりの回転・横と高さの倍率）と同じ色（線形の
    /// アルベド）のワールドの三角形にして、PLY（binary_little_endian。頂点は位置・法線・色の float 9 つ、面は 3 角の int）で
    /// path に書く。風のずれは無い株なので足さず、遠くで細い帯を広げる頂点シェーダーの処理は入れない（形そのもの）。
    /// 書けなければ false
    bool ExportReferencePlant(const NorvesLib::Core::Rendering::FoliageBatch &batch,
                              const NorvesLib::Core::Container::String &path, ReferencePlantExport &outRecord);

} // namespace Game::GameModes::WastelandFoliage
