// 前向き描画で高さフォグを共有するための std140 定数と構築関数を定義する。
#pragma once

#include "Rendering/VolumetricFog.h"

#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    struct CameraProxy;
    struct ViewRenderContext;

    /// 前向きの物に高さフォグを掛けるシェーダーの定数（std140）。Common/HeightFog.glsl の struct HeightFogParams と同じ並び。
    struct alignas(16) GPUHeightFogParams
    {
        float DensityBaseHeightFalloffSkyAvailable[4]; // x=基準の高さの密度（霧が無効なら 0）, y=基準の高さ, z=高さの減衰, w=空の放射輝度 LUT が使えれば 1
        float TintAndSkyBlend[4];                      // rgb=霧の色味（今は 1）, w=空への寄せ（今は 0）
        float FallbackColorAndAmbientScale[4];         // rgb=LUT が無いときの霧の色, w=周りの光の倍率
        float CameraPositionAndPreExposure[4];         // xyz=カメラの位置, w=プリエクスポージャ
        float MaxDistanceAndReserved[4];               // x=霧を積む距離の上限（m、0 以下で視線全体）, yzw=0
    };
    static_assert(sizeof(GPUHeightFogParams) == 80u);
    static_assert(offsetof(GPUHeightFogParams, DensityBaseHeightFalloffSkyAvailable) == 0u);
    static_assert(offsetof(GPUHeightFogParams, TintAndSkyBlend) == 16u);
    static_assert(offsetof(GPUHeightFogParams, FallbackColorAndAmbientScale) == 32u);
    static_assert(offsetof(GPUHeightFogParams, CameraPositionAndPreExposure) == 48u);
    static_assert(offsetof(GPUHeightFogParams, MaxDistanceAndReserved) == 64u);

    /// スナップショットの霧を安全化し、未接続なら既定値、指定された上書きは再安全化せず使う。
    /// 無効な霧は密度を 0 にし、色とプリエクスポージャを安全化してカメラ位置・空 LUT の可用性と詰める。
    [[nodiscard]] GPUHeightFogParams BuildHeightFogGpuParams(const ViewRenderContext& context,
                                                           const CameraProxy& camera,
                                                           bool bSkyRadianceAvailable,
                                                           const VolumetricFogParameters* fogOverride = nullptr);

} // namespace NorvesLib::Core::Rendering
