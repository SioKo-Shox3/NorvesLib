// Assets/Shaders/Particles/ParticleCommon.glsl と同じ並び（std430）。変えるときは両方を同時に変える。
#pragma once

#include "Particle/VfxEffectTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 粒が生存している旗。
    inline constexpr uint32_t kGpuParticleFlagAlive = 1u << 0u;
    /// 粒が衝突面に留まっている旗。
    inline constexpr uint32_t kGpuParticleFlagStuck = 1u << 1u;
    /// 粒が有効化されている旗。
    inline constexpr uint32_t kGpuParticleFlagActive = 1u << 2u;
    /// 子エミッタが無いことを示す配列添字。
    inline constexpr uint32_t kGpuParticleInvalidEmitter = 0xFFFFFFFFu;
    /// 初期位置の形を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagShape = 1u << 0u;
    /// 抗力を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagDrag = 1u << 1u;
    /// 風を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagWind = 1u << 2u;
    /// 乱れを使う旗。
    inline constexpr uint32_t kGpuEmitterFlagTurbulence = 1u << 3u;
    /// 粒の有効化を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagActivation = 1u << 4u;
    /// 平面衝突を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCollisionPlane = 1u << 5u;
    /// 高さ場衝突を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCollisionHeightField = 1u << 6u;
    /// 深度衝突を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCollisionDepth = 1u << 7u;
    /// 大きさの寿命曲線を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCurveSize = 1u << 8u;
    /// 色の寿命曲線を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCurveColor = 1u << 9u;
    /// 不透明度の寿命曲線を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCurveOpacity = 1u << 10u;
    /// 伸びの寿命曲線を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagCurveStretch = 1u << 11u;
    /// カメラの箱での折り返しを使う旗。
    inline constexpr uint32_t kGpuEmitterFlagWrapInCameraBox = 1u << 12u;
    /// 時間倍率の影響を受けない時間を使う旗。
    inline constexpr uint32_t kGpuEmitterFlagUnscaledTime = 1u << 13u;
    /// Flags 内の VfxSpawnShape の開始ビット。
    inline constexpr uint32_t kGpuEmitterShapeShift = 16u;
    /// シフト前の VfxSpawnShape のマスク。
    inline constexpr uint32_t kGpuEmitterShapeMask = 0x7u;
    /// スキンの表面から放出する形。
    inline constexpr uint32_t kGpuEmitterShapeSkinSurface = 5u;
    /// 骨のバインド位置から放出する形。
    inline constexpr uint32_t kGpuEmitterShapeBone = 6u;
    /// 表面の標本を一様に受け入れる方式。
    inline constexpr uint32_t kGpuSkinAcceptUniform = 0u;
    /// ディゾルブの境目で標本を受け入れる方式。
    inline constexpr uint32_t kGpuSkinAcceptDissolveEdge = 1u;
    /// 指定した骨の組で標本を受け入れる方式。
    inline constexpr uint32_t kGpuSkinAcceptBoneMask = 2u;
    /// 生成スレッドごとの標本の試行上限。
    inline constexpr uint32_t kGpuSkinMaxAttempts = 8u;
    /// このフレームにスキンの源が無い印。
    inline constexpr uint32_t kGpuSkinSourceNone = 0xFFFFFFFFu;
    /// 単独の骨の指定が無い印。
    inline constexpr uint32_t kGpuSkinNoBone = 0xFFFFFFFFu;
    /// 骨の組から一様に選ぶ乱数の番号。
    inline constexpr uint32_t kGpuSkinRandomDrawBonePick = 8u;
    /// 試行 k の標本は 16+2k、受け入れは 17+2k（k < 8、最大 31）。
    inline constexpr uint32_t kGpuSkinRandomDrawAttemptBase = 16u;
    /// RenderFlags 内の VfxGpuSpriteOrientation の開始ビット。
    inline constexpr uint32_t kGpuEmitterRenderOrientationShift = 0u;
    /// シフト前の VfxGpuSpriteOrientation のマスク。
    inline constexpr uint32_t kGpuEmitterRenderOrientationMask = 0x3u;
    /// RenderFlags 内の VfxGpuSpriteBlend の開始ビット。
    inline constexpr uint32_t kGpuEmitterRenderBlendShift = 2u;
    /// シフト前の VfxGpuSpriteBlend のマスク。
    inline constexpr uint32_t kGpuEmitterRenderBlendMask = 0x1u;
    /// RenderFlags 内の VfxGpuSpritePass の開始ビット。
    inline constexpr uint32_t kGpuEmitterRenderPassShift = 3u;
    /// シフト前の VfxGpuSpritePass のマスク。
    inline constexpr uint32_t kGpuEmitterRenderPassMask = 0x1u;
    /// RenderFlags 内の VfxGpuSpriteLighting の開始ビット。
    inline constexpr uint32_t kGpuEmitterRenderLightingShift = 4u;
    /// シフト前の VfxGpuSpriteLighting のマスク。
    inline constexpr uint32_t kGpuEmitterRenderLightingMask = 0x1u;
    /// RenderFlags 内の 幅 1px 未満の補償 の開始ビット。
    inline constexpr uint32_t kGpuEmitterRenderSubPixelCompensationShift = 5u;
    /// シフト前の 幅 1px 未満の補償 のマスク。
    inline constexpr uint32_t kGpuEmitterRenderSubPixelCompensationMask = 0x1u;
    /// RenderFlags 内のスロットの順に描く設定の開始ビット。
    /// 1 は DrawInstanced(6, Capacity, 0, PoolOffset) で詰めずにスロットの順に描き、0（既定）は生きている粒を詰めた列から間接描画する。
    /// 詰めは atomic の順なので描く順が毎フレーム変わりうる。premultiplied で重なりの多いエミッタはスロットの順を選ぶ。
    inline constexpr uint32_t kGpuEmitterRenderSlotOrderShift = 6u;
    /// シフト前のスロットの順に描く設定のマスク。
    inline constexpr uint32_t kGpuEmitterRenderSlotOrderMask = 0x1u;
    /// CurveKeyCounts 内の Size のキー数の開始ビット。
    inline constexpr uint32_t kGpuCurveKeyCountSizeShift = 0u;
    /// CurveKeyCounts 内の Color のキー数の開始ビット。
    inline constexpr uint32_t kGpuCurveKeyCountColorShift = 8u;
    /// CurveKeyCounts 内の Opacity のキー数の開始ビット。
    inline constexpr uint32_t kGpuCurveKeyCountOpacityShift = 16u;
    /// CurveKeyCounts 内の Stretch のキー数の開始ビット。
    inline constexpr uint32_t kGpuCurveKeyCountStretchShift = 24u;
    /// シフト前の曲線キー数のマスク。0 は曲線なし。
    inline constexpr uint32_t kGpuCurveKeyCountMask = 0xFFu;

    /// GPU の粒。xyz は位置・速度、w は年齢・寿命。Meta[0..2] は通し番号・エミッタのスロット・状態の旗。
    /// Meta[3] は命中の法線（八面体 15+15 bit、bit 31 が法線あり。0 は法線なし。GLSL の GpuParticlePackNormal）。
    struct alignas(16) GpuParticle
    {
        float PosAge[4];
        float VelLife[4];
        uint32_t Meta[4];
    };

    /// 衝突応答。ChildEmitter は GpuEmitterParams の配列添字で、無しは kGpuParticleInvalidEmitter。
    struct GpuCollisionParams
    {
        uint32_t Response; // Particle::VfxCollisionResponse の値。
        float Restitution;
        uint32_t ChildEmitter;
        float ChildProbability;
    };

    /// エミッタの GPU 定数。std430 の 16 バイト行を 19 行で構成する。
    /// CurveKeyCounts は Size・Color・Opacity・Stretch のキー数を下位から各 8 ビットに詰める（0 は曲線なし）。
    /// 別バッファのキーは CurveKeyOffset から同じ Target 順に連続して置く。
    /// RenderFlags は bit 0〜1 が向き、2 が合成、3 が描画段階、4 が照明、5 が幅 1px 未満の補償、6 がスロットの順に描く設定。
    struct alignas(16) GpuEmitterParams
    {
        uint32_t PoolOffset, Capacity, Seed, Flags;
        uint32_t SerialBase, SpawnCount, CurveKeyOffset, CurveKeyCounts;
        float Position[3], Lifetime;
        float Radius, ConeAngleRadians, ConeLength, DepthThickness;
        float BoxHalfExtents[3], NearFadeDistance; // CameraBox は（半幅, 高さ/2, 半幅）。
        float VelocityMin[3], Drag;
        float VelocityMax[3], WindCoupling;
        float Acceleration[3], WindScale;
        float Color[4];
        float Size[2], TurbulenceStrength, TurbulenceFrequency;
        float ActivationFraction, ActivationRamp, GroundY, KillBelowY;
        GpuCollisionParams PlaneCollision;
        GpuCollisionParams HeightFieldCollision;
        GpuCollisionParams DepthCollision;
        uint32_t RenderFlags;
        float SoftDistance, FogInfluence, StreakShutterSeconds;
        float CurveColor[4]; // CurveOverLife の Target が Color のときの到達色。
        float SkinBindPosition[3]; // Bone のバインド姿勢での位置（メッシュ空間）。
        uint32_t SkinBoneIndex; // 骨の番号。kGpuSkinNoBone なら組から粒ごとに一様に選ぶ。
        float SkinNormalSpeed; // 法線の向きの初速。
        float SkinVelocityInheritance; // 体の速度を継承する係数。
        uint32_t SkinAcceptMode; // 表面の標本を受け入れる方式。
        uint32_t SkinSourceIndex; // パスが毎フレーム書く源の行の番号。無しは kGpuSkinSourceNone。
        uint32_t SkinBoneMask[4]; // 表面の受け入れと Bone の選択に使う骨の組（128 bit）。
    };

    /// 正規化した年齢と値を組にした寿命曲線のキー。
    struct GpuCurveKey
    {
        float Time;
        float Value;
    };

    /// 生成要求。OutputOffset は生成数の排他的 prefix sum。
    struct GpuSpawnJob
    {
        uint32_t EmitterIndex, Count, FirstSerial, OutputOffset;
    };

    /// 通し番号からプール内のリングのスロットを返す。capacity は 1 以上が前提で、0 の場合は poolOffset を返す。
    inline constexpr uint32_t GpuParticleRingSlot(uint32_t poolOffset, uint32_t capacity, uint32_t serial)
    {
        return capacity == 0u ? poolOffset : poolOffset + serial % capacity;
    }

    static_assert(sizeof(float) == 4u && sizeof(uint32_t) == 4u);
    static_assert(sizeof(GpuParticle) == 48u);
    static_assert(alignof(GpuParticle) == 16u);
    static_assert(offsetof(GpuParticle, PosAge) == 0u);
    static_assert(offsetof(GpuParticle, VelLife) == 16u);
    static_assert(offsetof(GpuParticle, Meta) == 32u);
    static_assert(sizeof(GpuCollisionParams) == 16u);
    static_assert(offsetof(GpuCollisionParams, Response) == 0u);
    static_assert(offsetof(GpuCollisionParams, Restitution) == 4u);
    static_assert(offsetof(GpuCollisionParams, ChildEmitter) == 8u);
    static_assert(offsetof(GpuCollisionParams, ChildProbability) == 12u);
    static_assert(sizeof(GpuEmitterParams) == 304u);
    static_assert(alignof(GpuEmitterParams) == 16u);
    static_assert(offsetof(GpuEmitterParams, PoolOffset) == 0u);
    static_assert(offsetof(GpuEmitterParams, Capacity) == 4u);
    static_assert(offsetof(GpuEmitterParams, Seed) == 8u);
    static_assert(offsetof(GpuEmitterParams, Flags) == 12u);
    static_assert(offsetof(GpuEmitterParams, SerialBase) == 16u);
    static_assert(offsetof(GpuEmitterParams, SpawnCount) == 20u);
    static_assert(offsetof(GpuEmitterParams, CurveKeyOffset) == 24u);
    static_assert(offsetof(GpuEmitterParams, CurveKeyCounts) == 28u);
    static_assert(offsetof(GpuEmitterParams, Position) == 32u);
    static_assert(offsetof(GpuEmitterParams, Lifetime) == 44u);
    static_assert(offsetof(GpuEmitterParams, Radius) == 48u);
    static_assert(offsetof(GpuEmitterParams, ConeAngleRadians) == 52u);
    static_assert(offsetof(GpuEmitterParams, ConeLength) == 56u);
    static_assert(offsetof(GpuEmitterParams, DepthThickness) == 60u);
    static_assert(offsetof(GpuEmitterParams, BoxHalfExtents) == 64u);
    static_assert(offsetof(GpuEmitterParams, NearFadeDistance) == 76u);
    static_assert(offsetof(GpuEmitterParams, VelocityMin) == 80u);
    static_assert(offsetof(GpuEmitterParams, Drag) == 92u);
    static_assert(offsetof(GpuEmitterParams, VelocityMax) == 96u);
    static_assert(offsetof(GpuEmitterParams, WindCoupling) == 108u);
    static_assert(offsetof(GpuEmitterParams, Acceleration) == 112u);
    static_assert(offsetof(GpuEmitterParams, WindScale) == 124u);
    static_assert(offsetof(GpuEmitterParams, Color) == 128u);
    static_assert(offsetof(GpuEmitterParams, Size) == 144u);
    static_assert(offsetof(GpuEmitterParams, TurbulenceStrength) == 152u);
    static_assert(offsetof(GpuEmitterParams, TurbulenceFrequency) == 156u);
    static_assert(offsetof(GpuEmitterParams, ActivationFraction) == 160u);
    static_assert(offsetof(GpuEmitterParams, ActivationRamp) == 164u);
    static_assert(offsetof(GpuEmitterParams, GroundY) == 168u);
    static_assert(offsetof(GpuEmitterParams, KillBelowY) == 172u);
    static_assert(offsetof(GpuEmitterParams, PlaneCollision) == 176u);
    static_assert(offsetof(GpuEmitterParams, HeightFieldCollision) == 192u);
    static_assert(offsetof(GpuEmitterParams, DepthCollision) == 208u);
    static_assert(offsetof(GpuEmitterParams, RenderFlags) == 224u);
    static_assert(offsetof(GpuEmitterParams, SoftDistance) == 228u);
    static_assert(offsetof(GpuEmitterParams, FogInfluence) == 232u);
    static_assert(offsetof(GpuEmitterParams, StreakShutterSeconds) == 236u);
    static_assert(offsetof(GpuEmitterParams, CurveColor) == 240u);
    static_assert(offsetof(GpuEmitterParams, SkinBindPosition) == 256u);
    static_assert(offsetof(GpuEmitterParams, SkinBoneIndex) == 268u);
    static_assert(offsetof(GpuEmitterParams, SkinNormalSpeed) == 272u);
    static_assert(offsetof(GpuEmitterParams, SkinVelocityInheritance) == 276u);
    static_assert(offsetof(GpuEmitterParams, SkinAcceptMode) == 280u);
    static_assert(offsetof(GpuEmitterParams, SkinSourceIndex) == 284u);
    static_assert(offsetof(GpuEmitterParams, SkinBoneMask) == 288u);
    static_assert(sizeof(GpuCurveKey) == 8u);
    static_assert(offsetof(GpuCurveKey, Time) == 0u);
    static_assert(offsetof(GpuCurveKey, Value) == 4u);
    static_assert(sizeof(GpuSpawnJob) == 16u);
    static_assert(offsetof(GpuSpawnJob, EmitterIndex) == 0u);
    static_assert(offsetof(GpuSpawnJob, Count) == 4u);
    static_assert(offsetof(GpuSpawnJob, FirstSerial) == 8u);
    static_assert(offsetof(GpuSpawnJob, OutputOffset) == 12u);

    // 効果の列挙値を GPU に詰める値と結び付け、値の変更を検出する。
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::Point) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::Sphere) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::Cone) == 2u);
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::Box) == 3u);
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::CameraBox) == 4u);
    static_assert(static_cast<uint32_t>(Particle::VfxCollisionResponse::Kill) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxCollisionResponse::Stick) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxCollisionResponse::Bounce) == 2u);
    static_assert(static_cast<uint32_t>(Particle::VfxCollisionResponse::SpawnChild) == 3u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteOrientation::Billboard) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteOrientation::VelocityStretch) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteOrientation::SurfaceAligned) == 2u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteBlend::PremultipliedAlpha) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteBlend::Additive) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpritePass::PreTAA) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpritePass::PostTAA) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteLighting::Unlit) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteLighting::AmbientTinted) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxCurveTarget::Size) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxCurveTarget::Color) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxCurveTarget::Opacity) == 2u);
    static_assert(static_cast<uint32_t>(Particle::VfxCurveTarget::Stretch) == 3u);
    static_assert(static_cast<uint32_t>(Particle::VfxTimeChannel::Particle) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxTimeChannel::Unscaled) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxWrapPolicy::Die) == 0u);
    static_assert(static_cast<uint32_t>(Particle::VfxWrapPolicy::WrapInCameraBox) == 1u);
    static_assert(static_cast<uint32_t>(Particle::VfxSpawnShape::CameraBox) <= kGpuEmitterShapeMask);
    static_assert(kGpuEmitterShapeSkinSurface <= kGpuEmitterShapeMask);
    static_assert(kGpuEmitterShapeBone <= kGpuEmitterShapeMask);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteOrientation::SurfaceAligned) <= kGpuEmitterRenderOrientationMask);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteBlend::Additive) <= kGpuEmitterRenderBlendMask);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpritePass::PostTAA) <= kGpuEmitterRenderPassMask);
    static_assert(static_cast<uint32_t>(Particle::VfxGpuSpriteLighting::AmbientTinted) <= kGpuEmitterRenderLightingMask);
    static_assert(Particle::kVfxMaxCurveKeys <= kGpuCurveKeyCountMask);
    static_assert(kGpuCurveKeyCountSizeShift == 8u * static_cast<uint32_t>(Particle::VfxCurveTarget::Size));
    static_assert(kGpuCurveKeyCountColorShift == 8u * static_cast<uint32_t>(Particle::VfxCurveTarget::Color));
    static_assert(kGpuCurveKeyCountOpacityShift == 8u * static_cast<uint32_t>(Particle::VfxCurveTarget::Opacity));
    static_assert(kGpuCurveKeyCountStretchShift == 8u * static_cast<uint32_t>(Particle::VfxCurveTarget::Stretch));
} // namespace NorvesLib::Core::Rendering
