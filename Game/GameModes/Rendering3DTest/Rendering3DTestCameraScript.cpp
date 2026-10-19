#include "GameModes/Rendering3DTest/Rendering3DTestCameraScript.h"

#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Input/InputState.h"
#include "Core/Public/Input/MayaCameraController.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Debug/CameraScriptCapture.h"

namespace Game::GameModes
{

    namespace
    {
        // 台本の wait-ready で、落ち着かなくても先へ進むまでの Tick の数（Scenes/FreeCameraRig.cpp と同じ）
        constexpr uint32_t kWaitReadyTimeoutFrames = 3600;

        void LogPose(const char *phase, const NorvesLib::Core::Component::SpringArmComponent &springArm)
        {
            LOG_INFO_F("CAMERA_SCRIPT_ORBIT %s yaw=%.6f pitch=%.6f arm=%.6f", phase,
                       static_cast<double>(springArm.GetYaw()), static_cast<double>(springArm.GetPitch()),
                       static_cast<double>(springArm.GetArmLength()));
        }
    } // namespace

    bool Rendering3DTestCameraScript::Load(const char *scriptPath, Game::Debug::CameraScriptCapture *pScriptCapture,
                                           const NorvesLib::Core::Component::SpringArmComponent &springArm)
    {
        m_bActive = false;
        m_FramesPlayed = 0;
        m_bWaitingReady = false;
        m_WaitedFrames = 0;
        m_pScriptCapture = pScriptCapture;
        m_HomeYaw = springArm.GetYaw();
        m_HomePitch = springArm.GetPitch();
        m_HomeArmLength = springArm.GetArmLength();
        if (scriptPath == nullptr || scriptPath[0] == '\0')
        {
            return false;
        }
#if NORVES_BUILD_DEVELOPMENT
        // 読めなかったときは台本なしで続け、印をログに残す（撮影スクリプトが失敗にする）
        NorvesLib::Core::Container::String error;
        if (!m_Script.Load(scriptPath, error))
        {
            LOG_ERROR_F("CAMERA_SCRIPT_INVALID path=%s reason=%s", scriptPath, error.c_str());
            return false;
        }
        m_bActive = true;
        LOG_INFO_F("CAMERA_SCRIPT_LOADED path=%s steps=%zu frames=%u camera=orbit", scriptPath, m_Script.GetStepCount(),
                   m_Script.GetTotalFrames());
        if (m_Script.HasCaptureDirectives() && (m_pScriptCapture == nullptr || !m_pScriptCapture->IsConfigured()))
        {
            // 段取りは無視して入力だけを流す（撮影スクリプトはこの印で失敗にする）
            LOG_ERROR_F("CAMERA_SCRIPT_CAPTURE unavailable path=%s 撮影の段取り（home・epoch・capture・wait-ready）は "
                        "--capture-views で撮るシーンだけで使えます",
                        scriptPath);
        }
        LogPose("start", springArm);
        return true;
#else
        LOG_WARNING("--camera-script は開発ビルドだけで使えます。無視します");
        return false;
#endif
    }

    void Rendering3DTestCameraScript::Tick(const NorvesLib::Core::Input::MayaCameraController &controller,
                                           NorvesLib::Core::Component::SpringArmComponent &springArm)
    {
        if (!m_bActive)
        {
            return;
        }
        const bool bCaptureAvailable = m_pScriptCapture != nullptr && m_pScriptCapture->IsConfigured();
        if (m_bWaitingReady)
        {
            // wait-ready: 動かさずに、ストリーミングが落ち着くまで台本を止める（待ちが終わった Tick も動かさない）
            ++m_WaitedFrames;
            const bool bReady = bCaptureAvailable && m_pScriptCapture->IsReady();
            if (!bReady && bCaptureAvailable && m_WaitedFrames < kWaitReadyTimeoutFrames)
            {
                return;
            }
            m_bWaitingReady = false;
            LOG_INFO_F("CAMERA_SCRIPT_WAIT_READY %s waited=%u", bReady ? "ready" : "timeout", m_WaitedFrames);
        }
        else
        {
            NorvesLib::Core::Input::InputState scripted;
            Game::Input::CameraScriptFrameEvents events;
            if (m_Script.NextFrame(scripted, events))
            {
                if (events.bHome)
                {
                    springArm.SetYaw(m_HomeYaw);
                    springArm.SetPitch(m_HomePitch);
                    springArm.SetArmLength(m_HomeArmLength);
                }
                // 自由飛行カメラで視点を回す入力（右ボタン＋移動）を、軌道カメラで回す入力（Alt + 左ドラッグ）へ置き換える。
                // 右ボタンは下ろす（Alt + 右ドラッグは寄る入力になる）。ホイールはそのまま寄る入力になる
                using NorvesLib::Core::Input::KeyCode;
                using NorvesLib::Core::Input::MouseButton;
                const bool bRotate = scripted.IsMouseButtonDown(MouseButton::Right);
                NorvesLib::Core::Input::InputState orbitInput = scripted;
                orbitInput.SetKeyState(KeyCode::LeftAlt, bRotate);
                orbitInput.SetMouseButtonState(MouseButton::Left, bRotate);
                orbitInput.SetMouseButtonState(MouseButton::Right, false);
                springArm.ApplyIntent(controller.BuildIntent(orbitInput, kFrameSeconds, springArm.GetArmLength()));
                ++m_FramesPlayed;
                // エポックと撮影は、このフレームのカメラを渡す遅い更新（CameraLateUpdate）の前に頼む
                // （どちらもこのフレームのパケットに効く）
                if (events.bEpoch && bCaptureAvailable)
                {
                    m_pScriptCapture->BeginEpoch();
                }
                if (events.pCaptureTag != nullptr && bCaptureAvailable)
                {
                    springArm.RefreshOwnerTransform();
                    const NorvesLib::Core::Entity *pOwner = springArm.GetOwner();
                    const NorvesLib::Math::Vector3 position =
                        pOwner != nullptr ? pOwner->GetPosition() : NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f);
                    const float positionValues[3] = {position.x, position.y, position.z};
                    m_pScriptCapture->RequestCapture(*events.pCaptureTag, positionValues, springArm.GetYaw(),
                                                     springArm.GetPitch());
                }
                if (events.bWaitReadyAfter)
                {
                    m_bWaitingReady = true;
                    m_WaitedFrames = 0;
                }
            }
        }
        if (m_Script.IsFinished() && !m_bWaitingReady)
        {
            m_bActive = false;
            LogPose("end", springArm);
            LOG_INFO_F("CAMERA_SCRIPT_DONE frames=%u", m_FramesPlayed);
        }
    }

    void Rendering3DTestCameraScript::Stop()
    {
        m_bActive = false;
        m_bWaitingReady = false;
        m_pScriptCapture = nullptr;
    }

} // namespace Game::GameModes
