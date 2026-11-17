// GPU の粒の描画パスとシェーダーが共有する資源名・結合番号・定数を定義する。
// Assets/Shaders/Particles/ParticleDraw.glsl と同じ並び（std140/std430）。変えるときは両方を同時に変える。
#pragma once

#include "Rendering/GpuParticle/GpuParticleFrame.h"
#include "Rendering/GpuParticle/GpuParticleTypes.h"
#include "Rendering/HeightFogGpuParams.h"
#include "RHI/IndirectCommands.h"
#include "Text/IdentityPool.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// シミュレーションのパスが公開し、描画のパスが読む粒のプール。
    inline constexpr Identity kGpuParticlePoolResourceName = Identity::Literal("GpuParticle.Pool", sizeof("GpuParticle.Pool") - 1);

    /// シミュレーションのパスが公開し、描画のパスが読む、生きている粒のスロットをバケットごとに詰めた uint の列。
    /// グラフの入口と出口の状態は UnorderedAccess。
    inline constexpr Identity kGpuParticleDrawListResourceName = Identity::Literal("GpuParticle.DrawList", sizeof("GpuParticle.DrawList") - 1);
    /// バケットごとの間接描画の引数。GpuParticleDrawArgs の並びで、グラフの入口と出口の状態は UnorderedAccess。
    inline constexpr Identity kGpuParticleDrawArgsResourceName = Identity::Literal("GpuParticle.DrawArgs", sizeof("GpuParticle.DrawArgs") - 1);

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
    /// 生きている粒を詰めたスロットの列は頂点で読む。
    inline constexpr uint32_t kGpuParticleDrawBindingDrawList = 8u;
    inline constexpr uint32_t kGpuParticleDrawBindingCount = 9u;

    /// 描画の段と出力の方式。
    inline constexpr uint32_t kGpuParticleDrawPassPreTAA = 0u;
    inline constexpr uint32_t kGpuParticleDrawPassPostTAA = 1u;
    inline constexpr uint32_t kGpuParticleDrawOutputColor = 0u;
    inline constexpr uint32_t kGpuParticleDrawOutputReactive = 1u;

    /// 描画段階と合成方式の組み合わせごとのバケット数。
    inline constexpr uint32_t kGpuParticleDrawBucketCount = 4u;
    /// drawPass は PreTAA/PostTAA、blend は 0 が premultiplied、1 が加算。
    inline constexpr uint32_t GpuParticleDrawBucket(uint32_t drawPass, uint32_t blend)
    {
        return drawPass * 2u + blend;
    }
    /// 詰めたスロットの列の要素数。バケット b の先頭は b * kGpuParticlePoolCapacity。
    inline constexpr uint32_t kGpuParticleDrawListCapacity = kGpuParticleDrawBucketCount * kGpuParticlePoolCapacity;

    /// 間接描画の引数と詰めの数え。std430 で 96 バイト。
    struct GpuParticleDrawArgs
    {
        RHI::DrawIndirectCommand Commands[kGpuParticleDrawBucketCount]; // 0: 各バケットの {6, 生存数, 0, 0}。
        uint32_t DrawCounts[kGpuParticleDrawBucketCount];              // 64: DrawIndirectCount の描画数。生存数が 0 なら 0、それ以外は 1。
        uint32_t AliveCounters[kGpuParticleDrawBucketCount];          // 80: atomicAdd の数え。詰めの前に 0 にする。
    };
    static_assert(sizeof(RHI::DrawIndirectCommand) == 16u);
    static_assert(sizeof(GpuParticleDrawArgs) == 96u);
    static_assert(offsetof(GpuParticleDrawArgs, Commands) == 0u);
    static_assert(offsetof(GpuParticleDrawArgs, DrawCounts) == 64u);
    static_assert(offsetof(GpuParticleDrawArgs, AliveCounters) == 80u);

    /// gl_InstanceIndex をそのまま粒のスロットに使う。
    inline constexpr uint32_t kGpuParticleDrawSourceDirect = 0u;
    /// 詰めた列の bucket * kGpuParticlePoolCapacity + gl_InstanceIndex から粒のスロットを引く。
    inline constexpr uint32_t GpuParticleDrawSourceList(uint32_t bucket)
    {
        return 1u + bucket;
    }

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
        uint32_t Flags[4];                  // 256: x 描画段・y 照明の資源あり（1/0）・z 出力方式・w 粒の引き方（kGpuParticleDrawSourceDirect か GpuParticleDrawSourceList）。
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
