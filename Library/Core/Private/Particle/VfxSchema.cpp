#include "Particle/VfxSchema.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        template <typename T, size_t N>
        constexpr uint32_t CountOf(const T (&)[N])
        {
            return static_cast<uint32_t>(N);
        }

        constexpr VfxParameterSchema VectorParameter(const char* name, VfxValueType type, double min, double max,
            const char* unit, float x, float y, float z, float w, const char* description)
        {
            return {name, type, min, max, unit, {x, y, z, w}, 0u, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema UIntParameter(const char* name, uint32_t min, uint32_t max,
            const char* unit, uint32_t value, const char* description)
        {
            return {name, VfxValueType::UInt, static_cast<double>(min), static_cast<double>(max), unit,
                {0.0f, 0.0f, 0.0f, 0.0f}, value, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema BoolParameter(const char* name, bool value, const char* description)
        {
            return {name, VfxValueType::Bool, 0.0, 1.0, "", {0.0f, 0.0f, 0.0f, 0.0f},
                value ? 1u : 0u, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema EnumParameter(const char* name, const char* const* names,
            uint32_t count, uint32_t value, const char* description)
        {
            return {name, VfxValueType::Enum, 0.0, static_cast<double>(count - 1u), "",
                {0.0f, 0.0f, 0.0f, 0.0f}, value, "", names, count, description};
        }

        constexpr VfxParameterSchema NameParameter(const char* name, uint32_t min, uint32_t max,
            const char* value, const char* description)
        {
            return {name, VfxValueType::Name, static_cast<double>(min), static_cast<double>(max), "",
                {0.0f, 0.0f, 0.0f, 0.0f}, 0u, value, nullptr, 0u, description};
        }

        constexpr const char* kSpaceNames[] = {"Screen", "World"};
        constexpr const char* kLayerNames[] = {"Default", "Transparent", "UI"};
        constexpr const char* kBlendNames[] = {"Opaque", "Masked", "Translucent", "Additive", "Modulate"};

        constexpr const char* kExecutorNames[] = {"Cpu", "Gpu"};
        constexpr const char* kRandomStreamNames[] = {"Sequential", "Counter"};
        constexpr const char* kTimeChannelNames[] = {"Particle", "Unscaled"};
        constexpr const char* kWrapPolicyNames[] = {"Die", "WrapInCameraBox"};
        constexpr const char* kSpawnShapeNames[] = {"Point", "Sphere", "Cone", "Box", "CameraBox"};
        constexpr const char* kCurveTargetNames[] = {"Size", "Color", "Opacity", "Stretch"};
        constexpr const char* kCollisionResponseNames[] = {
            "Kill", "Stick", "Bounce", "SpawnChild", "ReturnToCameraBoxTop"};
        constexpr const char* kGpuSpriteOrientationNames[] = {"Billboard", "VelocityStretch", "SurfaceAligned"};
        constexpr const char* kGpuSpriteBlendNames[] = {"PremultipliedAlpha", "Additive"};
        constexpr const char* kGpuSpritePassNames[] = {"PreTAA", "PostTAA"};
        constexpr const char* kGpuSpriteLightingNames[] = {"Unlit", "AmbientTinted"};

        constexpr VfxParameterSchema kEmitterParameters[] = {
            UIntParameter("MaxParticles", 1u, 65536u, "count", 128u, "粒の最大数"),
            EnumParameter("Space", kSpaceNames, CountOf(kSpaceNames), 0u, "シミュレーション空間"),
            UIntParameter("Seed", 0u, 4294967295u, "", 0u, "エミッタの乱数種"),
            EnumParameter("Executor", kExecutorNames, CountOf(kExecutorNames), 0u, "実行先"),
            EnumParameter("RandomStream", kRandomStreamNames, CountOf(kRandomStreamNames), 0u, "乱数の流れ"),
            UIntParameter("Capacity", 0u, 16777216u, "count", 0u, "粒のプールの容量。0 は MaxParticles を使う"),
            EnumParameter("TimeChannel", kTimeChannelNames, CountOf(kTimeChannelNames), 0u, "積分と生成の速さに使う時間"),
            EnumParameter("WrapPolicy", kWrapPolicyNames, CountOf(kWrapPolicyNames), 0u, "カメラの箱を出た粒の扱い")
        };
        constexpr VfxParameterSchema kFlipbookParameters[] = {
            UIntParameter("Columns", 1u, 64u, "count", 1u, "列数"),
            UIntParameter("Rows", 1u, 64u, "count", 1u, "行数"),
            UIntParameter("FrameCount", 1u, 4096u, "count", 1u, "フレーム数"),
            VectorParameter("FramesPerSecond", VfxValueType::Float, 0.0, 240.0, "1/s", 0.0f, 0.0f, 0.0f, 0.0f, "毎秒のフレーム数"),
            UIntParameter("StartFrame", 0u, 4095u, "count", 0u, "開始フレーム"),
            BoolParameter("Loop", true, "繰り返し再生")
        };
        constexpr VfxParameterSchema kSpawnRateParameters[] = {
            VectorParameter("Rate", VfxValueType::Float, 0.0, 100000.0, "1/s", 10.0f, 0.0f, 0.0f, 0.0f, "毎秒の生成数")
        };
        constexpr VfxParameterSchema kSpawnBurstParameters[] = {
            UIntParameter("Count", 0u, 65536u, "count", 10u, "一度に生成する粒の数"),
            VectorParameter("Time", VfxValueType::Float, 0.0, 3600.0, "s", 0.0f, 0.0f, 0.0f, 0.0f, "生成時刻")
        };
        constexpr VfxParameterSchema kSpawnOnEventParameters[] = {
            NameParameter("EventName", 1u, 63u, "Event", "生成の契機となるイベント名"),
            UIntParameter("Count", 1u, 65536u, "count", 1u, "イベントごとの生成数")
        };
        constexpr VfxParameterSchema kInitPositionParameters[] = {
            VectorParameter("Offset", VfxValueType::Vector3, -1e6, 1e6, "space", 0.0f, 0.0f, 0.0f, 0.0f, "初期位置のオフセット")
        };
        constexpr VfxParameterSchema kInitVelocityBoxParameters[] = {
            VectorParameter("Min", VfxValueType::Vector3, -1e5, 1e5, "space/s", 0.0f, 0.0f, 0.0f, 0.0f, "初速度の下限"),
            VectorParameter("Max", VfxValueType::Vector3, -1e5, 1e5, "space/s", 0.0f, 0.0f, 0.0f, 0.0f, "初速度の上限")
        };
        constexpr VfxParameterSchema kInitLifetimeParameters[] = {
            VectorParameter("Lifetime", VfxValueType::Float, 0.001, 3600.0, "s", 1.0f, 0.0f, 0.0f, 0.0f, "粒の寿命")
        };
        constexpr VfxParameterSchema kInitColorParameters[] = {
            VectorParameter("Color", VfxValueType::Color, 0.0, 64.0, "linear_rgba", 1.0f, 1.0f, 1.0f, 1.0f, "粒の初期色")
        };
        constexpr VfxParameterSchema kInitSizeParameters[] = {
            VectorParameter("Size", VfxValueType::Vector2, 0.0001, 100000.0, "space", 8.0f, 8.0f, 0.0f, 0.0f, "粒の初期サイズ")
        };
        constexpr VfxParameterSchema kAccelerationParameters[] = {
            VectorParameter("Acceleration", VfxValueType::Vector3, -1e5, 1e5, "space/s^2", 0.0f, 0.0f, 0.0f, 0.0f, "粒に加える加速度")
        };
        constexpr VfxParameterSchema kSpriteRendererParameters[] = {
            EnumParameter("Layer", kLayerNames, CountOf(kLayerNames), 2u, "描画レイヤー"),
            EnumParameter("Blend", kBlendNames, CountOf(kBlendNames), 2u, "ブレンド方式"),
            UIntParameter("SortKey", 0u, 4294967295u, "", 0u, "描画順のキー"),
            NameParameter("Flipbook", 0u, 63u, "", "フリップブック名")
        };

        constexpr VfxParameterSchema kMeshRendererParameters[] = {
            NameParameter("Mesh", 0u, 63u, "", "メッシュの資産名"),
            BoolParameter("AlignToVelocity", false, "局所の +Z を速度へ向ける"),
            VectorParameter("DissolveStart", VfxValueType::Float, 0.0, 1.0, "life", 0.5f, 0.0f, 0.0f, 0.0f, "溶け始める正規化年齢"),
            VectorParameter("DissolveDuration", VfxValueType::Float, 0.0, 1.0, "life", 0.5f, 0.0f, 0.0f, 0.0f, "溶け切るまでの正規化年齢の長さ"),
            VectorParameter("EdgeWidth", VfxValueType::Float, 0.0, 1.0, "", 0.05f, 0.0f, 0.0f, 0.0f, "縁の帯の幅(ノイズの値の幅)"),
            VectorParameter("EdgeColor", VfxValueType::Color, 0.0, 64.0, "linear_rgba", 4.0f, 1.5f, 0.3f, 1.0f, "縁の色(線形)")
        };

        constexpr VfxParameterSchema kInitShapeParameters[] = {
            EnumParameter("Shape", kSpawnShapeNames, CountOf(kSpawnShapeNames), 0u, "位置の形"),
            VectorParameter("Radius", VfxValueType::Float, 0.0, 1e5, "space", 1.0f, 0.0f, 0.0f, 0.0f, "球の半径"),
            VectorParameter("ConeAngle", VfxValueType::Float, 0.0, 90.0, "deg", 30.0f, 0.0f, 0.0f, 0.0f, "円錐の半頂角"),
            VectorParameter("ConeLength", VfxValueType::Float, 0.0, 1e5, "space", 1.0f, 0.0f, 0.0f, 0.0f, "円錐の長さ（頂点から +Y）"),
            VectorParameter("BoxHalfExtents", VfxValueType::Vector3, 0.0, 1e5, "space", 1.0f, 1.0f, 1.0f, 0.0f, "箱の半径"),
            VectorParameter("CameraBoxHalfWidth", VfxValueType::Float, 0.0, 1e5, "space", 20.0f, 0.0f, 0.0f, 0.0f, "カメラの箱の水平の半幅"),
            VectorParameter("CameraBoxHeight", VfxValueType::Float, 0.0, 1e5, "space", 20.0f, 0.0f, 0.0f, 0.0f, "カメラの箱の高さ（カメラを中心に上下へ半分ずつ）")
        };
        constexpr VfxParameterSchema kDragParameters[] = {
            VectorParameter("Coefficient", VfxValueType::Float, 0.0, 1000.0, "1/s", 1.0f, 0.0f, 0.0f, 0.0f, "速度に比例する抗力の係数")
        };
        constexpr VfxParameterSchema kWindInfluenceParameters[] = {
            VectorParameter("Coupling", VfxValueType::Float, 0.0, 1000.0, "1/s", 1.0f, 0.0f, 0.0f, 0.0f, "風の速度へ寄る速さ"),
            VectorParameter("Scale", VfxValueType::Float, 0.0, 10.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "風の速度の倍率")
        };
        constexpr VfxParameterSchema kTurbulenceParameters[] = {
            VectorParameter("Strength", VfxValueType::Float, 0.0, 1e5, "space/s^2", 1.0f, 0.0f, 0.0f, 0.0f, "乱れの加速度の振れ幅"),
            VectorParameter("Frequency", VfxValueType::Float, 0.0001, 1000.0, "1/space", 1.0f, 0.0f, 0.0f, 0.0f, "乱れの格子の細かさ")
        };
        constexpr VfxParameterSchema kCurveOverLifeParameters[] = {
            EnumParameter("Target", kCurveTargetNames, CountOf(kCurveTargetNames), 0u, "曲線を掛ける量"),
            UIntParameter("KeyCount", 1u, kVfxMaxCurveKeys, "count", 2u, "使うキーの数"),
            VectorParameter("Key0", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 0.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key1", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key2", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key3", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key4", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key5", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key6", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Key7", VfxValueType::Vector2, 0.0, 100000.0, "life,value", 1.0f, 1.0f, 0.0f, 0.0f, "キー（x = 正規化した年齢 0〜1、y = 値）"),
            VectorParameter("Color", VfxValueType::Color, 0.0, 64.0, "linear_rgba", 1.0f, 1.0f, 1.0f, 1.0f, "Target が Color のとき、値 1 で到達する色")
        };
        constexpr VfxParameterSchema kActivationFractionParameters[] = {
            VectorParameter("Fraction", VfxValueType::Float, 0.0, 1.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "有効にする粒の割合"),
            VectorParameter("Ramp", VfxValueType::Float, 0.0, 0.5, "", 0.01f, 0.0f, 0.0f, 0.0f, "境目で不透明度を上げる幅")
        };
        constexpr VfxParameterSchema kCollisionPlaneParameters[] = {
            VectorParameter("GroundY", VfxValueType::Float, -1e6, 1e6, "space", 0.0f, 0.0f, 0.0f, 0.0f, "地面の高さ"),
            VectorParameter("KillBelowY", VfxValueType::Float, -1e6, 1e6, "space", -1e6f, 0.0f, 0.0f, 0.0f, "これより下の粒を消す高さ"),
            EnumParameter("Response", kCollisionResponseNames, CountOf(kCollisionResponseNames), 0u, "衝突したときの扱い"),
            VectorParameter("Restitution", VfxValueType::Float, 0.0, 1.0, "", 0.5f, 0.0f, 0.0f, 0.0f, "Bounce の反発係数"),
            NameParameter("ChildEmitter", 0u, 63u, "", "SpawnChild で生成する子のエミッタ名"),
            VectorParameter("ChildProbability", VfxValueType::Float, 0.0, 1.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "SpawnChild で子を生成する確率")
        };
        constexpr VfxParameterSchema kCollisionHeightFieldParameters[] = {
            EnumParameter("Response", kCollisionResponseNames, CountOf(kCollisionResponseNames), 0u, "衝突したときの扱い"),
            VectorParameter("Restitution", VfxValueType::Float, 0.0, 1.0, "", 0.5f, 0.0f, 0.0f, 0.0f, "Bounce の反発係数"),
            NameParameter("ChildEmitter", 0u, 63u, "", "SpawnChild で生成する子のエミッタ名"),
            VectorParameter("ChildProbability", VfxValueType::Float, 0.0, 1.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "SpawnChild で子を生成する確率")
        };
        constexpr VfxParameterSchema kCollisionDepthParameters[] = {
            VectorParameter("Thickness", VfxValueType::Float, 0.0, 100.0, "space", 0.1f, 0.0f, 0.0f, 0.0f, "深度の厚み"),
            EnumParameter("Response", kCollisionResponseNames, CountOf(kCollisionResponseNames), 0u, "衝突したときの扱い"),
            VectorParameter("Restitution", VfxValueType::Float, 0.0, 1.0, "", 0.5f, 0.0f, 0.0f, 0.0f, "Bounce の反発係数"),
            NameParameter("ChildEmitter", 0u, 63u, "", "SpawnChild で生成する子のエミッタ名"),
            VectorParameter("ChildProbability", VfxValueType::Float, 0.0, 1.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "SpawnChild で子を生成する確率")
        };
        constexpr VfxParameterSchema kGpuSpriteRendererParameters[] = {
            EnumParameter("Orientation", kGpuSpriteOrientationNames, CountOf(kGpuSpriteOrientationNames), 0u, "スプライトの向き"),
            EnumParameter("Blend", kGpuSpriteBlendNames, CountOf(kGpuSpriteBlendNames), 0u, "ブレンド方式"),
            EnumParameter("Pass", kGpuSpritePassNames, CountOf(kGpuSpritePassNames), 0u, "描画の段階"),
            VectorParameter("SoftDistance", VfxValueType::Float, 0.0, 100.0, "space", 0.5f, 0.0f, 0.0f, 0.0f, "ソフトパーティクルの距離"),
            EnumParameter("Lighting", kGpuSpriteLightingNames, CountOf(kGpuSpriteLightingNames), 0u, "照明の方式"),
            VectorParameter("FogInfluence", VfxValueType::Float, 0.0, 1.0, "", 1.0f, 0.0f, 0.0f, 0.0f, "霧の影響"),
            VectorParameter("StreakShutterSeconds", VfxValueType::Float, 0.0, 1.0, "s", 0.0208333f, 0.0f, 0.0f, 0.0f, "伸びのシャッター時間"),
            BoolParameter("SubPixelCompensation", true, "幅 1px 未満の補償"),
            VectorParameter("NearFadeDistance", VfxValueType::Float, 0.0, 100.0, "space", 0.5f, 0.0f, 0.0f, 0.0f, "近距離のフェード"),
            NameParameter("Texture", 0u, 63u, "", "テクスチャの資産名"),
            BoolParameter("DrawInSlotOrder", false, "スロットの順に描く（詰めずに描き、重なりの順を毎フレーム保つ）")
        };

        constexpr VfxObjectSchema kEmitterSchema = {
            "Emitter", static_cast<VfxModuleType>(0), VfxModuleStage::Object,
            kEmitterParameters, CountOf(kEmitterParameters)
        };
        constexpr VfxObjectSchema kFlipbookSchema = {
            "Flipbook", static_cast<VfxModuleType>(0), VfxModuleStage::Object,
            kFlipbookParameters, CountOf(kFlipbookParameters)
        };
        constexpr VfxObjectSchema kModuleSchemas[] = {
            {"SpawnRate", VfxModuleType::SpawnRate, VfxModuleStage::Spawn, kSpawnRateParameters, CountOf(kSpawnRateParameters)},
            {"SpawnBurst", VfxModuleType::SpawnBurst, VfxModuleStage::Spawn, kSpawnBurstParameters, CountOf(kSpawnBurstParameters)},
            {"SpawnOnEvent", VfxModuleType::SpawnOnEvent, VfxModuleStage::Spawn, kSpawnOnEventParameters, CountOf(kSpawnOnEventParameters)},
            {"InitPosition", VfxModuleType::InitPosition, VfxModuleStage::Initialize, kInitPositionParameters, CountOf(kInitPositionParameters)},
            {"InitVelocityBox", VfxModuleType::InitVelocityBox, VfxModuleStage::Initialize, kInitVelocityBoxParameters, CountOf(kInitVelocityBoxParameters)},
            {"InitLifetime", VfxModuleType::InitLifetime, VfxModuleStage::Initialize, kInitLifetimeParameters, CountOf(kInitLifetimeParameters)},
            {"InitColor", VfxModuleType::InitColor, VfxModuleStage::Initialize, kInitColorParameters, CountOf(kInitColorParameters)},
            {"InitSize", VfxModuleType::InitSize, VfxModuleStage::Initialize, kInitSizeParameters, CountOf(kInitSizeParameters)},
            {"Acceleration", VfxModuleType::Acceleration, VfxModuleStage::Update, kAccelerationParameters, CountOf(kAccelerationParameters)},
            {"SpriteRenderer", VfxModuleType::SpriteRenderer, VfxModuleStage::Render, kSpriteRendererParameters, CountOf(kSpriteRendererParameters)},
            {"MeshRenderer", VfxModuleType::MeshRenderer, VfxModuleStage::Render, kMeshRendererParameters, CountOf(kMeshRendererParameters)},
            {"InitShape", VfxModuleType::InitShape, VfxModuleStage::Initialize, kInitShapeParameters, CountOf(kInitShapeParameters)},
            {"Drag", VfxModuleType::Drag, VfxModuleStage::Update, kDragParameters, CountOf(kDragParameters)},
            {"WindInfluence", VfxModuleType::WindInfluence, VfxModuleStage::Update, kWindInfluenceParameters, CountOf(kWindInfluenceParameters)},
            {"Turbulence", VfxModuleType::Turbulence, VfxModuleStage::Update, kTurbulenceParameters, CountOf(kTurbulenceParameters)},
            {"CurveOverLife", VfxModuleType::CurveOverLife, VfxModuleStage::Update, kCurveOverLifeParameters, CountOf(kCurveOverLifeParameters)},
            {"ActivationFraction", VfxModuleType::ActivationFraction, VfxModuleStage::Update, kActivationFractionParameters, CountOf(kActivationFractionParameters)},
            {"CollisionPlane", VfxModuleType::CollisionPlane, VfxModuleStage::Update, kCollisionPlaneParameters, CountOf(kCollisionPlaneParameters)},
            {"CollisionHeightField", VfxModuleType::CollisionHeightField, VfxModuleStage::Update, kCollisionHeightFieldParameters, CountOf(kCollisionHeightFieldParameters)},
            {"CollisionDepth", VfxModuleType::CollisionDepth, VfxModuleStage::Update, kCollisionDepthParameters, CountOf(kCollisionDepthParameters)},
            {"GpuSpriteRenderer", VfxModuleType::GpuSpriteRenderer, VfxModuleStage::Render, kGpuSpriteRendererParameters, CountOf(kGpuSpriteRendererParameters)}
        };

        bool Fail(Container::String* outError, const char* code, const char* object, const char* parameter)
        {
            if (outError != nullptr)
            {
                *outError = code;
                *outError += " ";
                *outError += object;
                *outError += ".";
                *outError += parameter != nullptr ? parameter : "Unknown";
            }
            return false;
        }

        bool IsCollisionModule(VfxModuleType type)
        {
            return type == VfxModuleType::CollisionPlane || type == VfxModuleType::CollisionHeightField ||
                type == VfxModuleType::CollisionDepth;
        }

        const char* FindParameterOwner(const VfxParameterSchema& parameter)
        {
            const char* owner = nullptr;
            bool bAmbiguous = false;
            const auto inspect = [&](const VfxObjectSchema& object)
            {
                for (uint32_t index = 0u; index < object.ParameterCount; ++index)
                {
                    const VfxParameterSchema& candidate = object.Parameters[index];
                    if (&candidate == &parameter)
                    {
                        owner = object.Name;
                        return true;
                    }
                    if (parameter.Name != nullptr && candidate.Type == parameter.Type &&
                        candidate.Min == parameter.Min && candidate.Max == parameter.Max &&
                        std::strcmp(candidate.Name, parameter.Name) == 0)
                    {
                        if (owner != nullptr && std::strcmp(owner, object.Name) != 0)
                        {
                            bAmbiguous = true;
                        }
                        else
                        {
                            owner = object.Name;
                        }
                    }
                }
                return false;
            };
            if (inspect(kEmitterSchema) || inspect(kFlipbookSchema))
            {
                return owner;
            }
            for (const VfxObjectSchema& object : kModuleSchemas)
            {
                if (inspect(object))
                {
                    return owner;
                }
            }
            return owner != nullptr && !bAmbiguous ? owner : "Value";
        }

        bool ValidateName(const Container::String& name, double min, double max,
            const char* object, const char* parameter, Container::String* outError)
        {
            const double length = static_cast<double>(name.size());
            if (length < min || length > max)
            {
                return Fail(outError, "invalid_name", object, parameter);
            }
            for (size_t index = 0u; index < name.size(); ++index)
            {
                const char c = name[index];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
                {
                    return Fail(outError, "invalid_name", object, parameter);
                }
            }
            return true;
        }

        float Component(const Math::Vector4& value, uint32_t index)
        {
            switch (index)
            {
            case 0u: return value.x;
            case 1u: return value.y;
            case 2u: return value.z;
            default: return value.w;
            }
        }

        bool ValidateValue(const VfxParameterSchema& schema, const VfxValue& value,
            const char* object, Container::String* outError)
        {
            if (schema.Type != value.Type)
            {
                return Fail(outError, "type_mismatch", object, schema.Name);
            }

            uint32_t componentCount = 0u;
            switch (value.Type)
            {
            case VfxValueType::Float: componentCount = 1u; break;
            case VfxValueType::Vector2: componentCount = 2u; break;
            case VfxValueType::Vector3: componentCount = 3u; break;
            case VfxValueType::Color: componentCount = 4u; break;
            case VfxValueType::UInt:
            case VfxValueType::Bool:
            case VfxValueType::Enum:
                if (static_cast<double>(value.UInt) < schema.Min || static_cast<double>(value.UInt) > schema.Max ||
                    (value.Type == VfxValueType::Bool && value.UInt > 1u) ||
                    (value.Type == VfxValueType::Enum && value.UInt >= schema.EnumCount))
                {
                    return Fail(outError, "out_of_range", object, schema.Name);
                }
                return true;
            case VfxValueType::Name:
                return ValidateName(value.Name, schema.Min, schema.Max, object, schema.Name, outError);
            default:
                return Fail(outError, "type_mismatch", object, schema.Name);
            }

            for (uint32_t index = 0u; index < componentCount; ++index)
            {
                if (!std::isfinite(Component(value.Vector, index)))
                {
                    return Fail(outError, "not_finite", object, schema.Name);
                }
            }
            const float min = static_cast<float>(schema.Min);
            const float max = static_cast<float>(schema.Max);
            for (uint32_t index = 0u; index < componentCount; ++index)
            {
                const float component = Component(value.Vector, index);
                if (component < min || component > max)
                {
                    return Fail(outError, "out_of_range", object, schema.Name);
                }
            }
            return true;
        }
    } // namespace

    VfxValue VfxValue::MakeFloat(float value)
    {
        VfxValue result;
        result.Vector.x = value;
        return result;
    }

    VfxValue VfxValue::MakeUInt(uint32_t value)
    {
        VfxValue result;
        result.Type = VfxValueType::UInt;
        result.UInt = value;
        return result;
    }

    VfxValue VfxValue::MakeBool(bool value)
    {
        VfxValue result;
        result.Type = VfxValueType::Bool;
        result.UInt = value ? 1u : 0u;
        return result;
    }

    VfxValue VfxValue::MakeVector2(const Math::Vector2& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Vector2;
        result.Vector.x = value.x;
        result.Vector.y = value.y;
        return result;
    }

    VfxValue VfxValue::MakeVector3(const Math::Vector3& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Vector3;
        result.Vector.x = value.x;
        result.Vector.y = value.y;
        result.Vector.z = value.z;
        return result;
    }

    VfxValue VfxValue::MakeColor(const Math::Vector4& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Color;
        result.Vector = value;
        return result;
    }

    VfxValue VfxValue::MakeEnum(uint32_t value)
    {
        VfxValue result;
        result.Type = VfxValueType::Enum;
        result.UInt = value;
        return result;
    }

    VfxValue VfxValue::MakeName(const char* value)
    {
        VfxValue result;
        result.Type = VfxValueType::Name;
        result.Name = value;
        return result;
    }

    bool VfxValue::operator==(const VfxValue& other) const
    {
        if (Type != other.Type)
        {
            return false;
        }
        switch (Type)
        {
        case VfxValueType::Float:
            return Vector.x == other.Vector.x;
        case VfxValueType::Vector2:
            return Vector.x == other.Vector.x && Vector.y == other.Vector.y;
        case VfxValueType::Vector3:
            return Vector.x == other.Vector.x && Vector.y == other.Vector.y && Vector.z == other.Vector.z;
        case VfxValueType::Color:
            return Vector == other.Vector;
        case VfxValueType::UInt:
        case VfxValueType::Bool:
        case VfxValueType::Enum:
            return UInt == other.UInt;
        case VfxValueType::Name:
            if (Name.size() != other.Name.size())
            {
                return false;
            }
            for (size_t index = 0u; index < Name.size(); ++index)
            {
                if (Name[index] != other.Name[index])
                {
                    return false;
                }
            }
            return true;
        default:
            return false;
        }
    }

    Container::Span<const VfxObjectSchema> GetVfxModuleSchemas()
    {
        return Container::Span<const VfxObjectSchema>(kModuleSchemas);
    }

    const VfxObjectSchema* FindVfxModuleSchema(VfxModuleType type)
    {
        for (const VfxObjectSchema& schema : kModuleSchemas)
        {
            if (schema.ModuleType == type)
            {
                return &schema;
            }
        }
        return nullptr;
    }

    const VfxObjectSchema* FindVfxModuleSchemaByName(const char* name)
    {
        if (name == nullptr)
        {
            return nullptr;
        }
        for (const VfxObjectSchema& schema : kModuleSchemas)
        {
            if (std::strcmp(schema.Name, name) == 0)
            {
                return &schema;
            }
        }
        return nullptr;
    }

    const VfxObjectSchema& GetVfxEmitterSchema()
    {
        return kEmitterSchema;
    }

    const VfxObjectSchema& GetVfxFlipbookSchema()
    {
        return kFlipbookSchema;
    }

    int32_t FindVfxParameterIndex(const VfxObjectSchema& schema, const char* name)
    {
        if (name == nullptr)
        {
            return -1;
        }
        for (uint32_t index = 0u; index < schema.ParameterCount; ++index)
        {
            if (std::strcmp(schema.Parameters[index].Name, name) == 0)
            {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    VfxValue MakeVfxDefaultValue(const VfxParameterSchema& schema)
    {
        switch (schema.Type)
        {
        case VfxValueType::Float:
            return VfxValue::MakeFloat(schema.DefaultVector[0]);
        case VfxValueType::UInt:
            return VfxValue::MakeUInt(schema.DefaultUInt);
        case VfxValueType::Bool:
            return VfxValue::MakeBool(schema.DefaultUInt != 0u);
        case VfxValueType::Vector2:
            return VfxValue::MakeVector2(Math::Vector2(schema.DefaultVector[0], schema.DefaultVector[1]));
        case VfxValueType::Vector3:
            return VfxValue::MakeVector3(Math::Vector3(schema.DefaultVector[0], schema.DefaultVector[1], schema.DefaultVector[2]));
        case VfxValueType::Color:
            return VfxValue::MakeColor(Math::Vector4(schema.DefaultVector[0], schema.DefaultVector[1], schema.DefaultVector[2], schema.DefaultVector[3]));
        case VfxValueType::Enum:
            return VfxValue::MakeEnum(schema.DefaultUInt);
        case VfxValueType::Name:
            return VfxValue::MakeName(schema.DefaultName);
        default:
            return {};
        }
    }

    bool ValidateVfxValue(const VfxParameterSchema& schema, const VfxValue& value, Container::String* outError)
    {
        return ValidateValue(schema, value, FindParameterOwner(schema), outError);
    }

    VfxModule MakeVfxDefaultModule(VfxModuleType type)
    {
        VfxModule module;
        module.Type = type;
        const VfxObjectSchema* schema = FindVfxModuleSchema(type);
        if (schema != nullptr)
        {
            module.Values.reserve(schema->ParameterCount);
            for (uint32_t index = 0u; index < schema->ParameterCount; ++index)
            {
                module.Values.push_back(MakeVfxDefaultValue(schema->Parameters[index]));
            }
        }
        return module;
    }

    VfxEmitterAsset MakeVfxDefaultEmitter(const char* name)
    {
        VfxEmitterAsset emitter;
        emitter.Name = name;
        emitter.Properties.reserve(kEmitterSchema.ParameterCount);
        for (uint32_t index = 0u; index < kEmitterSchema.ParameterCount; ++index)
        {
            emitter.Properties.push_back(MakeVfxDefaultValue(kEmitterSchema.Parameters[index]));
        }
        return emitter;
    }

    bool ValidateVfxModule(const VfxModule& module, Container::String* outError)
    {
        const VfxObjectSchema* schema = FindVfxModuleSchema(module.Type);
        if (schema == nullptr)
        {
            return Fail(outError, "unknown_module", "Module", "Type");
        }
        if (module.Values.size() != schema->ParameterCount)
        {
            return Fail(outError, "parameter_count", schema->Name, "Values");
        }
        for (uint32_t index = 0u; index < schema->ParameterCount; ++index)
        {
            if (!ValidateValue(schema->Parameters[index], module.Values[index], schema->Name, outError))
            {
                return false;
            }
        }
        if (module.Type == VfxModuleType::InitVelocityBox)
        {
            const Math::Vector4& min = module.Values[0].Vector;
            const Math::Vector4& max = module.Values[1].Vector;
            if (min.x > max.x || min.y > max.y || min.z > max.z)
            {
                return Fail(outError, "min_greater_than_max", schema->Name, "Min");
            }
        }
        if (module.Type == VfxModuleType::CurveOverLife)
        {
            const uint32_t keyCount = module.Values[1].UInt;
            for (uint32_t index = 0u; index < keyCount; ++index)
            {
                const uint32_t parameterIndex = 2u + index;
                const float age = module.Values[parameterIndex].Vector.x;
                if (age < 0.0f || age > 1.0f ||
                    (index > 0u && age <= module.Values[parameterIndex - 1u].Vector.x))
                {
                    return Fail(outError, "out_of_range", schema->Name, schema->Parameters[parameterIndex].Name);
                }
            }
        }
        return true;
    }

    bool ValidateVfxEmitter(const VfxEmitterAsset& emitter, Container::String* outError)
    {
        if (!ValidateName(emitter.Name, 1.0, 63.0, "Emitter", "Name", outError))
        {
            return false;
        }
        if (emitter.Properties.size() != kEmitterSchema.ParameterCount)
        {
            return Fail(outError, "parameter_count", "Emitter", "Properties");
        }
        for (uint32_t index = 0u; index < kEmitterSchema.ParameterCount; ++index)
        {
            if (!ValidateValue(kEmitterSchema.Parameters[index], emitter.Properties[index], "Emitter", outError))
            {
                return false;
            }
        }
        const uint32_t executorIndex = static_cast<uint32_t>(FindVfxParameterIndex(kEmitterSchema, "Executor"));
        const uint32_t randomStreamIndex = static_cast<uint32_t>(FindVfxParameterIndex(kEmitterSchema, "RandomStream"));
        const uint32_t spaceIndex = static_cast<uint32_t>(FindVfxParameterIndex(kEmitterSchema, "Space"));
        const uint32_t wrapPolicyIndex = static_cast<uint32_t>(FindVfxParameterIndex(kEmitterSchema, "WrapPolicy"));
        const bool bGpu = emitter.Properties[executorIndex].UInt == static_cast<uint32_t>(VfxExecutor::Gpu);
        const bool bWorld = emitter.Properties[spaceIndex].UInt == static_cast<uint32_t>(VfxSimulationSpace::World);
        if (bGpu && emitter.Properties[randomStreamIndex].UInt != static_cast<uint32_t>(VfxRandomStream::Counter))
        {
            return Fail(outError, "gpu_requires_counter_stream", "Emitter", "RandomStream");
        }
        if (bGpu && !bWorld)
        {
            return Fail(outError, "gpu_requires_world_space", "Emitter", "Space");
        }

        bool bHasRenderer = false;
        bool bHasMeshRenderer = false;
        bool bHasShape = false;
        bool bHasCameraBox = false;
        bool bCurveTargets[CountOf(kCurveTargetNames)] = {};
        for (const VfxModule& module : emitter.Modules)
        {
            if (!ValidateVfxModule(module, outError))
            {
                return false;
            }
            const VfxObjectSchema& schema = *FindVfxModuleSchema(module.Type);
            if (!bGpu && (module.Type == VfxModuleType::CollisionDepth || module.Type == VfxModuleType::GpuSpriteRenderer))
            {
                return Fail(outError, "requires_gpu_executor", schema.Name, "Executor");
            }
            if (bGpu && (module.Type == VfxModuleType::SpriteRenderer || module.Type == VfxModuleType::MeshRenderer))
            {
                return Fail(outError, "requires_cpu_executor", schema.Name, "Executor");
            }
            if (module.Type == VfxModuleType::SpriteRenderer || module.Type == VfxModuleType::MeshRenderer ||
                module.Type == VfxModuleType::GpuSpriteRenderer)
            {
                if (bHasRenderer)
                {
                    return Fail(outError, "multiple_renderers", "Emitter", "Modules");
                }
                bHasRenderer = true;
                bHasMeshRenderer = module.Type == VfxModuleType::MeshRenderer;
            }
            if (module.Type == VfxModuleType::InitShape)
            {
                if (bHasShape)
                {
                    return Fail(outError, "multiple_shapes", "Emitter", "Modules");
                }
                bHasShape = true;
                bHasCameraBox = module.Values[0].UInt == static_cast<uint32_t>(VfxSpawnShape::CameraBox);
                if (bHasCameraBox && !bWorld)
                {
                    return Fail(outError, "camera_box_requires_world_space", "InitShape", "Space");
                }
            }
            if (module.Type == VfxModuleType::CurveOverLife)
            {
                const uint32_t target = module.Values[0].UInt;
                if (bCurveTargets[target])
                {
                    return Fail(outError, "duplicate_curve_target", "CurveOverLife", "Target");
                }
                bCurveTargets[target] = true;
            }
            if (IsCollisionModule(module.Type))
            {
                const uint32_t responseIndex = static_cast<uint32_t>(FindVfxParameterIndex(schema, "Response"));
                const uint32_t childIndex = static_cast<uint32_t>(FindVfxParameterIndex(schema, "ChildEmitter"));
                if (module.Values[responseIndex].UInt == static_cast<uint32_t>(VfxCollisionResponse::SpawnChild) &&
                    module.Values[childIndex].Name.empty())
                {
                    return Fail(outError, "missing_child_emitter", schema.Name, "ChildEmitter");
                }
            }
        }
        if (bHasMeshRenderer && !bWorld)
        {
            return Fail(outError, "mesh_requires_world_space", "MeshRenderer", "Space");
        }
        if (emitter.Properties[wrapPolicyIndex].UInt == static_cast<uint32_t>(VfxWrapPolicy::WrapInCameraBox) && !bHasCameraBox)
        {
            return Fail(outError, "wrap_requires_camera_box", "Emitter", "WrapPolicy");
        }
        return true;
    }

    bool ValidateVfxFlipbook(const VfxFlipbookImportSettings& flipbook, Container::String* outError)
    {
        if (!ValidateName(flipbook.Name, 1.0, 63.0, "Flipbook", "Name", outError))
        {
            return false;
        }
        const VfxValue values[] = {
            VfxValue::MakeUInt(flipbook.Columns),
            VfxValue::MakeUInt(flipbook.Rows),
            VfxValue::MakeUInt(flipbook.FrameCount),
            VfxValue::MakeFloat(flipbook.FramesPerSecond),
            VfxValue::MakeUInt(flipbook.StartFrame),
            VfxValue::MakeBool(flipbook.bLoop)
        };
        for (uint32_t index = 0u; index < kFlipbookSchema.ParameterCount; ++index)
        {
            if (!ValidateValue(kFlipbookSchema.Parameters[index], values[index], "Flipbook", outError))
            {
                return false;
            }
        }
        if (flipbook.FrameCount > flipbook.Columns * flipbook.Rows)
        {
            return Fail(outError, "out_of_range", "Flipbook", "FrameCount");
        }
        if (flipbook.StartFrame >= flipbook.FrameCount)
        {
            return Fail(outError, "out_of_range", "Flipbook", "StartFrame");
        }
        return true;
    }

    bool ValidateVfxEffect(const VfxEffectAsset& effect, Container::String* outError)
    {
        for (size_t index = 0u; index < effect.Flipbooks.size(); ++index)
        {
            if (!ValidateVfxFlipbook(effect.Flipbooks[index], outError))
            {
                return false;
            }
            for (size_t previous = 0u; previous < index; ++previous)
            {
                if (effect.Flipbooks[index].Name == effect.Flipbooks[previous].Name)
                {
                    return Fail(outError, "duplicate_name", "Effect", "Flipbooks");
                }
            }
        }
        for (size_t index = 0u; index < effect.Emitters.size(); ++index)
        {
            const VfxEmitterAsset& emitter = effect.Emitters[index];
            if (!ValidateVfxEmitter(emitter, outError))
            {
                return false;
            }
            for (size_t previous = 0u; previous < index; ++previous)
            {
                if (emitter.Name == effect.Emitters[previous].Name)
                {
                    return Fail(outError, "duplicate_name", "Effect", "Emitters");
                }
            }
            for (const VfxModule& module : emitter.Modules)
            {
                if (IsCollisionModule(module.Type))
                {
                    const VfxObjectSchema& schema = *FindVfxModuleSchema(module.Type);
                    const uint32_t responseIndex = static_cast<uint32_t>(FindVfxParameterIndex(schema, "Response"));
                    if (module.Values[responseIndex].UInt == static_cast<uint32_t>(VfxCollisionResponse::SpawnChild))
                    {
                        const uint32_t childIndex = static_cast<uint32_t>(FindVfxParameterIndex(schema, "ChildEmitter"));
                        const Container::String& childName = module.Values[childIndex].Name;
                        bool bFound = false;
                        if (!(childName == emitter.Name))
                        {
                            for (const VfxEmitterAsset& child : effect.Emitters)
                            {
                                if (child.Name == childName)
                                {
                                    bFound = true;
                                    break;
                                }
                            }
                        }
                        if (!bFound)
                        {
                            return Fail(outError, "unknown_child_emitter", schema.Name, "ChildEmitter");
                        }
                    }
                }
                if (module.Type != VfxModuleType::SpriteRenderer)
                {
                    continue;
                }
                const Container::String& flipbookName = module.Values[3].Name;
                if (flipbookName.empty())
                {
                    continue;
                }
                bool bFound = false;
                for (const VfxFlipbookImportSettings& flipbook : effect.Flipbooks)
                {
                    if (flipbook.Name == flipbookName)
                    {
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    return Fail(outError, "unknown_flipbook", "SpriteRenderer", "Flipbook");
                }
            }
        }
        return true;
    }

    bool SetVfxModuleParameter(VfxModule& module, const char* name, const VfxValue& value, Container::String* outError)
    {
        const VfxObjectSchema* schema = FindVfxModuleSchema(module.Type);
        if (schema == nullptr)
        {
            return Fail(outError, "unknown_module", "Module", "Type");
        }
        const int32_t index = FindVfxParameterIndex(*schema, name);
        if (index < 0)
        {
            return Fail(outError, "unknown_parameter", schema->Name, name);
        }
        if (module.Values.size() != schema->ParameterCount)
        {
            return Fail(outError, "parameter_count", schema->Name, "Values");
        }
        VfxModule candidate = module;
        candidate.Values[static_cast<uint32_t>(index)] = value;
        if (!ValidateVfxModule(candidate, outError))
        {
            return false;
        }
        module = std::move(candidate);
        return true;
    }

    bool SetVfxEmitterProperty(VfxEmitterAsset& emitter, const char* name, const VfxValue& value, Container::String* outError)
    {
        const int32_t index = FindVfxParameterIndex(kEmitterSchema, name);
        if (index < 0)
        {
            return Fail(outError, "unknown_parameter", "Emitter", name);
        }
        if (emitter.Properties.size() != kEmitterSchema.ParameterCount)
        {
            return Fail(outError, "parameter_count", "Emitter", "Properties");
        }
        VfxEmitterAsset candidate = emitter;
        candidate.Properties[static_cast<uint32_t>(index)] = value;
        if (!ValidateVfxEmitter(candidate, outError))
        {
            return false;
        }
        emitter = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Particle
