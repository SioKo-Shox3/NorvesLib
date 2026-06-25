#include "Input/MayaCameraController.h"
#include "Math/MathTypes.h"
#include <cmath>
#include <algorithm>

namespace NorvesLib::Core::Input
{

    MayaCameraController::MayaCameraController()
        : m_Target(Math::Vector3::Zero), m_Distance(5.0f), m_Yaw(0.0f), m_Pitch(30.0f), m_Position(Math::Vector3::Zero), m_OrbitSpeed(0.3f), m_PanSpeed(0.005f), m_DollySpeed(0.01f), m_ScrollDollySpeed(0.1f), m_MinDistance(0.1f), m_MaxDistance(10000.0f)
    {
        RecalculatePosition();
    }

    void MayaCameraController::Initialize(const Math::Vector3 &target, float distance, float yaw, float pitch)
    {
        m_Target = target;
        m_Distance = distance;
        m_Yaw = yaw;
        m_Pitch = pitch;
        RecalculatePosition();
    }

    void MayaCameraController::Update(const InputState &input, float deltaTime)
    {
        (void)deltaTime; // マウスドラッグベースなので直接deltaTimeは使用しない

        const auto &mouse = input.GetMouseState();

        // LMB: Orbit (Tumble)
        if (input.IsMouseButtonDown(MouseButton::Left))
        {
            Orbit(mouse.DeltaX, mouse.DeltaY);
        }

        // MMB: Pan (Track)
        if (input.IsMouseButtonDown(MouseButton::Middle))
        {
            Pan(mouse.DeltaX, mouse.DeltaY);
        }

        // RMB: Dolly
        if (input.IsMouseButtonDown(MouseButton::Right))
        {
            Dolly(-mouse.DeltaX * m_DollySpeed * m_Distance);
        }

        // スクロール: Dolly
        if (std::abs(mouse.ScrollDelta) > 0.0f)
        {
            Dolly(mouse.ScrollDelta * m_ScrollDollySpeed * m_Distance);
        }
    }

    Component::SpringArmIntent MayaCameraController::BuildIntent(const InputState &input, float deltaTime, float currentArmLength) const
    {
        (void)deltaTime; // マウスドラッグベースなので直接deltaTimeは使用しない（Updateと同じ）

        Component::SpringArmIntent intent;

        const auto &mouse = input.GetMouseState();

        // LMB: Orbit (Tumble)
        // Update: Orbit() で m_Yaw -= deltaX*speed, m_Pitch -= deltaY*speed。
        // ApplyIntent は Yaw += YawDelta / Pitch += PitchDelta なので符号をそのまま渡す。
        if (input.IsMouseButtonDown(MouseButton::Left))
        {
            if (mouse.DeltaX != 0.0f || mouse.DeltaY != 0.0f)
            {
                intent.YawDelta += -mouse.DeltaX * m_OrbitSpeed;
                intent.PitchDelta += -mouse.DeltaY * m_OrbitSpeed;
                intent.bHasInput = true;
            }
        }

        // MMB: Pan (Track)
        // Update: panAmount = m_PanSpeed*m_Distance、
        //         offset = right*(-deltaX*panAmount) + up*(deltaY*panAmount)。
        // ApplyIntent は PanDelta（スクリーン基底 x=right, y=up）を basis へ投影する。
        // ドラッグ感（右ドラッグで構図が右へ流れる）を保つよう同じ係数で表現する。
        // 距離スケールは m_Distance ではなく呼び出し側の currentArmLength を使う
        //（距離の真実が SpringArmComponent 側にあるため）。
        if (input.IsMouseButtonDown(MouseButton::Middle))
        {
            if (mouse.DeltaX != 0.0f || mouse.DeltaY != 0.0f)
            {
                const float panAmount = m_PanSpeed * currentArmLength;
                intent.PanDelta.x += -mouse.DeltaX * panAmount;
                intent.PanDelta.y += mouse.DeltaY * panAmount;
                intent.bHasInput = true;
            }
        }

        // RMB: Dolly
        // Update: Dolly(-deltaX*m_DollySpeed*m_Distance)、Dolly() は m_Distance -= delta
        //         → m_Distance の変化 = +deltaX*m_DollySpeed*m_Distance。
        // ApplyIntent は ArmLength -= DollyDelta なので DollyDelta = -deltaX*m_DollySpeed*currentArmLength。
        // 距離スケールは m_Distance ではなく currentArmLength を使う。
        if (input.IsMouseButtonDown(MouseButton::Right))
        {
            if (mouse.DeltaX != 0.0f)
            {
                intent.DollyDelta += -mouse.DeltaX * m_DollySpeed * currentArmLength;
                intent.bHasInput = true;
            }
        }

        // スクロール: Dolly
        // Update: Dolly(scroll*m_ScrollDollySpeed*m_Distance) → m_Distance -= それ。
        // ApplyIntent は ArmLength -= DollyDelta なので DollyDelta = scroll*m_ScrollDollySpeed*currentArmLength。
        // 距離スケールは m_Distance ではなく currentArmLength を使う。
        if (std::abs(mouse.ScrollDelta) > 0.0f)
        {
            intent.DollyDelta += mouse.ScrollDelta * m_ScrollDollySpeed * currentArmLength;
            intent.bHasInput = true;
        }

        return intent;
    }

    Math::Vector3 MayaCameraController::GetPosition() const
    {
        return m_Position;
    }

    Math::Vector3 MayaCameraController::GetForward() const
    {
        // カメラから注視点への方向
        Math::Vector3 forward = m_Target - m_Position;
        return forward.Normalized();
    }

    Math::Vector3 MayaCameraController::GetUp() const
    {
        // Forwardとワールド上方向からRightを求め、RightとForwardの外積でUpを求める
        Math::Vector3 forward = GetForward();
        Math::Vector3 worldUp(0.0f, 1.0f, 0.0f);
        Math::Vector3 right = Math::VectorUtils::Cross(forward, worldUp).Normalized();

        // ほぼ真上/真下を向いている場合の対策
        if (right.LengthSquared() < Math::Constants::EPSILON)
        {
            right = Math::Vector3(1.0f, 0.0f, 0.0f);
        }

        return Math::VectorUtils::Cross(right, forward).Normalized();
    }

    Math::Vector3 MayaCameraController::GetRight() const
    {
        Math::Vector3 forward = GetForward();
        Math::Vector3 worldUp(0.0f, 1.0f, 0.0f);
        Math::Vector3 right = Math::VectorUtils::Cross(forward, worldUp).Normalized();

        if (right.LengthSquared() < Math::Constants::EPSILON)
        {
            right = Math::Vector3(1.0f, 0.0f, 0.0f);
        }

        return right;
    }

    Math::Vector3 MayaCameraController::GetTarget() const
    {
        return m_Target;
    }

    float MayaCameraController::GetDistance() const
    {
        return m_Distance;
    }

    float MayaCameraController::GetYaw() const
    {
        return m_Yaw;
    }

    float MayaCameraController::GetPitch() const
    {
        return m_Pitch;
    }

    void MayaCameraController::ApplyTo(Rendering::CameraProxy &camera) const
    {
        Math::Vector3 pos = GetPosition();
        Math::Vector3 forward = GetForward();
        Math::Vector3 up = GetUp();
        Math::Vector3 right = GetRight();

        camera.PositionX = pos.x;
        camera.PositionY = pos.y;
        camera.PositionZ = pos.z;

        camera.ForwardX = forward.x;
        camera.ForwardY = forward.y;
        camera.ForwardZ = forward.z;

        camera.UpX = up.x;
        camera.UpY = up.y;
        camera.UpZ = up.z;

        camera.RightX = right.x;
        camera.RightY = right.y;
        camera.RightZ = right.z;
    }

    void MayaCameraController::SetOrbitSpeed(float speed)
    {
        m_OrbitSpeed = speed;
    }

    void MayaCameraController::SetPanSpeed(float speed)
    {
        m_PanSpeed = speed;
    }

    void MayaCameraController::SetDollySpeed(float speed)
    {
        m_DollySpeed = speed;
    }

    void MayaCameraController::SetScrollDollySpeed(float speed)
    {
        m_ScrollDollySpeed = speed;
    }

    void MayaCameraController::SetMinDistance(float minDistance)
    {
        m_MinDistance = minDistance;
    }

    void MayaCameraController::SetMaxDistance(float maxDistance)
    {
        m_MaxDistance = maxDistance;
    }

    void MayaCameraController::Orbit(float deltaX, float deltaY)
    {
        // マウスX移動 → Yaw回転（水平）
        m_Yaw -= deltaX * m_OrbitSpeed;

        // マウスY移動 → Pitch回転（垂直）
        m_Pitch -= deltaY * m_OrbitSpeed;

        // Pitchをクランプ（真上/真下の特異点回避）
        m_Pitch = std::clamp(m_Pitch, PITCH_MIN, PITCH_MAX);

        // Yawを0-360に正規化
        while (m_Yaw > 360.0f)
        {
            m_Yaw -= 360.0f;
        }
        while (m_Yaw < 0.0f)
        {
            m_Yaw += 360.0f;
        }

        RecalculatePosition();
    }

    void MayaCameraController::Pan(float deltaX, float deltaY)
    {
        // カメラのRight/Up方向に沿って平行移動
        Math::Vector3 right = GetRight();
        Math::Vector3 up = GetUp();

        // 距離に比例したPan量（遠いほど大きく動く）
        float panAmount = m_PanSpeed * m_Distance;

        // 注視点とカメラを同時に移動
        Math::Vector3 offset = right * (-deltaX * panAmount) + up * (deltaY * panAmount);
        m_Target += offset;

        RecalculatePosition();
    }

    void MayaCameraController::Dolly(float delta)
    {
        // 距離を変更（正:近づく、負:遠ざかる）
        m_Distance -= delta;

        // 距離をクランプ
        m_Distance = std::clamp(m_Distance, m_MinDistance, m_MaxDistance);

        RecalculatePosition();
    }

    void MayaCameraController::RecalculatePosition()
    {
        // 球面座標からカメラ位置を計算
        float yawRad = m_Yaw * Math::Constants::PI / 180.0f;
        float pitchRad = m_Pitch * Math::Constants::PI / 180.0f;

        // 球面座標 → デカルト座標
        // Y-up, 右手座標系
        float cosP = std::cos(pitchRad);
        float sinP = std::sin(pitchRad);
        float cosY = std::cos(yawRad);
        float sinY = std::sin(yawRad);

        m_Position.x = m_Target.x + m_Distance * cosP * sinY;
        m_Position.y = m_Target.y + m_Distance * sinP;
        m_Position.z = m_Target.z + m_Distance * cosP * cosY;
    }

} // namespace NorvesLib::Core::Input
