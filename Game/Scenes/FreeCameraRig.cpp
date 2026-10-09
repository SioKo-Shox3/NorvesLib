#include "Scenes/FreeCameraRig.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"


using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace Game::Scenes
{

    bool FreeCameraRig::Enter(GameModeContext &ctx, const FreeCameraPose &pose, const char *scriptPath)
    {
        m_Controller.Initialize(Math::Vector3(pose.Position[0], pose.Position[1], pose.Position[2]), pose.Yaw,
                                pose.Pitch);
        m_Collector.ResetAll();
        m_bScriptActive = false;
        m_ScriptFramesPlayed = 0;

        m_pCameraEntity = ctx.ScopeRef.SpawnObject<Entity>();
        if (m_pCameraEntity == nullptr)
        {
            return false;
        }
        m_pCamera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(m_pCameraEntity);
        if (m_pCamera == nullptr)
        {
            m_pCameraEntity = nullptr;
            return false;
        }
        m_pCamera->SetActiveCamera(true);

        // 台本は開発ビルドだけ。読めなかったときは台本なしで続け、印をログに残す（撮影スクリプトが失敗にする）。
        if (scriptPath != nullptr && scriptPath[0] != '\0')
        {
#if NORVES_BUILD_DEVELOPMENT
            NorvesLib::Core::Container::String error;
            if (m_Script.Load(scriptPath, error))
            {
                m_bScriptActive = true;
                LOG_INFO_F("CAMERA_SCRIPT_LOADED path=%s steps=%zu frames=%u", scriptPath, m_Script.GetStepCount(),
                           m_Script.GetTotalFrames());
            }
            else
            {
                LOG_ERROR_F("CAMERA_SCRIPT_INVALID path=%s reason=%s", scriptPath, error.c_str());
            }
#else
            LOG_WARNING("--camera-script は開発ビルドだけで使えます。無視します");
#endif
        }

        ctx.EngineRef.GetInputRouter().RegisterController(&m_Collector,
                                                          NorvesLib::Core::Input::InputRouter::PriorityGame);
        m_bInputRegistered = true;

        LogPose("start");
        Publish(ctx);
        return true;
    }

    void FreeCameraRig::Tick(GameModeContext &ctx, float deltaTime, bool bSuspended)
    {
        if (bSuspended)
        {
            // ポーズメニューの間は何も動かさない。押しっぱなしの状態も持ち越さない。
            m_Collector.ResetAll();
        }
        else if (m_bScriptActive)
        {
            NorvesLib::Core::Input::InputState scripted;
            if (m_Script.NextFrame(scripted))
            {
                m_Controller.Update(scripted, kScriptFrameSeconds);
                ++m_ScriptFramesPlayed;
            }
            if (m_Script.IsFinished())
            {
                m_bScriptActive = false;
                LogPose("end");
                LOG_INFO_F("CAMERA_SCRIPT_DONE frames=%u", m_ScriptFramesPlayed);
            }
            // 台本の間は実際の入力を使わない
            m_Collector.ResetFrame();
        }
        else
        {
            m_Controller.Update(m_Collector.BuildFrameInputState(), deltaTime);
            m_Collector.ResetFrame();
        }
        Publish(ctx);
    }

    void FreeCameraRig::Leave(GameModeContext &ctx)
    {
        if (m_bInputRegistered)
        {
            ctx.EngineRef.GetInputRouter().UnregisterController(&m_Collector);
            m_bInputRegistered = false;
        }
        m_Collector.ResetAll();
        m_bScriptActive = false;
        // 実体は GameModeScope::Cleanup が返す
        m_pCameraEntity = nullptr;
        m_pCamera = nullptr;
    }

    void FreeCameraRig::Publish(GameModeContext &ctx)
    {
        if (m_pCameraEntity == nullptr || m_pCamera == nullptr)
        {
            return;
        }
        // エンティティの姿勢は、カメラの部品の投影や露出の値を CameraProxy へ組み立てる元になる。
        // 位置と向きは、最後に自由飛行カメラの値で上書きして正本にする。
        const Math::Vector3 position = m_Controller.GetPosition();
        m_pCameraEntity->SetPosition(position.x, position.y, position.z);
        m_pCameraEntity->SetRotation(Math::QuaternionUtils::LookRotation(m_Controller.GetForward(), Math::Vector3::Up));
        ctx.WorldRef.UpdateWorldTransforms();

        CameraProxy proxy = m_ProxyTemplate;
        if (!m_pCamera->BuildCameraProxy(proxy))
        {
            return;
        }
        m_Controller.ApplyTo(proxy);
        ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
    }

    void FreeCameraRig::LogPose(const char *phase) const
    {
        const Math::Vector3 position = m_Controller.GetPosition();
        LOG_INFO_F("FREE_CAMERA_POSE phase=%s position=(%.3f, %.3f, %.3f) yaw=%.3f pitch=%.3f move_speed=%.3f", phase,
                   static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z),
                   static_cast<double>(m_Controller.GetYaw()), static_cast<double>(m_Controller.GetPitch()),
                   static_cast<double>(m_Controller.GetMoveSpeed()));
    }

} // namespace Game::Scenes
