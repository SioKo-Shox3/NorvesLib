#pragma once

// 主カメラの視錐台でスキンの描画を省く純関数。
// 保守的な判定で、面をまたぐものと境界の無いものは残す。
// LOD の選び方は後の段でこのファイルに足す。

#include "Math/GeometryTypes.h"
#include "Math/MatrixUtils.h"

namespace NorvesLib::Core::Rendering
{
    struct DrawCommand;

    struct SkinnedViewFrustum
    {
        Math::ClipSpaceFrustumPlanes Planes;
        bool bValid = false;
    };

    // CameraViewConstants::BuildForDevice(...).ViewProjectionMatrix を渡す。
    SkinnedViewFrustum MakeSkinnedViewFrustum(const Math::Matrix4x4& viewProjection);

    bool ComputeSkinnedWorldBounds(const Math::AABB& localBounds, const Math::Matrix4x4& worldMatrix,
        Math::AABB& outWorldBounds);

    bool IsSkinnedBoundsInView(const Math::AABB& localBounds, const Math::Matrix4x4& worldMatrix,
        const SkinnedViewFrustum& frustum);

    bool IsSkinnedDrawInView(const DrawCommand& command, const SkinnedViewFrustum& frustum);
} // namespace NorvesLib::Core::Rendering
