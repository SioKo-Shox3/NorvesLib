#pragma once

#include "Math/Vector3.h"

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

    ViewClearanceParameters MakeDefaultViewClearanceParameters();

    ViewClearanceParameters SanitizeViewClearanceParameters(
        const ViewClearanceParameters& parameters);

    /** 指定した点の可視度を返す（0 で消し、1 でそのまま）。必要に応じて入力を先に整える。 */
    float ComputeViewClearanceVisibility(const ViewClearanceParameters& parameters,
                                         const Math::Vector3& point);
}
