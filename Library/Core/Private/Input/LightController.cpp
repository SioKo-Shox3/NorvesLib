#include "Input/LightController.h"
#include "Math/MathTypes.h"
#include <cmath>
#include <algorithm>

namespace NorvesLib::Core::Input
{

    LightController::LightController()
        : m_TargetLight(nullptr), m_Yaw(45.0f), m_Pitch(-45.0f), m_RotationSpeed(90.0f), m_IntensitySpeed(1.0f), m_KeyYawNeg(KeyCode::Left), m_KeyYawPos(KeyCode::Right), m_KeyPitchNeg(KeyCode::Down), m_KeyPitchPos(KeyCode::Up), m_KeyIntensityUp(KeyCode::Equal), m_KeyIntensityDown(KeyCode::Minus)
    {
    }

    void LightController::SetTargetLight(Rendering::LightProxy *light)
    {
        m_TargetLight = light;
    }

    Rendering::LightProxy *LightController::GetTargetLight() const
    {
        return m_TargetLight;
    }

    void LightController::SetDirection(float yaw, float pitch)
    {
        m_Yaw = yaw;
        m_Pitch = pitch;
        RecalculateDirection();
    }

    void LightController::Update(const InputState &input, float deltaTime)
    {
        if (!m_TargetLight)
        {
            return;
        }

        float speedMultiplier = input.IsShiftDown() ? SHIFT_MULTIPLIER : 1.0f;
        float rotationAmount = m_RotationSpeed * deltaTime * speedMultiplier;
        float intensityAmount = m_IntensitySpeed * deltaTime * speedMultiplier;

        bool bDirectionChanged = false;

        // Yaw操作
        if (input.IsKeyDown(m_KeyYawNeg))
        {
            m_Yaw -= rotationAmount;
            bDirectionChanged = true;
        }
        if (input.IsKeyDown(m_KeyYawPos))
        {
            m_Yaw += rotationAmount;
            bDirectionChanged = true;
        }

        // Pitch操作
        if (input.IsKeyDown(m_KeyPitchNeg))
        {
            m_Pitch -= rotationAmount;
            bDirectionChanged = true;
        }
        if (input.IsKeyDown(m_KeyPitchPos))
        {
            m_Pitch += rotationAmount;
            bDirectionChanged = true;
        }

        // Pitchをクランプ
        m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);

        // Yawを正規化
        while (m_Yaw > 360.0f)
        {
            m_Yaw -= 360.0f;
        }
        while (m_Yaw < 0.0f)
        {
            m_Yaw += 360.0f;
        }

        // 方向を更新
        if (bDirectionChanged)
        {
            RecalculateDirection();
        }

        // 強度操作
        if (input.IsKeyDown(m_KeyIntensityUp))
        {
            m_TargetLight->Intensity += intensityAmount;
        }
        if (input.IsKeyDown(m_KeyIntensityDown))
        {
            m_TargetLight->Intensity -= intensityAmount;
            if (m_TargetLight->Intensity < 0.0f)
            {
                m_TargetLight->Intensity = 0.0f;
            }
        }
    }

    void LightController::SetRotationSpeed(float speed)
    {
        m_RotationSpeed = speed;
    }

    void LightController::SetIntensitySpeed(float speed)
    {
        m_IntensitySpeed = speed;
    }

    void LightController::SetRotationKeys(KeyCode yawNeg, KeyCode yawPos, KeyCode pitchNeg, KeyCode pitchPos)
    {
        m_KeyYawNeg = yawNeg;
        m_KeyYawPos = yawPos;
        m_KeyPitchNeg = pitchNeg;
        m_KeyPitchPos = pitchPos;
    }

    void LightController::SetIntensityKeys(KeyCode increase, KeyCode decrease)
    {
        m_KeyIntensityUp = increase;
        m_KeyIntensityDown = decrease;
    }

    float LightController::GetYaw() const
    {
        return m_Yaw;
    }

    float LightController::GetPitch() const
    {
        return m_Pitch;
    }

    void LightController::RecalculateDirection()
    {
        if (!m_TargetLight)
        {
            return;
        }

        float yawRad = m_Yaw * Math::Constants::PI / 180.0f;
        float pitchRad = m_Pitch * Math::Constants::PI / 180.0f;

        float cosP = std::cos(pitchRad);

        // ライトの方向ベクトル（ライトが照射する方向）
        m_TargetLight->DirectionX = cosP * std::sin(yawRad);
        m_TargetLight->DirectionY = std::sin(pitchRad);
        m_TargetLight->DirectionZ = cosP * std::cos(yawRad);
    }

} // namespace NorvesLib::Core::Input
