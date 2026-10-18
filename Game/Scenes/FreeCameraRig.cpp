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
#include "Debug/CameraScriptCapture.h"
#include "Scenes/SceneViewSwitch.h"


#include <algorithm>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace Game::Scenes
{

    namespace
    {
        // 再生中のパスの時刻をログへ出す間隔（Tick の数。60 fps で約 1 秒）
        constexpr uint32_t kPathTimeLogFrames = 60;
        // 台本の wait-ready で、落ち着かなくても先へ進むまでの Tick の数（落ち着かなかった印をログに残す）
        constexpr uint32_t kScriptWaitReadyTimeoutFrames = 3600;
    } // namespace

    void FreeCameraRig::ConfigurePath(const ScenePathDescriptor *pPath, float startTimeSeconds, bool bPlayWhenReady)
    {
        m_Path.Clear();
        m_PathStartTime = -1.0f;
        m_bPathPlaying = false;
        m_PathTime = 0.0f;
        m_bPlayWhenReady = false;
        m_PathPlayedFrames = 0;
        if (pPath == nullptr)
        {
            if (bPlayWhenReady)
            {
                LOG_WARNING("--camera-path-play: このシーンにはカメラパスがありません。再生せずに続けます");
            }
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
        if (bPlayWhenReady)
        {
            // 組み立ての間は、再生を始める時刻の位置に止めて置く
            m_bPlayWhenReady = true;
            m_PathStartTime = std::max(m_PathStartTime, 0.0f);
        }
    }

    bool FreeCameraRig::Enter(GameModeContext &ctx, const FreeCameraPose &pose, const char *scriptPath)
    {
        m_Controller.Initialize(Math::Vector3(pose.Position[0], pose.Position[1], pose.Position[2]), pose.Yaw,
                                pose.Pitch);
        m_Collector.ResetAll();
        m_bScriptActive = false;
        m_ScriptFramesPlayed = 0;
        m_HomePose = pose;
        m_bScriptWaitingReady = false;
        m_ScriptWaitedFrames = 0;
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
                if (m_Script.HasCaptureDirectives() &&
                    (m_pScriptCapture == nullptr || !m_pScriptCapture->IsConfigured()))
                {
                    // 段取りは無視して入力だけを流す（撮影スクリプトはこの印で失敗にする）
                    LOG_ERROR_F("CAMERA_SCRIPT_CAPTURE unavailable path=%s 撮影の段取り（home・epoch・capture・wait-ready）は "
                                "--capture-views で撮るシーンだけで使えます",
                                scriptPath);
                }
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
            TickScript();
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
                ++m_PathPlayedFrames;
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
                if (m_PathPlayedFrames % kPathTimeLogFrames == 0)
                {
                    LOG_INFO_F("CAMERA_PATH_TIME time=%.3f frames=%u", static_cast<double>(m_PathTime), m_PathPlayedFrames);
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
        m_bScriptWaitingReady = false;
        m_bPathPlaying = false;
        m_bPlayWhenReady = false;
        // 実体は GameModeScope::Cleanup が返す
        m_pCameraEntity = nullptr;
        m_pCamera = nullptr;
    }

    void FreeCameraRig::PlaceAt(const FreeCameraPose &pose)
    {
        if (m_bPathPlaying)
        {
            StopPath("view");
        }
        m_Controller.Initialize(Math::Vector3(pose.Position[0], pose.Position[1], pose.Position[2]), pose.Yaw,
                                pose.Pitch);
        m_HomePose = pose;
        m_Collector.ResetAll();
        LogPose("view");
    }

    void FreeCameraRig::TickScript()
    {
        const bool bCaptureAvailable = m_pScriptCapture != nullptr && m_pScriptCapture->IsConfigured();
        if (m_bScriptWaitingReady)
        {
            // wait-ready: 動かさずに、ストリーミングが落ち着くまで台本を止める（待ちが終わった Tick も動かさない）
            ++m_ScriptWaitedFrames;
            const bool bReady = bCaptureAvailable && m_pScriptCapture->IsReady();
            if (!bReady && bCaptureAvailable && m_ScriptWaitedFrames < kScriptWaitReadyTimeoutFrames)
            {
                return;
            }
            m_bScriptWaitingReady = false;
            LOG_INFO_F("CAMERA_SCRIPT_WAIT_READY %s waited=%u", bReady ? "ready" : "timeout", m_ScriptWaitedFrames);
        }
        else
        {
            NorvesLib::Core::Input::InputState scripted;
            Game::Input::CameraScriptFrameEvents events;
            if (m_Script.NextFrame(scripted, events))
            {
                if (events.bHome)
                {
                    m_Controller.SetPose(
                        Math::Vector3(m_HomePose.Position[0], m_HomePose.Position[1], m_HomePose.Position[2]),
                        m_HomePose.Yaw, m_HomePose.Pitch);
                }
                m_Controller.Update(scripted, kScriptFrameSeconds);
                ++m_ScriptFramesPlayed;
                // エポックと撮影は、このフレームのカメラを渡す Publish の前に頼む（どちらもこのフレームのパケットに効く）
                if (events.bEpoch && bCaptureAvailable)
                {
                    m_pScriptCapture->BeginEpoch();
                }
                if (events.pCaptureTag != nullptr && bCaptureAvailable)
                {
                    const Math::Vector3 position = m_Controller.GetPosition();
                    const float positionValues[3] = {position.x, position.y, position.z};
                    m_pScriptCapture->RequestCapture(*events.pCaptureTag, positionValues, m_Controller.GetYaw(),
                                                     m_Controller.GetPitch());
                }
                if (events.bWaitReadyAfter)
                {
                    m_bScriptWaitingReady = true;
                    m_ScriptWaitedFrames = 0;
                }
            }
        }
        if (m_Script.IsFinished() && !m_bScriptWaitingReady)
        {
            m_bScriptActive = false;
            LogPose("end");
            LOG_INFO_F("CAMERA_SCRIPT_DONE frames=%u", m_ScriptFramesPlayed);
        }
    }

    void FreeCameraRig::ApplyRequestedView(SceneViewSwitch *pViewSwitch, const SceneDescriptor &scene)
    {
        if (pViewSwitch == nullptr || !pViewSwitch->IsRequested())
        {
            return;
        }
        const SceneViewDescriptor *view = FindSceneView(scene, pViewSwitch->GetViewName().c_str());
        if (view == nullptr || view->Kind != SceneViewKind::FreePose)
        {
            LOG_ERROR_F("SCENE_VIEW_UNKNOWN scene=%s view=\"%s\" 自由飛行の視点に置き直せません", scene.Id,
                        pViewSwitch->GetViewName().c_str());
            pViewSwitch->Complete(false);
            return;
        }
        FreeCameraPose pose{};
        pose.Position[0] = view->Position[0];
        pose.Position[1] = view->Position[1];
        pose.Position[2] = view->Position[2];
        pose.Yaw = view->Yaw;
        pose.Pitch = view->Pitch;
        PlaceAt(pose);
        LOG_INFO_F("SCENE_VIEW_SWITCHED scene=%s view=%s", scene.Id, pViewSwitch->GetViewName().c_str());
        pViewSwitch->Complete(true);
    }

    void FreeCameraRig::NotifySceneReady()
    {
        if (!m_bPlayWhenReady)
        {
            return;
        }
        m_bPlayWhenReady = false;
        if (m_Path.IsValid() && !m_bPathPlaying)
        {
            m_PathPlayedFrames = 0;
            StartPath();
        }
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
