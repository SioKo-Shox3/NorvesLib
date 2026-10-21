#pragma once

/**
 * @file FoliageTypes.h
 * @brief 草・低木の株（葉や枝の束）をインスタンスの配列から描くためのデータ
 *
 * 1 つの束（FoliageBatch）は、1 つの株のメッシュと、その株を置くインスタンスの配列と、色と風の値を持つ。
 * GameThread が RenderWorld::SetFoliageBatches で束の一覧を与え、毎フレームの FramePacket に写して RenderThread へ渡す。
 * メッシュとインスタンスの配列は作った後に書き換えない共有の値（TSharedPtr<const ...>）で、写しは参照を数えるだけで済む。
 * 描くのは FoliagePass（ビジビリティの解決の後に GBuffer へ直接ラスタする）で、束ごとに 1 回のインスタンス描画にする。
 * 太陽の影は、カメラに近い株だけを VirtualShadowMapPass が同じ置き方で仮想シャドウマップへ描く（VirtualShadowMapFoliageCasters.h）。
 * 地形全体へ撒く束は、インスタンスを区画（FoliageCell）ごとにまとめ、区画ごとに視錐台で外し、距離で株を間引く
 * （区画ごとに 1 回のインスタンス描画）。
 */

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 株のメッシュの頂点（52 バイト。foliage.vert の頂点の入力と同じ並び）
     *
     * 葉・枝は幅の狭い帯か細い角柱で、Side は帯の中心線からこの頂点までの横の向きと長さ（半分の幅）。遠くで帯が画素より
     * 細くなると、頂点シェーダーが Side の向きへ広げて 1 画素の幅を保つ（切れ切れの点にならない）。
     */
    struct FoliageVertex
    {
        /// 株の根元を原点とする位置[m]（+Y が上）
        float Position[3] = {0.0f, 0.0f, 0.0f};
        /// 表の側の法線
        float Normal[3] = {0.0f, 1.0f, 0.0f};
        /// 中心線からこの頂点への横の向きと長さ[m]（中心線の上の頂点は 0）
        float Side[3] = {0.0f, 0.0f, 0.0f};
        /// 風でしなる量の重み（根元 0、先 1）
        float Flex = 0.0f;
        /// 茎の色（0）から先の色（1）への混ぜ具合。葉・枝ごとの揺らぎを含む
        float Tint = 0.0f;
        /// 株の中の遮蔽（1 は遮られない。株の内側・根元ほど小さい）
        float Occlusion = 1.0f;
        /// 葉・枝ごとの細かな揺れの位相（0〜1。同じ葉・枝の頂点は同じ値）
        float Phase = 0.0f;
    };
    static_assert(sizeof(FoliageVertex) == 52, "foliage.vert の頂点の入力と合わせる");

    /// 株のメッシュ（作った後は書き換えない）
    struct FoliageMeshData
    {
        Container::VariableArray<FoliageVertex> Vertices;
        Container::VariableArray<uint32_t> Indices;
        /// 株の高さ[m]（インスタンスの高さの倍率が 1 のとき）。風のしなりの大きさをこれに比例させる
        float Height = 0.0f;
    };

    /**
     * @brief 株 1 つの置き方（36 バイト。foliage.vert のインスタンスの入力と同じ並び）
     */
    struct FoliageInstance
    {
        /// 根元のワールドの位置[m]
        float Position[3] = {0.0f, 0.0f, 0.0f};
        /// Y 軸まわりの向き[rad]
        float Yaw = 0.0f;
        /// 横（X・Z）と高さ（Y）の倍率
        float WidthScale = 1.0f;
        float HeightScale = 1.0f;
        /// 色の揺らぎ（0〜1。先の色 TipColorA から TipColorB への混ぜ具合）
        float ColorVariation = 0.0f;
        /// 風の揺れの位相（0〜1）
        float WindPhase = 0.0f;
        /// 距離で間引くときのしきい値（0〜1 の一様な乱数）。距離で決まる残す割合がこれを下回ると、株は縮んで消える。
        /// 株ごとに違うので、消える距離が株ごとにばらつき、消え方が線にならない
        float FadeThreshold = 0.0f;
    };
    static_assert(sizeof(FoliageInstance) == 36, "foliage.vert のインスタンスの入力と合わせる");

    /**
     * @brief 束の中の 1 つの区画（インスタンスの配列の連続した範囲と、その範囲を囲む箱）
     *
     * FoliagePass は区画ごとに、箱が視錐台の外なら描かず、カメラから箱までの最短の距離で残す割合を求め、しきい値がそれを
     * 下回る株だけを描く。区画の中のインスタンスは FadeThreshold の小さい順に並べる（先頭からの個数で切れる）。
     */
    struct FoliageCell
    {
        /// 区画の株を囲むワールドの箱[m]（風で揺れる分を含める）
        float BoundsMin[3] = {0.0f, 0.0f, 0.0f};
        float BoundsMax[3] = {0.0f, 0.0f, 0.0f};
        /// 束のインスタンスの配列の中の範囲
        uint32_t FirstInstance = 0;
        uint32_t InstanceCount = 0;
    };

    /**
     * @brief 遠い株の太陽の影（VSM）だけに使う簡略のメッシュの段
     *
     * 頂点の形式は束の Mesh と同じで、しなりの重み・揺れの位相も同じ値を持つ（風の置き方の株の高さは束の Mesh の Height を使う）。
     */
    struct FoliageShadowLod
    {
        Container::TSharedPtr<const FoliageMeshData> Mesh;
        /// この段を使い始める、カメラから根元までの距離[m]
        float StartDistance = 0.0f;
        /// true なら影を風で揺らさない（揺れが影の texel より小さい遠い段。ページのキャッシュを毎フレーム捨てない）
        bool bStill = false;
    };

    /**
     * @brief 1 種類の株の束（1 回のインスタンス描画で描く）
     */
    struct FoliageBatch
    {
        Container::TSharedPtr<const FoliageMeshData> Mesh;
        Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> Instances;
        /// 区画の一覧（Instances の範囲。作った後は書き換えない）。空なら全部のインスタンスを 1 回で描き、区画のカリングをしない
        Container::TSharedPtr<const Container::VariableArray<FoliageCell>> Cells;
        /// 距離で間引く範囲[m]。FadeStartDistance までは全部の株を描き、そこから FadeEndDistance へ向けて残す割合を 1 から 0 へ
        /// 下げる（株は FadeThreshold が残す割合を下回ると縮んで消える）。FadeEndDistance が FadeStartDistance 以下なら間引かない
        float FadeStartDistance = 0.0f;
        float FadeEndDistance = 0.0f;
        /// 太陽の影（VSM）を落とす株の、カメラから根元までの距離の上限[m]（0 で落とさない）。内側の株を近い順に、
        /// 株の影の塊の予算（VirtualShadowMapFoliageCasters.h）に収まるだけ影のパスで描く。外の株の接地は照明の接触影だけで出る
        float ShadowDistance = 25.0f;
        /// 影を落とす株を選ぶ順の段（小さい段の株を全部見てから次の段へ。同じ段の中は近い順）。予算は小さい段の株が先に使う
        uint32_t ShadowPriority = 0;
        /// 遠い株の影の簡略のメッシュ（StartDistance の小さい順）。空ならどの距離の株も Mesh で影を描く
        Container::VariableArray<FoliageShadowLod> ShadowLods;
        /// 茎・根元の色と、先の色の 2 つ（線形のアルベド）。インスタンスの ColorVariation で先の 2 色を混ぜる
        float StemColor[3] = {0.2f, 0.17f, 0.12f};
        float TipColorA[3] = {0.4f, 0.3f, 0.15f};
        float TipColorB[3] = {0.3f, 0.25f, 0.18f};
        /// 粗さ
        float Roughness = 0.85f;
        /// 風の強さ（強い突風のとき、先が株の高さの何倍ずれるか）
        float WindStrength = 0.15f;
        /// 揺れの周波数[Hz]（株の固有の揺れ。細い草ほど速い）
        float WindSwayFrequency = 0.6f;
        /// 帯を広げて保つ画面上の最小の幅[画素]（0 で広げない）と、元の幅に対する広げる上限の倍率
        float MinPixelWidth = 0.75f;
        float MaxWidenRatio = 3.0f;
        /// 名前（ログ用）
        const char *DebugName = "foliage";
    };

    /**
     * @brief 草・低木の全部の束に共通の風
     */
    struct FoliageWindParameters
    {
        /// 風の吹いていく向き（XZ。長さ 1 にして使う）
        float DirectionX = 1.0f;
        float DirectionZ = 0.0f;
        /// 突風の塊が流れる速さ[m/s]と、塊の大きさ[m]
        float GustSpeed = 4.0f;
        float GustScale = 14.0f;
        /// 風の時計に足す値[s]（撮影で、同じ構図の時刻だけを変えた 2 枚を撮るのに使う）
        float TimeOffsetSeconds = 0.0f;
    };

    /// 風の時計の折り返し[s]（float の精度を保つ。折り返しの瞬間だけ揺れが飛ぶ）
    inline constexpr double FoliageWindClockWrapSeconds = 4096.0;

    /// 描画のフレームの時刻 totalTime[s] での風の時計[s]（GBuffer の描画と影の描画が同じ値で揺らす）
    inline float FoliageWindClock(double totalTime, const FoliageWindParameters &wind)
    {
        return static_cast<float>(std::fmod(totalTime, FoliageWindClockWrapSeconds)) + wind.TimeOffsetSeconds;
    }

    /// 風の吹いていく向きを長さ 1 にした XZ（長さが 0 なら +X）
    inline void FoliageWindDirection(const FoliageWindParameters &wind, float &outX, float &outZ)
    {
        const float length = std::sqrt(wind.DirectionX * wind.DirectionX + wind.DirectionZ * wind.DirectionZ);
        outX = length > 1.0e-6f ? wind.DirectionX / length : 1.0f;
        outZ = length > 1.0e-6f ? wind.DirectionZ / length : 0.0f;
    }

    /// FramePacket で渡す草・低木の一覧
    struct FoliageScene
    {
        Container::VariableArray<FoliageBatch> Batches;
        FoliageWindParameters Wind;

        bool IsEmpty() const
        {
            return Batches.empty();
        }
    };

} // namespace NorvesLib::Core::Rendering
