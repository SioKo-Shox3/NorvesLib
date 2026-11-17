// GPU の粒の描画パスとシェーダーが共有する資源名・結合番号・定数を定義する。
// Assets/Shaders/Particles/ParticleDraw.glsl と同じ並び（std140/std430）。変えるときは両方を同時に変える。
#pragma once

#include "Rendering/GpuParticle/GpuParticleFrame.h"
#include "Rendering/GpuParticle/GpuParticleTypes.h"
#include "Rendering/HeightFogGpuParams.h"
#include "Text/IdentityPool.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// シミュレーションのパスが公開し、描画のパスが読む粒のプール。
    inline constexpr Identity kGpuParticlePoolResourceName = Identity::Literal("GpuParticle.Pool", sizeof("GpuParticle.Pool") - 1);

    /// set 0 の結合番号。定数は頂点・断片の両方で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingConstants = 0u;
    /// 粒のプールは頂点で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingPool = 1u;
    /// このパスで描くエミッタの列は頂点・断片の両方で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingEmitters = 2u;
    /// 粒の Meta[1] をエミッタ列の添字へ引く表。無しは kGpuParticleInvalidEmitter。
    /// 表の要素数は kGpuParticleMaxEmitters で、頂点で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingSlotToEmitter = 3u;
    /// 寿命曲線のキーは頂点で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingCurveKeys = 4u;
    /// Scene.Depth は断片で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingSceneDepth = 5u;
    /// 拡散の放射照度の正距円筒は断片で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingDiffuseIrradiance = 6u;
    /// 高さフォグの空の放射輝度 LUT は断片で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingHeightFogSky = 7u;
    inline constexpr uint32_t kGpuParticleDrawBindingCount = 8u;

    /// 描画の段と出力の方式。
    inline constexpr uint32_t kGpuParticleDrawPassPreTAA = 0u;
    inline constexpr uint32_t kGpuParticleDrawPassPostTAA = 1u;
    inline constexpr uint32_t kGpuParticleDrawOutputColor = 0u;
    inline constexpr uint32_t kGpuParticleDrawOutputReactive = 1u;

    /// 描画拡張の名前。RenderExtensionRegistry は Ext.<Game>.<Name> の形を受け付ける。
    inline constexpr const char* kGpuParticleDrawPreTAAExtensionName = "Ext.Engine.GpuParticleDrawPreTAA";
    inline constexpr const char* kGpuParticleDrawPostTAAExtensionName = "Ext.Engine.GpuParticleDrawPostTAA";

    /// 粒の描画定数。std140 で 368 バイト。
    struct alignas(16) GpuParticleDrawConstants
    {
        float View[16];                     // 0: CameraViewConstants::CopyShaderView の並び。
        float Projection[16];               // 64: CopyShaderProjection の並び。PreTAA はジッタつき、PostTAA はジッタなし。
        float CameraPositionPreExposure[4];  // 128: xyz カメラのワールド位置・w シーン色のプリエクスポージャ。
        float CameraRight[4];               // 144: xyz カメラの右（ワールド、単位）。
        float CameraUp[4];                  // 160: xyz カメラの上（ワールド、単位）。
        float CameraForward[4];             // 176: xyz カメラの前（ワールド、単位）。
        float CameraVelocity[4];            // 192: xyz カメラの速度（m/粒の秒）。前のカメラが連続でなければ 0。
        float TargetSizeInvSize[4];         // 208: 描画先の幅・高さ（画素）とその逆数。
        float NearFarParams[4];             // 224: x ニア面・y ファー面（m）・z ニア/ファー・w 未使用。
        float AmbientColorIblIntensity[4];  // 240: rgb 予備の環境光（色×強さ）・w IBL の強さ。
        uint32_t Flags[4];                  // 256: x 描画段・y 照明の資源あり（1/0）・z 出力方式・w 予約 0。
        GPUHeightFogParams HeightFog;       // 272: 高さフォグ。
    };

    static_assert(sizeof(GPUHeightFogParams) == 96u);
    static_assert(sizeof(GpuParticleDrawConstants) == 368u);
    static_assert(alignof(GpuParticleDrawConstants) == 16u);
    static_assert(offsetof(GpuParticleDrawConstants, View) == 0u);
    static_assert(offsetof(GpuParticleDrawConstants, Projection) == 64u);
    static_assert(offsetof(GpuParticleDrawConstants, CameraPositionPreExposure) == 128u);
    static_assert(offsetof(GpuParticleDrawConstants, CameraRight) == 144u);
    static_assert(offsetof(GpuParticleDrawConstants, CameraUp) == 160u);
    static_assert(offsetof(GpuParticleDrawConstants, CameraForward) == 176u);
    static_assert(offsetof(GpuParticleDrawConstants, CameraVelocity) == 192u);
    static_assert(offsetof(GpuParticleDrawConstants, TargetSizeInvSize) == 208u);
    static_assert(offsetof(GpuParticleDrawConstants, NearFarParams) == 224u);
    static_assert(offsetof(GpuParticleDrawConstants, AmbientColorIblIntensity) == 240u);
    static_assert(offsetof(GpuParticleDrawConstants, Flags) == 256u);
    static_assert(offsetof(GpuParticleDrawConstants, HeightFog) == 272u);
} // 名前空間 NorvesLib::Core::Rendering
