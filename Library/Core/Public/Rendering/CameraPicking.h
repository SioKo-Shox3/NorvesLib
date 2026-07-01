#pragma once

#include "Rendering/SceneProxy.h"
#include "Math/GeometryTypes.h"

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief カメラ情報からクライアント座標のピッキングレイを構築します。
     *
     * screenX/screenY はクライアント上の pixel 座標で、camera.Viewport.X/Y を基準に
     * ビューポート内へローカル化されます。Viewport 外または無効な Viewport では false を返します。
     * Perspective/Orthographic の両方に対応し、RHI や Scene には依存しません。
     */
    bool BuildPickingRay(const CameraProxy& camera, float screenX, float screenY, Math::Ray& outRay);

    /**
     * @brief 画面矩形が囲むワールド空間の視錐台を構築します。
     *
     * 画面矩形 (x0, y0)-(x1, y1) が囲む視錐台を構築します。Perspective 限定で、
     * Orthographic では false を返します。Viewport 外の矩形は内側へクランプします。
     * 縮退した矩形、near <= 0、far <= near、法線縮退では false を返します。
     * 入力座標は反転していても正規化して扱います。
     */
    bool BuildScreenRectFrustum(
        const CameraProxy& camera,
        float x0,
        float y0,
        float x1,
        float y1,
        Math::Frustum& outFrustum);

    /**
     * @brief 中心点と外周点からワールド空間の選択球を構築します。
     *
     * centerX/centerY のピッキングレイ上で centerAlongRayDistance にある点を球中心とし、
     * edgeX/edgeY のピッキングレイとカメラ forward に垂直な中心平面との交点までを半径にします。
     * 入力が非有限、Viewport 外、距離が正でない、平面交差不能、半径縮退の場合は false を返します。
     * false を返す場合 outSphere は変更しません。
     */
    bool BuildSelectionSphere(
        const CameraProxy& camera,
        float centerX,
        float centerY,
        float edgeX,
        float edgeY,
        float centerAlongRayDistance,
        Math::Sphere& outSphere);

} // namespace NorvesLib::Core::Rendering
