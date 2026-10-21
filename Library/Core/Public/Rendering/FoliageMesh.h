#pragma once

/**
 * @file FoliageMesh.h
 * @brief 草・低木の株のメッシュの手続き生成（アルファ抜きを使わず、細い帯と細い角柱だけで作る）
 *
 * - 枯れ草の株: 根元の小さな円から、外へ反って垂れる細い葉（先へ細くなる帯）を数〜十数枚と、まっすぐな穂の茎を数本束ねる。
 * - ブラックブラッシュ（Coleogyne ramosissima）: 根元から斜めに出る硬い枝が 2 段に分かれ、丸い樹冠の中に細い枝が詰まる。
 *   枝先の短い枝に小さな灰緑の葉が数枚ずつ付く。
 * - モルモンティー（Ephedra viridis）: 根元から立ち上がる多くの細い緑の茎が、節ごとに上向きの細い茎を出すほうき状の株。
 * どれも種（Seed）が同じなら同じ形になる。高さ・広がりは記述の値で、置くときにインスタンスの倍率でさらに変える。
 */

#include "Container/PointerTypes.h"
#include "Rendering/FoliageTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::FoliageMesh
{
    /// 枯れ草の株
    struct GrassClumpDesc
    {
        uint32_t Seed = 1;
        /// 葉の数
        uint32_t BladeCount = 14;
        /// 穂の茎の数
        uint32_t CulmCount = 3;
        /// 株の高さ[m]（葉の長さの目安。反って垂れるので先はこれより低い）
        float Height = 0.35f;
        /// 根元の円の半径[m]
        float BaseRadius = 0.035f;
        /// 葉の根元の幅[m]
        float BladeWidth = 0.006f;
        /// 葉 1 枚の区切りの数
        uint32_t Segments = 6;
    };

    /// 細い枝の低木の形
    enum class ShrubForm : uint8_t
    {
        /// 丸い樹冠に硬い枝が詰まる（ブラックブラッシュ）
        Rounded,
        /// 立ち上がる細い茎のほうき状（モルモンティー）
        Upright,
    };

    /// 細い枝の低木
    struct ShrubDesc
    {
        uint32_t Seed = 1;
        ShrubForm Form = ShrubForm::Rounded;
        /// 株の高さ[m]と、樹冠の横の半径[m]
        float Height = 0.6f;
        float CrownRadius = 0.45f;
        /// 根元から出る太い枝（茎）の数
        uint32_t StemCount = 10;
        /// 枝分かれの段の数（茎を含む。3 なら茎 → 枝 → 棘のような短い枝先）
        uint32_t BranchLevels = 3;
        /// 最後の段の短い枝先の長さ[m]（3 段以上のとき）
        float SpurLength = 0.06f;
        /// 根元の枝の半径[m]と、細い枝先の半径[m]
        float StemRadius = 0.007f;
        float TwigRadius = 0.0015f;
        /// 枝先の短い枝 1 本に付ける葉の数（0 で葉を付けない）
        uint32_t LeavesPerTwig = 4;
        /// 葉の長さ[m]と幅[m]
        float LeafLength = 0.012f;
        float LeafWidth = 0.004f;
    };

    /**
     * @brief 遠い株の太陽の影に使う、低木の簡略のメッシュの作り方
     *
     * 本体と同じ枝と葉の並び（同じ種）から、細い枝の角柱を省き、葉を間引いて残りを大きくする。葉は LeafKeepEvery 枚に 1 枚を残し、
     * 長さと幅をどちらも √LeafKeepEvery 倍にして（葉の中ほどを中心に広げる）、樹冠の葉の面積の合計を保つ。
     */
    struct ShrubShadowProxyDesc
    {
        /// 角柱を残す枝の段の上限（0 = 根元の枝だけ。負なら枝を残さない）
        int32_t MaxPrismLevel = 1;
        /// 葉を何枚に 1 枚残すか（1 で全部）
        uint32_t LeafKeepEvery = 4;
    };

    /// 枯れ草の株のメッシュを作る
    Container::TSharedPtr<FoliageMeshData> BuildGrassClump(const GrassClumpDesc &desc);

    /// 細い枝の低木のメッシュを作る
    Container::TSharedPtr<FoliageMeshData> BuildShrub(const ShrubDesc &desc);

    /**
     * @brief 細い枝の低木のメッシュと、同じ形の影の簡略のメッシュ（proxyCount 個）を作る
     *
     * 本体は BuildShrub(desc) と同じ（簡略のメッシュを作っても乱数の進み方は変わらない）。outProxies[i] は proxies[i] の作り方で作る。
     */
    Container::TSharedPtr<FoliageMeshData> BuildShrub(const ShrubDesc &desc, const ShrubShadowProxyDesc *proxies, uint32_t proxyCount,
                                                      Container::TSharedPtr<FoliageMeshData> *outProxies);

} // namespace NorvesLib::Core::Rendering::FoliageMesh
