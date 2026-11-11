#include "Scenes/FreeCameraRig.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Debug/CameraScriptCapture.h"
#include "Debug/MapCapture.h"
#include "Scenes/SceneViewSwitch.h"


#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
#if NORVES_BUILD_DEVELOPMENT
        m_pPathCapture = nullptr;
        m_PathCaptureTime = -1.0;
        m_PathCaptureTick = 0;
        m_PathCaptureTargetTick = 0;
#endif
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

#if NORVES_BUILD_DEVELOPMENT
    void FreeCameraRig::ConfigurePath(const ScenePathDescriptor *pPath, float startTimeSeconds, bool bPlayWhenReady,
                                      double captureTimeSeconds, CameraPathCaptureState *pCaptureState)
    {
        ConfigurePath(pPath, startTimeSeconds, bPlayWhenReady);
        if (captureTimeSeconds < 0.0)
        {
            return;
        }
        if (!pCaptureState || !m_Path.IsValid() || !bPlayWhenReady ||
            captureTimeSeconds < pCaptureState->StartTimeSeconds)
        {
            LOG_ERROR("CAMERA_PATH_CAPTURE 撮影に必要なパス・開始時刻・再生の指定が不正です");
            if (pCaptureState)
            {
                pCaptureState->bFinished = true;
            }
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
            return;
        }
        if (!m_Path.IsLoop() && captureTimeSeconds > static_cast<double>(m_Path.GetDuration()))
        {
            LOG_ERROR("CAMERA_PATH_CAPTURE 指定時刻がループしないパスの長さを超えています");
            pCaptureState->bFinished = true;
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
            return;
        }
        const double targetTick = std::ceil((captureTimeSeconds - pCaptureState->StartTimeSeconds) * 60.0);
        if (!std::isfinite(targetTick) || targetTick >= static_cast<double>(std::numeric_limits<uint64_t>::max()))
        {
            LOG_ERROR("CAMERA_PATH_CAPTURE 撮影までの Tick 数が大きすぎます");
            pCaptureState->bFinished = true;
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
            return;
        }
        m_pPathCapture = pCaptureState;
        m_PathCaptureTime = captureTimeSeconds;
        m_PathCaptureTargetTick = static_cast<uint64_t>(targetTick);
    }
#endif

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
        if (m_pMapCapture != nullptr && m_pMapCapture->IsControllingCamera())
        {
            // 撮影中の入力や押しっぱなしの状態を持ち越さない。
            m_Collector.ResetAll();
            Publish(ctx);
            return;
        }
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
#if NORVES_BUILD_DEVELOPMENT
            if (bToggle && m_pPathCapture && !m_pPathCapture->bFinished && m_bPlayWhenReady)
            {
                LOG_WARNING("CAMERA_PATH_CAPTURE シーンの準備が終わるまで再生操作を無視します");
            }
            else
#endif
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
#if NORVES_BUILD_DEVELOPMENT
                if (m_pPathCapture && !m_pPathCapture->bFinished)
                {
                    const double seconds = std::max(
                        m_pPathCapture->StartTimeSeconds,
                        m_PathCaptureTime + (static_cast<double>(m_PathCaptureTick) -
                                             static_cast<double>(m_PathCaptureTargetTick)) / 60.0);
                    m_PathTime = m_Path.WrapTime(static_cast<float>(seconds));
                    PlaceOnPath(m_PathTime);
                    ++m_PathPlayedFrames;
                    if (m_PathCaptureTick <= m_PathCaptureTargetTick &&
                        m_PathCaptureTargetTick - m_PathCaptureTick <= 4)
                    {
                        LOG_INFO_F("CAMERA_PATH_CAPTURE_TICK tick=%llu time=%.6f",
                                   static_cast<unsigned long long>(m_PathCaptureTick), seconds);
                    }
                    if (m_PathCaptureTick == m_PathCaptureTargetTick)
                    {
                        // この Tick の Publish と要求は、直後の BeginFrame / EndFrame の同じパケットに入る。
                        FrameCaptureRequest request;
                        request.SourceKind = FrameCaptureSourceKind::BackBuffer;
                        if (!ctx.EngineRef.GetRenderWorld().RequestFrameCapture(request).IsAccepted())
                        {
                            LOG_ERROR("CAMERA_PATH_CAPTURE 撮影の要求が拒否されました");
                            m_pPathCapture->bFinished = true;
                            NorvesLib::Core::Engine::GEngine->RequestExit(1);
                        }
                        else
                        {
                            m_pPathCapture->bWaitingResult = true;
                        }
                    }
                    else if (m_PathCaptureTick > m_PathCaptureTargetTick && !m_Path.IsLoop() &&
                             seconds >= static_cast<double>(m_Path.GetDuration()))
                    {
                        StopPath("end");
                    }
                    ++m_PathCaptureTick;
                }
                else
#endif
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
        if (m_pMapCapture != nullptr && m_pMapCapture->IsControllingCamera())
        {
            m_pMapCapture->ApplyToCamera(*m_pCameraEntity, *m_pCamera);
            m_Controller.SetPose(m_pCameraEntity->GetWorldTransform().position, m_Controller.GetYaw(),
                                 m_Controller.GetPitch());
            ctx.WorldRef.UpdateWorldTransforms();

            CameraProxy proxy = m_ProxyTemplate;
            if (!m_pCamera->BuildCameraProxy(proxy))
            {
                return;
            }
            m_pMapCapture->ApplyToProxy(proxy);
            ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
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
        if (!m_Path.IsLoop() && m_PathTime >= m_Path.GetDuration()
#if NORVES_BUILD_DEVELOPMENT
            && !(m_pPathCapture && !m_pPathCapture->bFinished)
#endif
        )
        {
            // 終わりまで再生したパスは、最初から
            m_PathTime = 0.0f;
        }
        m_bPathPlaying = true;
        LOG_INFO_F("CAMERA_PATH state=playing time=%.3f", static_cast<double>(m_PathTime));
    }

    void FreeCameraRig::StopPath(const char *reason)
    {
#if NORVES_BUILD_DEVELOPMENT
        if (m_pPathCapture && !m_pPathCapture->bFinished &&
            (std::strcmp(reason, "input") == 0 || std::strcmp(reason, "toggle") == 0))
        {
            LOG_ERROR("CAMERA_PATH_CAPTURE 入力でパスが停止したため撮影を中止します");
            m_pPathCapture->bFinished = true;
            m_pPathCapture->bWaitingResult = false;
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
        }
#endif
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
