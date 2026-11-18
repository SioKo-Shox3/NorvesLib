#pragma once

#include "CoreTypes.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <cstdint>

namespace NorvesLib::Core::Particle
{
    enum class VfxValueType : uint8_t
    {
        Float,
        UInt,
        Bool,
        Vector2,
        Vector3,
        Color,
        Enum,
        Name
    };

    struct VfxValue
    {
        VfxValueType Type = VfxValueType::Float;
        Math::Vector4 Vector = Math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        uint32_t UInt = 0u;
        Container::String Name;

        static VfxValue MakeFloat(float value);
        static VfxValue MakeUInt(uint32_t value);
        static VfxValue MakeBool(bool value);
        static VfxValue MakeVector2(const Math::Vector2& value);
        static VfxValue MakeVector3(const Math::Vector3& value);
        static VfxValue MakeColor(const Math::Vector4& value);
        static VfxValue MakeEnum(uint32_t value);
        static VfxValue MakeName(const char* value);

        bool operator==(const VfxValue& other) const;
    };

    enum class VfxModuleType : uint16_t
    {
        SpawnRate = 1,
        SpawnBurst = 2,
        SpawnOnEvent = 3,
        InitPosition = 10,
        InitVelocityBox = 11,
        InitLifetime = 12,
        InitColor = 13,
        InitSize = 14,
        InitShape = 15,
        Acceleration = 20,
        Drag = 21,
        WindInfluence = 22,
        Turbulence = 23,
        CurveOverLife = 24,
        ActivationFraction = 25,
        CollisionPlane = 26,
        CollisionHeightField = 27,
        CollisionDepth = 28,
        SpriteRenderer = 30,
        MeshRenderer = 31,
        GpuSpriteRenderer = 32
    };

    enum class VfxModuleStage : uint8_t
    {
        Spawn,
        Initialize,
        Update,
        Render,
        Object
    };

    enum class VfxSimulationSpace : uint8_t
    {
        Screen = 0,
        World = 1
    };

    /// @brief 効果を実行する処理先。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxExecutor : uint8_t
    {
        /// @brief CPU で実行する。
        Cpu = 0,
        /// @brief GPU で実行する。
        Gpu = 1
    };

    /// @brief 乱数の生成方式。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxRandomStream : uint8_t
    {
        /// @brief 現行の xorshift32 の列を順番に使う。
        Sequential = 0,
        /// @brief エミッタの種・粒の通し番号・引く順を PCG ハッシュへ渡す。
        Counter = 1
    };

    /// @brief 積分と生成の速さに使う時間。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxTimeChannel : uint8_t
    {
        /// @brief 粒に適用される時間を使う。
        Particle = 0,
        /// @brief 時間倍率の影響を受けない時間を使う。
        Unscaled = 1
    };

    /// @brief カメラの箱を出た粒の扱い。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxWrapPolicy : uint8_t
    {
        /// @brief 箱の外へ出た粒を消す。
        Die = 0,
        /// @brief 箱の反対側へ粒を折り返す。
        WrapInCameraBox = 1
    };

    /// @brief 初期位置の形。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxSpawnShape : uint8_t
    {
        /// @brief 一点から生成する。
        Point = 0,
        /// @brief 球の中で生成する。
        Sphere = 1,
        /// @brief 頂点から +Y へ延びる円錐の中で生成する。
        Cone = 2,
        /// @brief 箱の中で生成する。
        Box = 3,
        /// @brief カメラを中心にした箱の中で生成する。
        CameraBox = 4,
        /// @brief スキンの表面から生成する。
        SkinSurface = 5,
        /// @brief 骨のバインド位置から生成する。
        Bone = 6
    };

    /// @brief 寿命の曲線を掛ける量。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxCurveTarget : uint8_t
    {
        /// @brief 粒の大きさに掛ける。
        Size = 0,
        /// @brief 粒の色に掛ける。
        Color = 1,
        /// @brief 粒の不透明度に掛ける。
        Opacity = 2,
        /// @brief 粒の伸びに掛ける。
        Stretch = 3
    };

    /// @brief 衝突した粒の応答。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxCollisionResponse : uint8_t
    {
        /// @brief 衝突した粒を消す。
        Kill = 0,
        /// @brief 衝突面に粒を留める。
        Stick = 1,
        /// @brief 衝突面で粒を跳ね返す。
        Bounce = 2,
        /// @brief 指定した子エミッタから粒を生成する。
        SpawnChild = 3,
        /// @brief 初期位置の形が CameraBox のとき、着いた粒をカメラの箱の上面へ戻して速度を保つ。
        /// 着いた面が上面以上なら衝突を無視する。CameraBox 以外では Kill と同じ。
        ReturnToCameraBoxTop = 4
    };

    /// @brief GPU スプライトの向き。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxGpuSpriteOrientation : uint8_t
    {
        /// @brief カメラの方へ向ける。
        Billboard = 0,
        /// @brief 速度の向きに伸ばす。
        VelocityStretch = 1,
        /// @brief 表面に沿わせる。
        SurfaceAligned = 2
    };

    /// @brief GPU スプライトの合成方式。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxGpuSpriteBlend : uint8_t
    {
        /// @brief 乗算済みアルファで合成する。
        PremultipliedAlpha = 0,
        /// @brief 加算で合成する。
        Additive = 1
    };

    /// @brief GPU スプライトの描画段階。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxGpuSpritePass : uint8_t
    {
        /// @brief 時間方向のアンチエイリアスの前に描画する。
        PreTAA = 0,
        /// @brief 時間方向のアンチエイリアスの後に描画する。
        PostTAA = 1
    };

    /// @brief GPU スプライトの照明方式。値は VfxValue::UInt の列挙添字と一致する。
    enum class VfxGpuSpriteLighting : uint8_t
    {
        /// @brief 照明の影響を受けない。
        Unlit = 0,
        /// @brief 環境光の色を反映する。
        AmbientTinted = 1
    };

    /// @brief 寿命の曲線が保持できるキーの最大数。
    inline constexpr uint32_t kVfxMaxCurveKeys = 8u;

    struct VfxModule
    {
        VfxModuleType Type = VfxModuleType::SpawnRate;
        VariableArray<VfxValue> Values;
    };

    struct VfxEmitterAsset
    {
        Container::String Name;
        VariableArray<VfxValue> Properties;
        VariableArray<VfxModule> Modules;
    };

    struct VfxFlipbookImportSettings
    {
        Container::String Name;
        Container::String Texture;
        uint32_t Columns = 1u;
        uint32_t Rows = 1u;
        uint32_t FrameCount = 1u;
        float FramesPerSecond = 0.0f;
        uint32_t StartFrame = 0u;
        bool bLoop = true;
    };

    struct VfxEffectAsset
    {
        Container::String Name;
        VariableArray<VfxEmitterAsset> Emitters;
        VariableArray<VfxFlipbookImportSettings> Flipbooks;
    };
} // namespace NorvesLib::Core::Particle
