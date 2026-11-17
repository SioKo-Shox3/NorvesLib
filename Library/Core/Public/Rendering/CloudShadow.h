// 雲の影と作者が置く切れ間の値、および CPU 側の投影・配置の純関数。
#pragma once

#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t MaxCloudSunGaps = 4u;
    inline constexpr uint32_t CloudShadowMaskResolution = 256u; // マスクの一辺の texel 数
    inline constexpr float CloudShadowMinSunHeight = 0.02f; // 正規化した太陽の向きの y がこれ未満ならマスクを使わない

    /** @brief 雲の底の平面に置く円形の切れ間。距離 R まで重み 1、外側の幅 S で 0 へ落とす。 */
    struct CloudSunGap
    {
        float CenterX = 0.0f; // 雲の底の平面の中心（ワールドの X、m）
        float CenterZ = 0.0f; // 同じく Z（m）
        float RadiusMeters = 0.0f; // 重み 1 の半径（m）
        float SoftnessMeters = 0.0f; // 半径の外で 0 へ落ちる幅（m）。0 で縁が硬い
        float Strength = 1.0f; // 切れ間の内でマスクを 1 へ寄せる強さ（0〜1）
    };

    /**
     * @brief 雲の影の設定。既定は無効。雲の層（CloudLayerParameters）と空が有効なときだけ効く。
     *
     * マスクは霧の太陽の散乱と、選んだときの地面の太陽の直達だけに掛ける。
     * 空由来 IBL・PT・RT・DDGI には入れない。
     */
    struct CloudShadowParameters
    {
        bool bEnabled = false;
        float MaskExtentMeters = 8000.0f; // カメラに追従するマスクの一辺（m）
        uint32_t SunGapCount = 0u;
        CloudSunGap SunGaps[MaxCloudSunGaps] = {};
        bool bApplyToGroundDirect = false; // 地面の太陽の直達（lighting.frag）にも掛けるか
    };

    /** @brief 雲の底の平面で texel の格子へ丸めたマスクの置き場。投影できなければ無効。 */
    struct CloudShadowMaskPlacement
    {
        float CenterX = 0.0f; // 雲の底の平面の X（m）。texel の格子に丸めた中心
        float CenterZ = 0.0f;
        float ExtentMeters = 0.0f;
        float TexelMeters = 0.0f; // ExtentMeters / CloudShadowMaskResolution
        bool bValid = false;
    };

    /**
     * @brief 非有限値を既定へ戻し、extent を [100, 200000]、数を min(count, 4) にする。
     * 半径・幅を [0, 100000]、強さを [0, 1] に丸め、数より後ろの切れ間は既定へ戻す。
     */
    CloudShadowParameters SanitizeCloudShadowParameters(const CloudShadowParameters& parameters);

    /** @brief 全ての欄を a.field == b.field で比較する。数より後ろの切れ間も含める。 */
    bool AreCloudShadowParametersEqual(const CloudShadowParameters& a, const CloudShadowParameters& b);

    /**
     * @brief d = sqrt((x - CenterX)^2 + (z - CenterZ)^2) から切れ間の重みを求める。強さは掛けない。
     * S > 0 なら 1 - t^2(3 - 2t)、t = clamp((d - R) / ((R + S) - R), 0, 1)。
     * S = 0 なら d <= R で 1、外で 0。
     */
    float ComputeCloudSunGapWeight(const CloudSunGap& gap, float x, float z);

    /** @brief clamp(max(weight[i] * Strength[i]), 0, 1) を返す。無効・数 0 なら 0。 */
    float ComputeCloudSunGapOpenness(const CloudShadowParameters& parameters, float x, float z);

    /**
     * @brief s = normalize(sunDirection)、t = max(bottomAltitudeMeters - position.y, 0) / s.y として写す。
     * outX = position.x + s.x * t、outZ = position.z + s.z * t。
     * 向きの長さが 0・非有限、s.y < CloudShadowMinSunHeight、位置・高度・結果が非有限なら false。
     */
    bool ProjectToCloudBottomPlane(const Math::Vector3& position, const Math::Vector3& sunDirection,
                                   float bottomAltitudeMeters, float& outX, float& outZ);

    /**
     * @brief extent を [100, 200000] へ丸め、texel = extent / CloudShadowMaskResolution とする。
     * カメラを雲の底へ写し、中心を round(X / texel) * texel（Z も同様）へ丸める。投影失敗なら無効。
     */
    CloudShadowMaskPlacement ComputeCloudShadowMaskPlacement(const Math::Vector3& cameraPosition,
                                                             const Math::Vector3& sunDirection,
                                                             float bottomAltitudeMeters, float extentMeters);
} // namespace NorvesLib::Core::Rendering
