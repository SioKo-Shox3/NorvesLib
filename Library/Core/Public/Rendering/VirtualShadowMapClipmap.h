#pragma once

// 太陽の仮想シャドウマップ（VSM）のクリップマップの CPU の計算。
// 段 L は幅 W0·2^L の正射影のライト空間で、各段が同じ仮想の解像度（既定 16384×16384 texel・128×128 ページ）を持つ。
// 段の中心はカメラの位置をページの格子へスナップするので、カメラが動いても texel の格子のワールドの位置は変わらない。
// ページの表の番地はトーラス（絶対のページの座標 mod 128）で、段の中心がページ単位で動いても残ったページの番地は変わらない。

#include "Rendering/SceneProxy.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t VirtualShadowMapMaxLevels = 16u;

    /**
     * @brief クリップマップの設定
     *
     * BiasLevels は段の選び方の log2 のずれで、受け手の段は texel の一辺が「画面上の 1 画素の大きさ × 2^BiasLevels」
     * 以下の最も粗い段。既定 -0.5 では texel が画素の約 0.35〜0.71 倍になる。
     *
     * 既定を -1 にしないのは、texel の上限と範囲の被覆を両立させるため。段 L の texel と被覆は同じ 2^L で増えるので、
     * 2^BiasLevels·2tan(fovY/2)/画面の高さ ≥ 2·(段 0 の texel)/(段 0 の被覆) ≒ 2.5e-4 のとき、texel を満たす最も粗い段は
     * いつも受け手を覆う。-1 だと 1440 画素・fovY 35 度（1.94 m の受け手）で破れ、-0.5 なら 1440 画素・fovY 35 度まで破れない。
     */
    struct VirtualShadowMapClipmapSettings
    {
        /** @brief 段の数（既定 10 段 = 幅 4〜2048 m） */
        uint32_t LevelCount = 10u;
        /** @brief 段 0 の幅（m）。段 L の幅は FirstWidthMeters·2^L */
        float FirstWidthMeters = 4.0f;
        /** @brief 各段の仮想の解像度（texel。正方形） */
        uint32_t VirtualResolution = 16384u;
        /** @brief 1 ページの一辺（texel） */
        uint32_t PageResolution = 128u;
        /** @brief 深度の範囲の片側（m）。範囲はライトの向きに [深度の原点 − 値, 深度の原点 + 値] */
        float DepthRangeMeters = 1000.0f;
        /** @brief 受け手の段を選ぶときの log2 のずれ */
        float BiasLevels = -0.5f;
        /** @brief 影を受ける最大のカメラからの距離（m）。CSM の MaxShadowDistance と同じ */
        float MaxShadowDistance = 80.0f;
        /** @brief 奥の薄めの幅（MaxShadowDistance に対する割合） */
        float FadeRatio = 0.1f;
    };

    /** @brief クリップマップの 1 段 */
    struct VirtualShadowMapClipmapLevel
    {
        uint32_t Level = 0u;
        float WidthMeters = 0.0f;
        float TexelMeters = 0.0f;
        float PageMeters = 0.0f;
        /** @brief スナップした中心の絶対のページの格子の番号（ページの境界。ライト空間の XY） */
        int64_t CenterPageX = 0;
        int64_t CenterPageY = 0;
        /** @brief 範囲の最小の絶対のページの番号。範囲は [Origin, Origin + 1 ページ数) */
        int64_t OriginPageX = 0;
        int64_t OriginPageY = 0;
        /** @brief 範囲の最小の角のライト空間の位置（m） */
        double OriginLightX = 0.0;
        double OriginLightY = 0.0;
        /** @brief ライト空間の中心（スナップ済み）をワールドへ戻した位置で、深度は DepthCenter */
        Math::Vector3 Center = Math::Vector3::Zero;
        Math::Vector3 LightPosition = Math::Vector3::Zero;
        float NearDepth = 0.0f;
        float FarDepth = 0.0f;
        Math::Matrix4x4 View = Math::Matrix4x4::Identity;
        Math::Matrix4x4 Projection = Math::Matrix4x4::Identity;
    };

    /** @brief 太陽のクリップマップの 1 フレーム分の結果 */
    struct VirtualShadowMapClipmap
    {
        bool bEnabled = false;
        uint64_t LightId = 0;
        Math::Vector3 Direction = Math::Vector3(0.0f, -1.0f, 0.0f);
        Math::Vector3 LightRight = Math::Vector3::UnitX;
        Math::Vector3 LightUp = Math::Vector3::UnitY;
        VirtualShadowMapClipmapSettings Settings;
        uint32_t PagesPerAxis = 0u;
        uint32_t LevelCount = 0u;
        /** @brief 深度の原点（ライト空間の深度。範囲の 1/4 の刻みでスナップ済み） */
        double DepthCenter = 0.0;
        VirtualShadowMapClipmapLevel Levels[VirtualShadowMapMaxLevels];
    };

    /** @brief 設定が使える値か（段数・解像度・ページの大きさ・距離が正で、解像度がページの大きさの倍数） */
    bool IsValidVirtualShadowMapClipmapSettings(const VirtualShadowMapClipmapSettings& settings);

    /** @brief 段の幅（m）。FirstWidthMeters·2^level */
    float VirtualShadowMapLevelWidthMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level);

    /** @brief 段の texel の一辺（m）。段の幅 / VirtualResolution */
    float VirtualShadowMapLevelTexelMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level);

    /**
     * @brief 段が受け手を確実に含める、カメラからの最大の距離（m）
     *
     * 段の範囲の半幅（段の幅の半分）から、中心のスナップのずれ（最大半ページ）と縁のフィルタの余白を合わせた 2 ページを引いた値。
     */
    float VirtualShadowMapLevelCoverageMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level);

    /** @brief カメラからの距離 distance（m）にある面の、画面上の 1 画素の大きさ（m）。distance·2tan(fovY/2)/画面の高さ（画素） */
    float VirtualShadowMapScreenPixelMeters(float distance, float fovYDegrees, float screenHeightPixels);

    /**
     * @brief 受け手の段を選ぶ。texel の一辺が「画素の大きさ × 2^BiasLevels」以下になる最も粗い段で、0 段より細かくは選ばず、
     *        最も粗い段（LevelCount − 1）より粗くもしない。MaxShadowDistance を超える、または値が不正なら -1（影を受けない）。
     *
     * 選んだ段の範囲が受け手に届かない（画面が高精細で画角が狭く、texel を満たす段の半幅が距離より短い）ときだけ、
     * 受け手を含む最も細かい段（VirtualShadowMapLevelCoverageMeters が距離以上の最小の段）まで粗くする（texel の上限は破れる）。
     * 既定では 360〜1440 画素・fovY 35〜90 度の画面では起きない（BiasLevels の説明の式を満たさない画面でだけ起きる）。
     */
    int32_t SelectVirtualShadowMapLevel(const VirtualShadowMapClipmapSettings& settings,
                                        float distance,
                                        float fovYDegrees,
                                        float screenHeightPixels);

    /** @brief VirtualShadowMapLevelDistanceThresholds が、その段へ届く距離が無い（MaxShadowDistance までに選ばれない）ときに入れる値 */
    inline constexpr float VirtualShadowMapUnreachableDistance = 1.0e30f;

    /**
     * @brief 段を選ぶ距離のしきい値。outThresholds[k]（k = 0 .. LevelCount − 2）は、SelectVirtualShadowMapLevel が k + 1 以上を返す最小の距離
     *
     * 選ぶ段は距離について単調に増えるので、距離 d の段は「d >= outThresholds[k] となる k の数」に等しい（d が MaxShadowDistance 以下のとき）。
     * 計算シェーダーが段の選び方の式を写さず、CPU の SelectVirtualShadowMapLevel と同じ結果を得るために使う（二分法で求める）。
     * MaxShadowDistance までに届かない段は VirtualShadowMapUnreachableDistance。設定が不正なら何も書かず false。
     * outThresholds は VirtualShadowMapMaxLevels − 1 個以上の領域。
     */
    bool VirtualShadowMapLevelDistanceThresholds(const VirtualShadowMapClipmapSettings& settings,
                                                 float fovYDegrees,
                                                 float screenHeightPixels,
                                                 float* outThresholds);

    /** @brief 奥の薄めの重み（0 = 影のまま、1 = 影なし）。MaxShadowDistance の奥の FadeRatio の幅で滑らかに 0 → 1 */
    float VirtualShadowMapShadowFadeWeight(const VirtualShadowMapClipmapSettings& settings, float distance);

    /** @brief 絶対のページの座標のトーラスの番地（絶対のページの座標 mod ページ数。負の座標でも 0 以上） */
    uint32_t VirtualShadowMapPageTorusAddress(int64_t absolutePage, uint32_t pagesPerAxis);

    /** @brief ワールドの位置をライト空間（XY はライトの向きに垂直、depth は光の進む向き）の座標へ */
    void VirtualShadowMapWorldToLightSpace(const VirtualShadowMapClipmap& clipmap,
                                           const Math::Vector3& worldPosition,
                                           double& outX,
                                           double& outY,
                                           double& outDepth);

    /** @brief ライト空間の XY が段の範囲の中か。中なら絶対のページの座標を返す */
    bool VirtualShadowMapLevelFindPage(const VirtualShadowMapClipmap& clipmap,
                                       uint32_t level,
                                       double lightX,
                                       double lightY,
                                       int64_t& outPageX,
                                       int64_t& outPageY);

    /**
     * @brief 太陽の向きとカメラの位置からクリップマップを作る（ライト空間の基底は CSM と同じ規則）。
     *        設定が不正・向きが有限でない・位置が有限でないときは bEnabled = false。
     */
    VirtualShadowMapClipmap BuildVirtualShadowMapClipmap(const Math::Vector3& lightDirection,
                                                         uint64_t lightId,
                                                         const Math::Vector3& cameraPosition,
                                                         const VirtualShadowMapClipmapSettings& settings);

    /**
     * @brief CSM と同じ太陽（SelectShadowedDirectionalLight）とカメラからクリップマップを作る。
     *        太陽が無い・影を落とせないときは bEnabled = false。
     */
    VirtualShadowMapClipmap BuildVirtualShadowMapClipmap(const Container::VariableArray<LightProxy>* lightProxies,
                                                         const CameraProxy& camera,
                                                         const VirtualShadowMapClipmapSettings& settings);
} // namespace NorvesLib::Core::Rendering
