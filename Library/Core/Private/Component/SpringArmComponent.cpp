#include "Component/SpringArmComponent.h"
#include "Object/WorldObject.h"
#include "Object/World.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Math/QuaternionUtils.h"
#include "Math/MathTypes.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <algorithm>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(SpringArmComponent, Component)

    namespace
    {
        // MayaCameraController の既定値に合わせる。
        constexpr float kDefaultArmLength = 5.0f;
        constexpr float kDefaultYaw = 0.0f;
        constexpr float kDefaultPitch = 30.0f;
        constexpr float kDefaultMinPitch = -89.0f;
        constexpr float kDefaultMaxPitch = 89.0f;
        constexpr float kDefaultMinArmLength = 0.1f;
        constexpr float kDefaultMaxArmLength = 10000.0f;

        // 特異点ガード。DriveOwnerTransform は forward.Normalized()/LookRotation に依存し、
        // Pitch=±90（forward∥up）や ArmLength=0（forward ゼロ）で破綻する。
        // 公開 setter でこれらの危険値を物理的に防ぐ。
        constexpr float kMaxSafePitch = 89.9f;
        constexpr float kMinSafeArmLength = 0.01f;

        // ピボット未解決の警告を抑制する間隔（Tick 回数）。
        constexpr uint32_t kMissingPivotWarnInterval = 300;

        float NormalizeYawDegrees(float yaw)
        {
            while (yaw > 360.0f)
            {
                yaw -= 360.0f;
            }
            while (yaw < 0.0f)
            {
                yaw += 360.0f;
            }
            return yaw;
        }
    }

    SpringArmComponent::SpringArmComponent()
        : Component()
    {
        PivotObjectId = 0;
        ArmLength = kDefaultArmLength;
        Yaw = kDefaultYaw;
        Pitch = kDefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = kDefaultMinPitch;
        MaxPitch = kDefaultMaxPitch;
        MinArmLength = kDefaultMinArmLength;
        MaxArmLength = kDefaultMaxArmLength;
    }

    SpringArmComponent::SpringArmComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        PivotObjectId = 0;
        ArmLength = kDefaultArmLength;
        Yaw = kDefaultYaw;
        Pitch = kDefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = kDefaultMinPitch;
        MaxPitch = kDefaultMaxPitch;
        MinArmLength = kDefaultMinArmLength;
        MaxArmLength = kDefaultMaxArmLength;
    }

    SpringArmComponent::SpringArmComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        PivotObjectId = 0;
        ArmLength = kDefaultArmLength;
        Yaw = kDefaultYaw;
        Pitch = kDefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = kDefaultMinPitch;
        MaxPitch = kDefaultMaxPitch;
        MinArmLength = kDefaultMinArmLength;
        MaxArmLength = kDefaultMaxArmLength;
    }

    SpringArmComponent::~SpringArmComponent()
    {
    }

    void SpringArmComponent::Initialize()
    {
        Component::Initialize();
    }

    void SpringArmComponent::Finalize()
    {
        Component::Finalize();
    }

    void SpringArmComponent::BeginPlay()
    {
        Component::BeginPlay();
    }

    void SpringArmComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void SpringArmComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
        RefreshOwnerTransform();
    }

    void SpringArmComponent::RefreshOwnerTransform()
    {
        WorldObject *pivot = ResolvePivot();
        if (pivot == nullptr)
        {
            // ピボットが解決できない場合、オーナー Transform は維持して暴れない。
            // use-after-free を避けるため破棄済みポインタには一切触れない。
            if (PivotObjectId != 0)
            {
                if ((m_MissingPivotWarnTicks % kMissingPivotWarnInterval) == 0)
                {
                    NORVES_LOG_WARNING("SpringArmComponent",
                                       "Pivot (ObjectId=%llu) could not be resolved; keeping owner transform",
                                       static_cast<unsigned long long>(PivotObjectId.Get()));
                }
                ++m_MissingPivotWarnTicks;
            }
            return;
        }

        m_MissingPivotWarnTicks = 0;
        DriveOwnerTransform(pivot);
    }

    // ========================================
    // ピボット管理
    // ========================================

    void SpringArmComponent::SetPivot(const WorldObject *pivot)
    {
        PivotObjectId = (pivot != nullptr) ? pivot->GetObjectId() : 0;
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetPivotObjectId(uint64_t id)
    {
        PivotObjectId = id;
        MarkRenderStateDirty();
    }

    void SpringArmComponent::ClearPivot()
    {
        PivotObjectId = 0;
        MarkRenderStateDirty();
    }

    bool SpringArmComponent::HasValidPivot() const
    {
        return ResolvePivot() != nullptr;
    }

    WorldObject *SpringArmComponent::ResolvePivot() const
    {
        if (PivotObjectId == 0)
        {
            return nullptr;
        }

        const WorldObject *owner = GetOwner();
        if (owner == nullptr)
        {
            return nullptr;
        }

        World *world = owner->GetWorld();
        if (world == nullptr)
        {
            return nullptr;
        }

        return world->FindObjectById(PivotObjectId);
    }

    // ========================================
    // アームパラメーター
    // ========================================

    void SpringArmComponent::SetArmLength(float armLength)
    {
        ArmLength = std::clamp(armLength, MinArmLength.Get(), MaxArmLength.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetYaw(float yaw)
    {
        Yaw = NormalizeYawDegrees(yaw);
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetPitch(float pitch)
    {
        Pitch = std::clamp(pitch, MinPitch.Get(), MaxPitch.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetTargetOffset(const Math::Vector3 &offset)
    {
        TargetOffset = offset;
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetPitchLimits(float minPitch, float maxPitch)
    {
        if (minPitch > maxPitch)
        {
            std::swap(minPitch, maxPitch);
        }

        // 特異点（Pitch=±90 で forward∥up）を避けるため安全範囲へクランプ。
        minPitch = std::clamp(minPitch, -kMaxSafePitch, kMaxSafePitch);
        maxPitch = std::clamp(maxPitch, -kMaxSafePitch, kMaxSafePitch);

        MinPitch = minPitch;
        MaxPitch = maxPitch;

        // 既存の Pitch を新しい制限に収める。
        Pitch = std::clamp(Pitch.Get(), MinPitch.Get(), MaxPitch.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetArmLengthLimits(float minArmLength, float maxArmLength)
    {
        if (minArmLength > maxArmLength)
        {
            std::swap(minArmLength, maxArmLength);
        }

        // ArmLength=0（forward ゼロ→正規化破綻）を避けるため下限を強制する。
        minArmLength = std::max(minArmLength, kMinSafeArmLength);
        maxArmLength = std::max(maxArmLength, minArmLength);

        MinArmLength = minArmLength;
        MaxArmLength = maxArmLength;

        // 既存の ArmLength を新しい制限に収める。
        ArmLength = std::clamp(ArmLength.Get(), MinArmLength.Get(), MaxArmLength.Get());
        MarkRenderStateDirty();
    }

    // ========================================
    // 入力意図の適用
    // ========================================

    void SpringArmComponent::ApplyIntent(const SpringArmIntent &intent)
    {
        // Yaw / Pitch / ArmLength の更新（クランプ込み）。
        Yaw = NormalizeYawDegrees(Yaw.Get() + intent.YawDelta);
        Pitch = std::clamp(Pitch.Get() + intent.PitchDelta, MinPitch.Get(), MaxPitch.Get());
        ArmLength = std::clamp(ArmLength.Get() - intent.DollyDelta, MinArmLength.Get(), MaxArmLength.Get());

        // Pan: スクリーン基底（x=right, y=up）を現在のカメラ basis へ投影して
        // ピボット WorldObject の Position を動かす（焦点移動＝Maya 忠実）。
        const bool bHasPan = (intent.PanDelta.x != 0.0f) ||
                             (intent.PanDelta.y != 0.0f) ||
                             (intent.PanDelta.z != 0.0f);
        if (bHasPan)
        {
            WorldObject *pivot = ResolvePivot();
            if (pivot != nullptr)
            {
                // 現在のアーム姿勢から forward を求め、CameraComponent と同一規約で
                // right/up を導出する（LookRotation: ローカル +Z = forward）。
                const Math::Vector3 armOffset = ComputeArmOffset();
                const Math::Vector3 forward = (armOffset * -1.0f).Normalized();
                const Math::Quaternion rotation =
                    Math::QuaternionUtils::LookRotation(forward, Math::Vector3::Up);
                const Math::Vector3 right = rotation * Math::Vector3::Right;
                const Math::Vector3 up = rotation * Math::Vector3::Up;

                // LookRotation 由来の right は右手系（Cross(up, forward)）で導かれるため、
                // 旧 MayaCameraController::GetRight()（左手系 Cross(forward, worldUp)）と符号が逆。
                // 旧 Pan は offset = right_old*(-deltaX*pan) + up*(deltaY*pan)、
                // BuildIntent は PanDelta.x = -deltaX*pan / PanDelta.y = +deltaY*pan を渡すので
                // 旧 offset = right_old*PanDelta.x + up*PanDelta.y。
                // ここで right == -right_old なので、旧 Pan のドラッグ感に合わせて
                // x 成分は -right(= 旧 right_old) へ投影する。up は up_new==up_old なので不変でよい。
                const Math::Vector3 worldDelta =
                    (right * -1.0f) * intent.PanDelta.x + up * intent.PanDelta.y;
                pivot->SetPosition(pivot->GetPosition() + worldDelta);
            }
        }

        MarkRenderStateDirty();
    }

    // ========================================
    // 内部駆動
    // ========================================

    Math::Vector3 SpringArmComponent::ComputeArmOffset() const
    {
        // 球面座標 → デカルト座標（注視点からの相対）。
        // MayaCameraController::RecalculatePosition と同一式（Y-up, 右手座標系）。
        const float yawRad = Yaw.Get() * Math::Constants::PI / 180.0f;
        const float pitchRad = Pitch.Get() * Math::Constants::PI / 180.0f;

        const float cosP = std::cos(pitchRad);
        const float sinP = std::sin(pitchRad);
        const float cosY = std::cos(yawRad);
        const float sinY = std::sin(yawRad);

        const float distance = ArmLength.Get();
        return Math::Vector3(
            distance * cosP * sinY,
            distance * sinP,
            distance * cosP * cosY);
    }

    void SpringArmComponent::DriveOwnerTransform(const WorldObject *pivot)
    {
        WorldObject *owner = GetOwner();
        if (owner == nullptr || pivot == nullptr)
        {
            return;
        }

        // 注視点 = ピボット位置 + 静的構図補正オフセット。
        const Math::Vector3 target = pivot->GetPosition() + TargetOffset.Get();

        // カメラ位置 = 注視点中心の球面座標（Maya と同一式）。
        const Math::Vector3 cameraPos = target + ComputeArmOffset();

        // forward は注視点へ向かう方向。
        const Math::Vector3 forward = (target - cameraPos).Normalized();

        // ローカル +Z = forward の規約で回転を構築（CameraComponent と整合）。
        // up 引数は Vector3::Up を渡す。これにより forward/up は
        // MayaCameraController::ApplyTo の forward/up と一致し、結果として
        // CameraViewConstants の view 行列（forward/up のみ使用）が一致する。
        const Math::Quaternion rotation =
            Math::QuaternionUtils::LookRotation(forward, Math::Vector3::Up);

        owner->SetPosition(cameraPos);
        owner->SetRotation(rotation);
    }

} // namespace NorvesLib::Core::Component
