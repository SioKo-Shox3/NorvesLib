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

} // namespace NorvesLib::Core::Rendering
