#pragma once

#include "Math/Vector3.h"
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief カメラと注視点の間の切り抜き、およびカメラ近くのフェード
     *
     * 座標と距離はワールド空間のメートル。対象の種類と草の低背化の設定は描画側へ渡す。
     */
    struct ViewClearanceParameters
    {
        bool bEnabled = false;
        // 0 で従来どおり、1 で切り抜きと近接のフェードをそのまま適用する。
        float Strength = 1.0f;
        Math::Vector3 CameraPosition;
        Math::Vector3 TargetPosition;
        // 半径は主人公の体の幅＋余白程度の仮の値。
        float CameraRadius = 0.3f;
        float TargetRadius = 0.6f;
        // 半径に対する切り抜きの縁の幅。
        float FeatherRatio = 0.5f;
        float NearFadeStart = 0.15f;
        float NearFadeEnd = 0.6f;
        // 草の低背化の下限。可視度の計算には使わない。
        float MinHeightScale = 0.2f;
        bool bAffectGrass = true;
        bool bAffectOccluders = true;
        bool bHeroSilhouette = false;
    };

    // std140 の vec4 4 本。Common/ViewClearance.glsl と同じ並び。
    struct alignas(16) ViewClearanceGpuData
    {
        float CameraRadius[4] = {}; // xyz: カメラ位置、w: CameraRadius
        float TargetRadius[4] = {}; // xyz: 注視点、w: TargetRadius
        float Shape[4] = {};        // x: FeatherRatio、y: NearFadeStart、z: NearFadeEnd、w: MinHeightScale
        float Control[4] = {};      // x: Strength、y: 有効なら 1、それ以外は 0。zw は予約の 0
    };
    static_assert(alignof(ViewClearanceGpuData) == 16);
    static_assert(sizeof(ViewClearanceGpuData) == 64);
    static_assert(offsetof(ViewClearanceGpuData, CameraRadius) == 0);
    static_assert(offsetof(ViewClearanceGpuData, TargetRadius) == 16);
    static_assert(offsetof(ViewClearanceGpuData, Shape) == 32);
    static_assert(offsetof(ViewClearanceGpuData, Control) == 48);

    ViewClearanceParameters MakeDefaultViewClearanceParameters();

    ViewClearanceParameters SanitizeViewClearanceParameters(
        const ViewClearanceParameters& parameters);

    /** @brief 入力を整えて GPU 用に詰める。適用しない場合は全成分を 0 にする。 */
    ViewClearanceGpuData MakeViewClearanceGpuData(const ViewClearanceParameters& parameters, bool bApply);

    /** 指定した点の可視度を返す（0 で消し、1 でそのまま）。必要に応じて入力を先に整える。 */
    float ComputeViewClearanceVisibility(const ViewClearanceParameters& parameters,
                                         const Math::Vector3& point);
}
