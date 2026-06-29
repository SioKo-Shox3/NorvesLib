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

} // namespace NorvesLib::Core::Rendering
