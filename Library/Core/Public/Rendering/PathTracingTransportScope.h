#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief パストレーサーが追う光輸送の範囲
     *
     * Fullは多重散乱をすべて追う参照。DirectOnly・SingleDiffuseBounce・TwoDiffuseBouncesは、
     * 輸送範囲を限ったラスタの近似（直接光だけ、拡散バウンスを数えるRTGI）と同じ範囲の参照を作る
     * 検証用で、発光三角形と太陽円盤は光源標本だけで評価する。拡散バウンスの範囲では、経路は拡散葉だけで
     * 散乱し、散乱光線の命中面も拡散葉（Lambert）だけで照らす（RTGIの命中点と同じ）。1次命中の鏡面反射は、
     * ラスタのIBLの鏡面反射に当たる環境の鏡面反射だけを数える（物体に当たる方向は0。周りの物体の鏡面反射は
     * 範囲に入れない）。
     */
    enum class PathTracingTransportScope : uint32_t
    {
        /** @brief 多重散乱をすべて追う */
        Full = 0,
        /** @brief 1次命中の発光と直接光（光源標本）だけ */
        DirectOnly = 1,
        /** @brief 直接光に、拡散葉だけで散乱した1回の間接光（命中点の発光を除く直接光と環境光）を加える */
        SingleDiffuseBounce = 2,
        /** @brief SingleDiffuseBounceの命中点からもう1回、拡散葉だけで散乱した間接光を加える */
        TwoDiffuseBounces = 3
    };

    /**
     * @brief 1次光線を画素内のどこから出すか
     *
     * Boxは画素内を一様にずらし、累積で画素面積の平均（縁の被覆）を求める。Centerは常に画素中心から
     * 出し、ラスタのGBufferと同じ標本位置にする（縁のaliasingまでラスタと揃えて光輸送だけを比べる検証用）。
     */
    enum class PathTracingPixelSampling : uint32_t
    {
        Box = 0,
        Center = 1
    };

    /**
     * @brief パストレーサーの検証出力
     *
     * None以外では、1次命中面の値を放射輝度の代わりに累積画像へ書く。HitDistanceは1次光線の始点
     * （カメラ）から1次命中点までの距離をRGBへ書き、不交差は0にする。
     */
    enum class PathTracingDebugOutput : uint32_t
    {
        None = 0,
        Albedo = 1,
        ShadingNormal = 2,
        MetallicRoughness = 3,
        HitDistance = 4,
        /** @brief 1次命中点から太陽円盤の1方向が見えるか（試料の平均で可視の割合）。面が太陽に背を向ければ0 */
        SunVisibility = 5
    };
} // namespace NorvesLib::Core::Rendering
