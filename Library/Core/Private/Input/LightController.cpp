#include "Input/LightController.h"
#include "Component/LightComponent.h"
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

    void LightController::SetTargetComponent(Component::LightComponent *light)
    {
        m_TargetComponent = light;

        if (light == nullptr)
        {
            return;
        }

        // コンポーネントの現在方向から Yaw/Pitch を逆算し、初回入力での
        // 値ジャンプを防ぐ（RecalculateDirection の順方向と整合させる）。
        float dirX = 0.0f;
        float dirY = -1.0f;
        float dirZ = 0.0f;
        light->GetLightDirection(dirX, dirY, dirZ);

        float pitchRad = std::asin(std::clamp(dirY, -1.0f, 1.0f));
        float yawRad = std::atan2(dirX, dirZ);

        m_Pitch = std::clamp(pitchRad * 180.0f / Math::Constants::PI, -89.0f, 89.0f);
        m_Yaw = yawRad * 180.0f / Math::Constants::PI;
        while (m_Yaw > 360.0f)
        {
            m_Yaw -= 360.0f;
        }
        while (m_Yaw < 0.0f)
        {
            m_Yaw += 360.0f;
        }
    }

    Component::LightComponent *LightController::GetTargetComponent() const
    {
        return m_TargetComponent;
    }

    void LightController::SetDirection(float yaw, float pitch)
    {
        m_Yaw = yaw;
        m_Pitch = pitch;
        RecalculateDirection();
    }

    bool LightController::IsBoundKey(KeyCode code) const
    {
        return code == m_KeyYawNeg || code == m_KeyYawPos ||
               code == m_KeyPitchNeg || code == m_KeyPitchPos ||
               code == m_KeyIntensityUp || code == m_KeyIntensityDown ||
               code == KeyCode::LeftShift || code == KeyCode::RightShift;
    }

    bool LightController::OnKey(const KeyEvent &event)
    {
        // 自分のバインドキー以外は透過させる（下位コントローラへ）。
        if (!IsBoundKey(event.Code))
        {
            return false;
        }

        // Pressed/Repeat を押下、Released を解放として held フラグへ反映する。
        const bool bDown = (event.Action != InputAction::Released);

        if (event.Code == KeyCode::LeftShift || event.Code == KeyCode::RightShift)
        {
            m_bHoldShift = bDown;
        }
        if (event.Code == m_KeyYawNeg)
        {
            m_bHoldYawNeg = bDown;
        }
        if (event.Code == m_KeyYawPos)
        {
            m_bHoldYawPos = bDown;
        }
        if (event.Code == m_KeyPitchNeg)
        {
            m_bHoldPitchNeg = bDown;
        }
        if (event.Code == m_KeyPitchPos)
        {
            m_bHoldPitchPos = bDown;
        }
        if (event.Code == m_KeyIntensityUp)
        {
            m_bHoldIntensityUp = bDown;
        }
        if (event.Code == m_KeyIntensityDown)
        {
            m_bHoldIntensityDown = bDown;
        }

        // 自バインドキーは consume（上位の UI 層が先に consume するため、
        // ここに届く時点でゲームへの入力として扱う）。
        return true;
    }

    void LightController::Update(float deltaTime)
    {
        // 対象（Component 優先・なければ Proxy）が無ければ何もしない。
        if (m_TargetComponent == nullptr && m_TargetLight == nullptr)
        {
            return;
        }

        float speedMultiplier = m_bHoldShift ? SHIFT_MULTIPLIER : 1.0f;
        float rotationAmount = m_RotationSpeed * deltaTime * speedMultiplier;
        float intensityAmount = m_IntensitySpeed * deltaTime * speedMultiplier;

        bool bDirectionChanged = false;

        // Yaw操作
        if (m_bHoldYawNeg)
        {
            m_Yaw -= rotationAmount;
            bDirectionChanged = true;
        }
        if (m_bHoldYawPos)
        {
            m_Yaw += rotationAmount;
            bDirectionChanged = true;
        }

        // Pitch操作
        if (m_bHoldPitchNeg)
        {
            m_Pitch -= rotationAmount;
            bDirectionChanged = true;
        }
        if (m_bHoldPitchPos)
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
        if (m_bHoldIntensityUp || m_bHoldIntensityDown)
        {
            float intensity = GetCurrentIntensity();
            if (m_bHoldIntensityUp)
            {
                intensity += intensityAmount;
            }
            if (m_bHoldIntensityDown)
            {
                intensity -= intensityAmount;
            }
            if (intensity < 0.0f)
            {
                intensity = 0.0f;
            }
            ApplyIntensity(intensity);
        }
    }

    float LightController::GetCurrentIntensity() const
    {
        if (m_TargetComponent != nullptr)
        {
            return m_TargetComponent->GetIntensity();
        }
        if (m_TargetLight != nullptr)
        {
            return m_TargetLight->Intensity;
        }
        return 0.0f;
    }

    void LightController::ApplyIntensity(float intensity)
    {
        if (m_TargetComponent != nullptr)
        {
            m_TargetComponent->SetIntensity(intensity);
            return;
        }
        if (m_TargetLight != nullptr)
        {
            m_TargetLight->Intensity = intensity;
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
        if (m_TargetComponent == nullptr && m_TargetLight == nullptr)
        {
            return;
        }

        float yawRad = m_Yaw * Math::Constants::PI / 180.0f;
        float pitchRad = m_Pitch * Math::Constants::PI / 180.0f;

        float cosP = std::cos(pitchRad);

        // ライトの方向ベクトル（ライトが照射する方向）
        const float dirX = cosP * std::sin(yawRad);
        const float dirY = std::sin(pitchRad);
        const float dirZ = cosP * std::cos(yawRad);

        // Component 優先。なければ後方互換で Proxy へ直接書く。
        if (m_TargetComponent != nullptr)
        {
            m_TargetComponent->SetLightDirection(dirX, dirY, dirZ);
            return;
        }

        m_TargetLight->DirectionX = dirX;
        m_TargetLight->DirectionY = dirY;
        m_TargetLight->DirectionZ = dirZ;
    }

} // namespace NorvesLib::Core::Input
