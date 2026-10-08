#pragma once

// 点光源の仮想シャドウマップ（VSM。--point-shadow-method=vsm）の面と解像度の段の CPU の計算。
// 影を落とす点光源（PointShadowSnapshot の最大 4 灯）ごとに 6 面（キューブの層の順 +X,-X,+Y,-Y,+Z,-Z）× 解像度の段を、
// 太陽のクリップマップの段と同じスライスの表（GPUVsmSlice）へ並べる。面の行列は PointShadowFaceMatrices と同じ（90 度・near 0.05 m・far = Range）。
// 面の深度は面の軸の向きの線形の距離 ÷ Range（[0,1]、0 が光源の側）。
// 面 f の解像度の段 m は一辺 FaceResolution >> m の正方形で、1 段の一辺のページの数は (FaceResolution >> m) / PageResolution
// （既定 4096² = 32 ページ四方 → 段 5 の 128² = 1 ページ）。ページの座標は面の NDC（面の軸の距離で割った sc, tc）を [0,1] へ写したもの。

#include "Rendering/PointShadowSnapshot.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 点光源の VSM の設定 */
    struct VirtualShadowMapPointSettings
    {
        /** @brief 面の段 0 の解像度（texel。正方形）。2 の冪 */
        uint32_t FaceResolution = 4096u;
        /** @brief 1 ページの一辺（texel）。2 の冪 */
        uint32_t PageResolution = 128u;
        /** @brief 解像度の段の数（段 m の一辺は FaceResolution >> m。最も粗い段でもページの一辺以上） */
        uint32_t MipCount = 6u;
        /** @brief 受け手の段を選ぶときの log2 のずれ（太陽のクリップマップと同じ既定） */
        float BiasLevels = -0.5f;
    };

    /** @brief 1 フレームの点光源の VSM のスライスの並び */
    struct VirtualShadowMapPointLights
    {
        VirtualShadowMapPointSettings Settings;
        uint32_t LightCount = 0u;
        /** @brief スライスの表の中の先頭の番号（太陽の段の後ろに並べるときの太陽の段の数） */
        uint32_t FirstSlice = 0u;
        /** @brief 1 灯のスライスの数（6 面 × 段の数） */
        uint32_t SlicesPerLight = 0u;
        uint64_t LightId[PointShadowMaxLights] = {};
        Math::Vector3 Position[PointShadowMaxLights] = {};
        float Range[PointShadowMaxLights] = {};

        /** @brief 点光源のスライスの総数（LightCount × SlicesPerLight） */
        uint32_t SliceCount() const { return LightCount * SlicesPerLight; }
    };

    /** @brief 受け手の位置から求めた、面・段・ページ */
    struct VirtualShadowMapPointReceiver
    {
        /** @brief 面の番号（キューブの層の順） */
        uint32_t Face = 0u;
        /** @brief 解像度の段 */
        uint32_t Mip = 0u;
        /** @brief スライスの表の番号（VirtualShadowMapPointSliceIndex） */
        uint32_t Slice = 0u;
        /** @brief 段の中のページの座標 [0, 1 段の一辺のページ数) */
        uint32_t PageX = 0u;
        uint32_t PageY = 0u;
        /** @brief 面の NDC（sc / 軸の距離, tc / 軸の距離。[-1,1]） */
        float NdcX = 0.0f;
        float NdcY = 0.0f;
        /** @brief 面の軸の向きの線形の距離（m）と、それを Range で割った深度 [0,1] */
        float AxialDistance = 0.0f;
        float Depth = 0.0f;
    };

    /**
     * @brief シェーダーの VsmPointSampleParams（std140。Common/VirtualShadowMapParams.glsl）と同じ並び。照明・影の測定・テストが
     *        点光源の VSM を読む（Common/VirtualShadowMapPoint.glsl）パラメータ
     */
    struct GPUVsmPointSampleParams
    {
        /** @brief x = 灯の数（0 なら点光源の VSM は読まない）、y = 点光源のスライスの先頭の番号、z = 解像度の段の数、w = 物理ページの数 */
        uint32_t header[4];
        /** @brief x = カメラからの距離 1 m あたりの画素の大きさ（m）、y = 段を選ぶ目標 texel の係数（2^bias）、z = PCF の半径の下限の画素数、w = 予約（0） */
        float tuning[4];
        /** @brief xyz = カメラの位置 */
        float cameraPosition[4];
        /** @brief x = 面の近い平面の距離（m）、y = 面の段 0 の解像度（texel） */
        float plane[4];
        /** @brief xyz = 灯の位置、w = Range（灯の順） */
        float lights[PointShadowMaxLights][4];
    };
    static_assert(PointShadowMaxLights == 4u && sizeof(GPUVsmPointSampleParams) == 128,
                  "Common/VirtualShadowMapParams.glsl の VsmPointSampleParams と同じ大きさにすること");

    /**
     * @brief 点光源の VSM を読むパラメータを作る
     *
     * 灯が無い・設定が不正・カメラや画角が不正・物理ページが 0 のときは、全部 0（header[0] = 0 = 読まない）にして false を返す。
     * 呼び出し側は無効のパラメータを渡し、キューブのまま描く。印付け（vsm_mark.comp）と同じ式で段を選ぶので、tuning[0] は
     * 1 m あたりの画素の大きさ（VirtualShadowMapScreenPixelMeters(1, fov, 高さ)）、tuning[1] は 2^BiasLevels。
     *
     * @param cameraPosition カメラの位置（ワールド。3 要素）
     * @param poolPages 物理ページの数
     */
    bool BuildVirtualShadowMapPointSampleParams(const VirtualShadowMapPointLights& lights,
                                                const float* cameraPosition,
                                                float fovYDegrees,
                                                float screenHeightPixels,
                                                uint32_t poolPages,
                                                GPUVsmPointSampleParams& outParams);

    /** @brief 設定が使える値か（解像度・ページの大きさが 2 の冪、段の数が 1 以上で最も粗い段でもページの一辺以上、MipCount が 16 以下） */
    bool IsValidVirtualShadowMapPointSettings(const VirtualShadowMapPointSettings& settings);

    /** @brief 段 mip の一辺（texel）。FaceResolution >> mip */
    uint32_t VirtualShadowMapPointMipResolution(const VirtualShadowMapPointSettings& settings, uint32_t mip);

    /** @brief 段 mip の一辺のページの数。(FaceResolution >> mip) / PageResolution */
    uint32_t VirtualShadowMapPointPagesPerAxis(const VirtualShadowMapPointSettings& settings, uint32_t mip);

    /**
     * @brief 点光源のスナップショットから、点光源のスライスの並びを作る
     *
     * 灯の順と選び方は BuildPointShadowSnapshot のまま（灯 i の面の層は 6i + 面）。設定が不正なら LightCount = 0。
     * 灯の数 × 6 面 × 段の数が、firstSlice から始めて VirtualShadowMapMaxSlices に収まらないときも LightCount = 0。
     */
    VirtualShadowMapPointLights BuildVirtualShadowMapPointLights(const PointShadowSnapshot& snapshot,
                                                                 const VirtualShadowMapPointSettings& settings,
                                                                 uint32_t firstSlice);

    /** @brief 灯・面・段のスライスの表の番号。FirstSlice + (灯 × 6 + 面) × 段の数 + 段 */
    uint32_t VirtualShadowMapPointSliceIndex(const VirtualShadowMapPointLights& lights, uint32_t light, uint32_t face, uint32_t mip);

    /** @brief 灯の数・LightId・位置・Range・設定・先頭の番号のどれかが違うか（VSM_POINT を出し直す判定） */
    bool VirtualShadowMapPointLightsDiffer(const VirtualShadowMapPointLights& lhs, const VirtualShadowMapPointLights& rhs);

    /**
     * @brief 点光源のスライスを、スライスの表の FirstSlice から SliceCount() 件書く（outSlices は FirstSlice + SliceCount() 件以上の領域）
     *
     * 投影は透視（extra[2] = VirtualShadowMapSliceProjectionPerspective）。axisX/Y/Z は、ワールドの位置 p を面の座標
     * （x = sc の距離、y = tc の距離、z = 面の軸の向きの距離）へ写す行（w は光源の位置の分のずれ）で、NDC は (x / z, y / z)。
     * info[0] = ページの NDC の幅（2 / 1 段の一辺のページ数）、info[1] = texel の NDC の幅（2 / 段の一辺）、info[2] = Range（m）、info[3] = 近い平面の距離（m。PointShadowNearPlane）。
     * origin[0..1] = 0（面の全体がページの範囲）、origin[2] = スライスの番号 × 128 × 128（太陽の段と同じ並べ方）、origin[3] = 1 段の一辺のページ数。
     * extra[0..1] = 0。使わない灯の分は書かない。
     * @return 書いたスライスの数
     */
    uint32_t BuildVirtualShadowMapPointSlices(const VirtualShadowMapPointLights& lights, GPUVsmSlice* outSlices);

    /** @brief 向き（光源から受け手へ）の主軸の面。成分の絶対値が最大の軸で、同じ大きさなら X、次に Y、最後に Z の順に優先する */
    uint32_t SelectVirtualShadowMapPointFace(float directionX, float directionY, float directionZ);

    /** @brief 面 face の軸の距離 axialDistance（m）での texel の一辺（m）。2 × 軸の距離 / 段の一辺 */
    float VirtualShadowMapPointTexelMeters(const VirtualShadowMapPointSettings& settings, uint32_t mip, float axialDistance);

    /**
     * @brief 受け手の段を選ぶ。texel（軸の距離 z で 2z ÷ 段の一辺）が「画素の大きさ p(d) × 2^b」以下の最も粗い段。
     *        段 0 でも超えるときは 0（段 0 より細かくは選ばない）。値が不正なら -1。
     * @param axialDistance 受け手の面の軸の向きの距離（m）
     * @param cameraDistance カメラから受け手までの距離（m）。画素の大きさ p(d) = d × 2 tan(fovY/2) / 画面の高さ
     */
    int32_t SelectVirtualShadowMapPointMip(const VirtualShadowMapPointSettings& settings,
                                           float axialDistance,
                                           float cameraDistance,
                                           float fovYDegrees,
                                           float screenHeightPixels);

    /**
     * @brief ワールドの受け手の位置から、面・段・ページを求める
     *
     * 光源から受け手までが Range を超える・光源と重なる（near 面の内側）・値が不正なら false。
     * ページの座標は面の縁・角でも [0, 1 段の一辺のページ数) に収める。
     */
    bool LocateVirtualShadowMapPointReceiver(const VirtualShadowMapPointLights& lights,
                                             uint32_t light,
                                             const Math::Vector3& receiverPosition,
                                             float cameraDistance,
                                             float fovYDegrees,
                                             float screenHeightPixels,
                                             VirtualShadowMapPointReceiver& outReceiver);
} // namespace NorvesLib::Core::Rendering
