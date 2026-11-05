// 共通カメラを Scope で所有し、積算した時間から鉛直軸の周回姿勢を描画へ渡す。
#include "GameModes/ShishaShowcase/ShishaShowcaseCamera.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"

#include <cmath>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    namespace Math = NorvesLib::Math;

    bool ShishaShowcaseCamera::Enter(GameMode::GameModeContext& ctx)
    {
        m_pEntity = ctx.ScopeRef.SpawnObject<Entity>();
        if (m_pEntity == nullptr)
        {
            return false;
        }
        m_pCamera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(m_pEntity);
        if (m_pCamera == nullptr)
        {
            return false;
        }
        m_pCamera->SetActiveCamera(true);
        m_pCamera->SetExposureMode(Rendering::CameraExposureMode::Manual);
        m_pCamera->SetAperture(16.0f);
        m_pCamera->SetISO(100.0f);
        m_pCamera->SetShutterSpeed(ComputeShutterSpeedForEV100(
            m_pCamera->GetAperture(), m_pCamera->GetISO(), 15.0f));
        m_pCamera->SetExposureCompensation(0.0f);
        m_pCamera->SetAntiAliasingMode(Rendering::CameraAntiAliasingMode::FXAA);
        m_pCamera->SetFieldOfView(50.0f);
        return true;
    }

    void ShishaShowcaseCamera::SetView(const Math::Vector3& eye, const Math::Vector3& target)
    {
        m_Eye = eye;
        m_Target = target;
        if (m_pCamera != nullptr)
        {
            // 被写体と一緒に距離が変わっても、手前と奥のクリップ面が全身を切らないようにする。
            const float distance = (eye - target).Length();
            m_pCamera->SetNearPlane(distance * 0.002f);
            m_pCamera->SetFarPlane(distance * 1000.0f);
        }
    }

    void ShishaShowcaseCamera::Update(GameMode::GameModeContext& ctx, double elapsedSeconds,
                                     float orbitDegreesPerSecond)
    {
        if (m_pEntity == nullptr || m_pCamera == nullptr)
        {
            return;
        }
        const double angle = std::fmod(elapsedSeconds * static_cast<double>(orbitDegreesPerSecond), 360.0) *
                             (3.14159265358979323846 / 180.0);
        const float cosine = static_cast<float>(std::cos(angle));
        const float sine = static_cast<float>(std::sin(angle));
        const Math::Vector3 offset = m_Eye - m_Target;
        const Math::Vector3 eye = m_Target + Math::Vector3(offset.x * cosine + offset.z * sine, offset.y,
                                                         -offset.x * sine + offset.z * cosine);
        m_pEntity->SetPosition(eye.x, eye.y, eye.z);
        m_pEntity->SetRotation(Math::QuaternionUtils::LookRotation(m_Target - eye, Math::Vector3::Up));
        ctx.WorldRef.UpdateWorldTransforms();
        Rendering::CameraProxy proxy;
        if (m_pCamera->BuildCameraProxy(proxy))
        {
            ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
        }
    }

    void ShishaShowcaseCamera::Leave()
    {
        m_pCamera = nullptr;
        m_pEntity = nullptr;
    }
} // namespace Game::GameModes
