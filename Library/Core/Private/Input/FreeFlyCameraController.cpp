#include "Input/FreeFlyCameraController.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Input
{

    namespace
    {
        constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

        /// Yaw を -180〜180 度に収める
        float WrapDegrees(float degrees)
        {
            float wrapped = std::fmod(degrees, 360.0f);
            if (wrapped > 180.0f)
            {
                wrapped -= 360.0f;
            }
            else if (wrapped <= -180.0f)
            {
                wrapped += 360.0f;
            }
            return wrapped;
        }

        float KeyAxis(const InputState &input, KeyCode positive, KeyCode negative)
        {
            return (input.IsKeyDown(positive) ? 1.0f : 0.0f) - (input.IsKeyDown(negative) ? 1.0f : 0.0f);
        }
    } // namespace

    void FreeFlyCameraController::Initialize(const Math::Vector3 &position, float yawDegrees, float pitchDegrees)
    {
        m_Position = position;
        m_Yaw = WrapDegrees(yawDegrees);
        m_Pitch = std::clamp(pitchDegrees, -PitchLimitDegrees, PitchLimitDegrees);
        m_MoveSpeed = std::clamp(m_Settings.InitialMoveSpeed, m_Settings.MinMoveSpeed, m_Settings.MaxMoveSpeed);
    }

    void FreeFlyCameraController::SetSettings(const FreeFlyCameraSettings &settings)
    {
        m_Settings = settings;
        m_MoveSpeed = std::clamp(m_MoveSpeed, m_Settings.MinMoveSpeed, m_Settings.MaxMoveSpeed);
    }

    float FreeFlyCameraController::ApplyStickDeadZone(float value) const
    {
        const float deadZone = std::clamp(m_Settings.StickDeadZone, 0.0f, 0.99f);
        const float magnitude = std::fabs(value);
        if (magnitude <= deadZone)
        {
            return 0.0f;
        }
        const float scaled = std::min((magnitude - deadZone) / (1.0f - deadZone), 1.0f);
        return value < 0.0f ? -scaled : scaled;
    }

    float FreeFlyCameraController::GetPadAxis(const InputState &input, GamepadAxis axis) const
    {
        float sum = 0.0f;
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            sum += ApplyStickDeadZone(input.GetGamepadAxis(slot, axis));
        }
        return std::clamp(sum, -1.0f, 1.0f);
    }

    float FreeFlyCameraController::GetPadTrigger(const InputState &input, GamepadTrigger trigger) const
    {
        const float deadZone = std::clamp(m_Settings.TriggerDeadZone, 0.0f, 0.99f);
        float sum = 0.0f;
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            const float value = input.GetGamepadTrigger(slot, trigger);
            if (value > deadZone)
            {
                sum += (value - deadZone) / (1.0f - deadZone);
            }
        }
        return std::clamp(sum, 0.0f, 1.0f);
    }

    void FreeFlyCameraController::Update(const InputState &input, float deltaTime)
    {
        const MouseState &mouse = input.GetMouseState();

        // 視点: 右ボタンを押している間のマウスの移動量と、右スティック。画面の下方向への移動（DeltaY が正）で下を向く。
        if (input.IsMouseButtonDown(MouseButton::Right))
        {
            m_Yaw += mouse.DeltaX * m_Settings.LookDegreesPerPixel;
            m_Pitch -= mouse.DeltaY * m_Settings.LookDegreesPerPixel;
        }
        const float stickLook = m_Settings.StickLookDegreesPerSecond * deltaTime;
        m_Yaw += GetPadAxis(input, GamepadAxis::RightX) * stickLook;
        m_Pitch += GetPadAxis(input, GamepadAxis::RightY) * stickLook;
        m_Yaw = WrapDegrees(m_Yaw);
        m_Pitch = std::clamp(m_Pitch, -PitchLimitDegrees, PitchLimitDegrees);

        // 移動の速さ: ホイール1段ごとに倍率を掛ける
        if (mouse.ScrollDelta != 0.0f)
        {
            m_MoveSpeed *= std::pow(m_Settings.ScrollSpeedFactor, mouse.ScrollDelta);
            m_MoveSpeed = std::clamp(m_MoveSpeed, m_Settings.MinMoveSpeed, m_Settings.MaxMoveSpeed);
        }

        // 移動: 前後左右は視線の向き（前後は上下の傾きを含む）、上下はワールドの上方向
        float forwardInput = KeyAxis(input, KeyCode::W, KeyCode::S) + GetPadAxis(input, GamepadAxis::LeftY);
        float rightInput = KeyAxis(input, KeyCode::D, KeyCode::A) + GetPadAxis(input, GamepadAxis::LeftX);
        float upInput = KeyAxis(input, KeyCode::E, KeyCode::Q) +
                        GetPadTrigger(input, GamepadTrigger::Right) - GetPadTrigger(input, GamepadTrigger::Left);
        forwardInput = std::clamp(forwardInput, -1.0f, 1.0f);
        rightInput = std::clamp(rightInput, -1.0f, 1.0f);
        upInput = std::clamp(upInput, -1.0f, 1.0f);

        Math::Vector3 move = GetForward() * forwardInput + GetRight() * rightInput + Math::Vector3(0.0f, upInput, 0.0f);
        const float length = std::sqrt(move.x * move.x + move.y * move.y + move.z * move.z);
        if (length > 1.0f)
        {
            move = move * (1.0f / length);
        }

        bool bSprint = input.IsShiftDown();
        for (uint8_t slot = 0; slot < GamepadSlotCount && !bSprint; ++slot)
        {
            bSprint = input.IsGamepadButtonDown(slot, GamepadButton::LeftThumb);
        }
        const float speed = m_MoveSpeed * (bSprint ? m_Settings.SprintMultiplier : 1.0f);
        m_Position += move * (speed * deltaTime);
    }

    Math::Vector3 FreeFlyCameraController::ForwardFromAngles(float yawDegrees, float pitchDegrees)
    {
        const float yaw = yawDegrees * kDegreesToRadians;
        const float pitch = pitchDegrees * kDegreesToRadians;
        return Math::Vector3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    }

    Math::Vector3 FreeFlyCameraController::GetForward() const
    {
        return ForwardFromAngles(m_Yaw, m_Pitch);
    }

    Math::Vector3 FreeFlyCameraController::GetRight() const
    {
        const float yaw = m_Yaw * kDegreesToRadians;
        return Math::Vector3(-std::cos(yaw), 0.0f, -std::sin(yaw));
    }

    Math::Vector3 FreeFlyCameraController::GetUp() const
    {
        const float yaw = m_Yaw * kDegreesToRadians;
        const float pitch = m_Pitch * kDegreesToRadians;
        return Math::Vector3(std::sin(pitch) * std::sin(yaw), std::cos(pitch), -std::sin(pitch) * std::cos(yaw));
    }

    void FreeFlyCameraController::ApplyTo(Rendering::CameraProxy &camera) const
    {
        const Math::Vector3 forward = GetForward();
        const Math::Vector3 up = GetUp();
        const Math::Vector3 right = GetRight();

        camera.PositionX = m_Position.x;
        camera.PositionY = m_Position.y;
        camera.PositionZ = m_Position.z;

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

} // namespace NorvesLib::Core::Input
