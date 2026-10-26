#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の近景の石と礫
//
// 礫の地面・崖錐・沢底に、輪郭と接地の影が読める大きさ（2 cm〜40 cm）の石を、カメラの周り（数十 m まで）だけ置く。
// 手本の足元の写真（Assets/Reference/ValleyOfTheGods/ground_01.jpg・ground_02.jpg）は、谷床の礫の地面を数 cm の角張った
// 赤褐色〜褐色の砂岩の破片が大きさをばらつかせて半ば土に埋まって覆い、沢底には十数〜数十 cm の赤褐色の角張った石に
// 淡い桃褐色の丸い石が混じる。
//   - 大きさの段: 小礫（2〜5 cm）・石（5〜14 cm）・大きな石（14〜40 cm）の 3 段。段ごとに区画の大きさと作る範囲を変え、
//     小さい段ほどカメラの近くだけに置く。
//   - 消え方: 束の距離の間引き（FoliagePass の FadeStartDistance〜FadeEndDistance）で 1 つずつ縮めて消す。しきい値を大きさに
//     寄せ、小さい石ほど手前で消えるので、画面で 3〜4 画素より小さくなる石は描かない。その先は地形の材質の礫の層
//     （red_laterite_soil_stones。数 mm〜数 cm の礫）が引き継ぐ。
//   - 置く所: 地表の分類の地図（surface.png）の礫・崖錐・沢底の流路の重みと、数 m の群れの斑で密度を決める。細粒の地表には
//     まばらに、吹きだまりの砂にはほとんど置かない。急な斜面（崖）には置かない。
// 石の形は角張った砂岩の破片（いくつかの平面で切った多面体。形と色の違う 3 つの変種）と丸い礫（歪んだ楕円体）で、
// メッシュはここで作る。
// 描くのは草・低木と同じ FoliagePass（風の重み 0）で、束の一覧（FoliageScene）に草・低木の束と並べて渡す。
// 配置は世界の升目の番号と種だけで決まり、どの実行・どのカメラの動きでも同じになる。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Rendering/FoliageTypes.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"
#include "GameModes/Wasteland/WastelandFoliage.h"

#include <cstdint>

namespace Game::GameModes::WastelandGravel
{

    /// 大きさの段の数（WastelandGravel.cpp の kTiers）
    inline constexpr uint32_t kTierCount = 3;
    /// 段ごとの石の種類の数（赤褐色の角張った破片の形と色の変種 3 つ・淡い丸い石 1 つ）
    inline constexpr uint32_t kKindsPerTier = 4;

    /// 石の地面の線の高さ[m]を outY に書く: 中心の地面の高さを、根元の円（半径 radius・向き yaw）の最も低い点より
    /// WastelandFoliage::kRootRingFloatAllowance を超えて浮かない所まで下げた高さ。置き方と置いた物の書き出しが同じ値を使う。
    /// 円の最も高い点が地面の線より WastelandFoliage::kRootRingBurialAllowance を超えて高い（斜面に対して石が大きい）ときは false
    bool StoneGroundLine(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field, float x, float z, float radius,
                         float yaw, float &outY);

    /**
     * @brief カメラの周りの区画へ石と礫を置き、束の一覧を作る
     *
     * GameThread だけが使う。作る束の配列（インスタンス・区画）は作り直すたびに新しい共有の値で、描画スレッドが
     * 前の値を描いている間も書き換えない。
     */
    class GravelScatter
    {
    public:
        /// 石のメッシュ・色・間引きの距離を作り、高さ場と地図を持つ。地図が無い（読めない）ときは置かない
        void Initialize(NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainHeightField> field,
                        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask> mask);

        /// 持っているものを全部手放す
        void Reset();

        bool IsInitialized() const
        {
            return m_pField != nullptr && m_pMask != nullptr;
        }

        /// 置かない範囲（建物の足元・道）と、大きな石を置かない視点の足元の点を替える。作った区画を捨て、次の Update で作り直す
        void SetExclusions(NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionCircle> circles,
                           NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionRect> rects,
                           NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionCircle> viewClearings);

        /// カメラの (x,z) の周りの区画をそろえる。どれかの段の区画の組が変わったら true を返し、outBatches を作り直す。
        /// 変わらなければ false で outBatches に触らない
        bool Update(float cameraX, float cameraZ,
                    NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageBatch> &outBatches);
        /// 置いた物の書き出し（WastelandPlacementExport）: 段 tier の (x, z) を含む区画の石を Update と同じ規則で作り、
        /// 種類ごとに outInstances へ足す。持っている区画と束の一覧は変えない
        void CollectCellAt(uint32_t tier, float x, float z,
                           NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageInstance> (
                               &outInstances)[kKindsPerTier]) const;
        /// 段 tier の区画の一辺[m]（書き出しが区画を数えるのに使う）
        static float GetCellSize(uint32_t tier);
        /// 段 tier の名前（pebble・stone・cobble）
        static const char *GetTierName(uint32_t tier);
        /// 段 tier・種類 kind の束のひな形（メッシュと名前。インスタンスは空）
        const NorvesLib::Core::Rendering::FoliageBatch &GetTemplate(uint32_t tier, uint32_t kind) const
        {
            return m_Templates[tier][kind];
        }

    private:
        /// 1 つの区画の石（種類ごと。FadeThreshold の小さい順）
        struct Cell
        {
            int32_t X = 0;
            int32_t Z = 0;
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageInstance> Instances[kKindsPerTier];
            /// 石の底の高さの範囲[m]
            float MinRootY = 0.0f;
            float MaxRootY = 0.0f;
        };

        /// 1 つの段の区画の組
        struct TierState
        {
            NorvesLib::Core::Container::VariableArray<Cell> Cells;
            int32_t CenterX = 0;
            int32_t CenterZ = 0;
            bool bHasCenter = false;
        };

        /// 段 tier の区画 cell の石を作る
        void BuildCell(uint32_t tier, Cell &cell) const;
        /// (x,z) が置かない範囲か（bLarge なら視点の足元も外す）
        bool IsExcluded(float x, float z, bool bLarge) const;
        /// 持っている区画から束の一覧を作り直す
        void RebuildBatches(NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageBatch> &outBatches) const;

        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainHeightField> m_pField;
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask> m_pMask;
        /// 段・種類ごとの束のひな形（メッシュ・色・間引きの距離。インスタンスと区画は空）
        NorvesLib::Core::Rendering::FoliageBatch m_Templates[kTierCount][kKindsPerTier];
        /// 段・種類ごとのメッシュの地面の線の円の半径（ローカル。WastelandFoliage::ComputeRootRadius）
        float m_RootRadius[kTierCount][kKindsPerTier] = {};
        TierState m_Tiers[kTierCount];
        NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionCircle> m_ExclusionCircles;
        NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionRect> m_ExclusionRects;
        NorvesLib::Core::Container::VariableArray<WastelandFoliage::ExclusionCircle> m_ViewClearings;
    };

} // namespace Game::GameModes::WastelandGravel
