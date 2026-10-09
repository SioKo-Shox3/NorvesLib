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

    void FreeCameraRig::ConfigurePath(const ScenePathDescriptor *pPath, float startTimeSeconds)
    {
        m_Path.Clear();
        m_PathStartTime = -1.0f;
        m_bPathPlaying = false;
        m_PathTime = 0.0f;
        if (pPath == nullptr)
        {
            return;
        }
        if (!m_Path.Set(pPath->Keys, pPath->KeyCount, pPath->bLoop, pPath->LoopSeconds))
        {
            LOG_ERROR("カメラパスのキー列が不正です。パスなしで続けます");
            return;
        }
        if (startTimeSeconds >= 0.0f)
        {
            m_PathStartTime = startTimeSeconds;
        }
    }

    bool FreeCameraRig::Enter(GameModeContext &ctx, const FreeCameraPose &pose, const char *scriptPath)
    {
        m_Controller.Initialize(Math::Vector3(pose.Position[0], pose.Position[1], pose.Position[2]), pose.Yaw,
                                pose.Pitch);
        m_Collector.ResetAll();
        m_bScriptActive = false;
        m_ScriptFramesPlayed = 0;
        m_bPathPlaying = false;
        m_PathTime = 0.0f;
        if (m_PathStartTime >= 0.0f)
        {
            // 撮影用: 最初の視点の代わりに、パスの途中へ停止したまま置く
            m_PathTime = m_Path.WrapTime(m_PathStartTime);
            PlaceOnPath(m_PathTime);
            LOG_INFO_F("CAMERA_PATH state=placed time=%.3f", static_cast<double>(m_PathTime));
        }

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
            m_Collector.ConsumePathToggleRequest();
            m_Collector.ResetFrame();
        }
        else
        {
            const NorvesLib::Core::Input::InputState frame = m_Collector.BuildFrameInputState();
            const bool bToggle = m_Collector.ConsumePathToggleRequest();
            if (bToggle && m_Path.IsValid())
            {
                if (m_bPathPlaying)
                {
                    StopPath("toggle");
                }
                else
                {
                    StartPath();
                }
            }
            else if (m_bPathPlaying && m_Controller.HasMoveOrLookInput(frame))
            {
                // 自由飛行の入力が来たら止め、このフレームの入力は自由飛行として使う
                StopPath("input");
            }

            if (m_bPathPlaying)
            {
                m_PathTime += deltaTime;
                if (!m_Path.IsLoop() && m_PathTime >= m_Path.GetDuration())
                {
                    m_PathTime = m_Path.GetDuration();
                    PlaceOnPath(m_PathTime);
                    StopPath("end");
                }
                else
                {
                    m_PathTime = m_Path.WrapTime(m_PathTime);
                    PlaceOnPath(m_PathTime);
                }
            }
            else
            {
                m_Controller.Update(frame, deltaTime);
            }
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
        m_bPathPlaying = false;
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

    void FreeCameraRig::PlaceOnPath(float time)
    {
        Math::Vector3 position;
        Math::Vector3 target;
        if (!m_Path.Evaluate(time, position, target))
        {
            return;
        }
        float yaw = m_Controller.GetYaw();
        float pitch = m_Controller.GetPitch();
        // 位置と注視点が重なって向きが決まらないときは、今の向きのまま
        NorvesLib::Core::Input::CameraPath::LookAngles(position, target, yaw, pitch);
        m_Controller.SetPose(position, yaw, pitch);
    }

    void FreeCameraRig::StartPath()
    {
        if (!m_Path.IsLoop() && m_PathTime >= m_Path.GetDuration())
        {
            // 終わりまで再生したパスは、最初から
            m_PathTime = 0.0f;
        }
        m_bPathPlaying = true;
        LOG_INFO_F("CAMERA_PATH state=playing time=%.3f", static_cast<double>(m_PathTime));
    }

    void FreeCameraRig::StopPath(const char *reason)
    {
        m_bPathPlaying = false;
        const Math::Vector3 position = m_Controller.GetPosition();
        LOG_INFO_F("CAMERA_PATH state=stopped reason=%s time=%.3f position=(%.3f, %.3f, %.3f)", reason,
                   static_cast<double>(m_PathTime), static_cast<double>(position.x), static_cast<double>(position.y),
                   static_cast<double>(position.z));
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
