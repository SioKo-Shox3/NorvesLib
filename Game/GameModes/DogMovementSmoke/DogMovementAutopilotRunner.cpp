#include "DogMovementAutopilotRunner.h"
#include "DogMovementSmokeMode.h"
#include "Application/IWindow.h"
#include "Asset/AssetFileReader.h"
#include "Component/CameraComponent.h"
#include "Debug/CapturedFramePng.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/Camera/FollowCameraComponent.h"
#include "Gameplay/Locomotion/QuadrupedLocomotionComponent.h"
#include "Input/InputSystem.h"
#include "Locomotion/QuadrupedLocomotionJson.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/CharacterBodyComponent.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderWorld.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    namespace Course = DogMovementAutopilot;
    namespace M = NorvesLib::Math;
    namespace P = NorvesLib::Modules::Physics;
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float MouseSensitivity = .15f;
        // PhysicsModuleCharacterがCharacterBodyに適用する重力の基準値。
        constexpr float CharacterGravity = 9.81f;

        double InputTime()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        double MarkerNumber(float value)
        {
            return std::round(static_cast<double>(value) * 10000.0) / 10000.0;
        }
        bool WithinTolerance(float measured, float expected)
        {
            if (!std::isfinite(measured) || !std::isfinite(expected) || expected <= 0 || measured < 0)
                return false;
            const double visibleMeasured = MarkerNumber(measured), visibleExpected = MarkerNumber(expected);
            return std::fabs(measured - expected) <= std::max(.15f, .2f * expected) &&
                   std::fabs(visibleMeasured - visibleExpected) <= std::max(.15, .2 * visibleExpected);
        }
        float HorizontalLength(const M::Vector3& vector)
        {
            return std::hypot(vector.x, vector.z);
        }
        M::Vector3 PlatformLocal(const Entity& platform, const M::Vector3& position)
        {
            const auto rotation = platform.GetRotation();
            return M::Quaternion(-rotation.x, -rotation.y, -rotation.z, rotation.w) * (position - platform.GetPosition());
        }
        const char* CaptureName(Course::AutopilotStage stage)
        {
            switch (stage)
            {
            case Course::AutopilotStage::CameraLow: return "low";
            case Course::AutopilotStage::CameraSprint: return "sprint";
            case Course::AutopilotStage::CameraWall: return "wall";
            default: return nullptr;
            }
        }
    } // namespace

    bool DogMovementAutopilotRunner::Initialize(GameMode::GameModeContext& ctx, DogMovementSmokeData& data)
    {
        Asset::AssetFileReader reader;
        const auto asset = reader.Read("Gameplay/DogLocomotion.json");
        Container::String error;
        if (!asset.Succeeded() || !Locomotion::ParseQuadrupedLocomotionJson(asset.Blob.GetSpan(), m_Params, error))
        {
            Fail(ctx, data, "locomotion_profile");
            return false;
        }
        // 自動走行の合成入力を背景ウィンドウでもMapperへ届ける。OSのフォーカスは変更しない。
        auto& mapper = ctx.EngineRef.GetInputMapper();
        if (!mapper.IsFocused())
        {
            mapper.SetFocused(true);
            m_bOverrideFocus = true;
        }
        NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=autopilot segments=%zu source=%s",
                        data.Script.Segments.size(), data.AutopilotPath.c_str());
        return true;
    }

    void DogMovementAutopilotRunner::ReleaseInputs(GameMode::GameModeContext& ctx)
    {
        auto& input = ctx.EngineRef.GetInputSystem();
        const auto mouse = input.GetState().GetMouseState();
        for (size_t i = 0; i < static_cast<size_t>(Input::KeyCode::Count); ++i)
        {
            if (m_Keys[i])
                input.InjectKeyEvent(static_cast<Input::KeyCode>(i), Input::InputAction::Released);
            m_Keys[i] = false;
        }
        for (size_t i = 0; i < static_cast<size_t>(Input::MouseButton::Count); ++i)
        {
            if (m_Buttons[i])
                input.InjectMouseButton(static_cast<Input::MouseButton>(i), Input::InputAction::Released,
                                        mouse.PositionX, mouse.PositionY);
            m_Buttons[i] = false;
        }
    }

    void DogMovementAutopilotRunner::Stop(GameMode::GameModeContext& ctx)
    {
        ReleaseInputs(ctx);
        if (m_bOverrideFocus)
        {
            auto* window = ctx.EngineRef.GetMainWindow();
            ctx.EngineRef.GetInputMapper().SetFocused(window && window->IsInputFocused());
            m_bOverrideFocus = false;
        }
    }

    void DogMovementAutopilotRunner::Fail(GameMode::GameModeContext& ctx, DogMovementSmokeData& data,
                                          const char* reason)
    {
        if (m_Phase == Phase::Finished)
            return;
        ReleaseInputs(ctx);
        if (data.LateState)
            data.LateState->TrackingAutopilotHitstop = false;
        m_Phase = Phase::Finished;
        NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=failed reason=%s", reason);
        ctx.EngineRef.RequestExit(1);
    }

    bool DogMovementAutopilotRunner::ApplyLook(GameMode::GameModeContext& ctx, float x, float y)
    {
        auto& input = ctx.EngineRef.GetInputSystem();
        const auto mouse = input.GetState().GetMouseState();
        // 絶対位置の初回基準を確立し、同じ相対量をlocked cursor用の経路へも流す。
        input.InjectMouseMove(mouse.PositionX, mouse.PositionY);
        input.InjectMouseMove(mouse.PositionX + x, mouse.PositionY + y);
        return input.InjectRawMouseDelta(x, y);
    }

    bool DogMovementAutopilotRunner::EvaluateInputs(GameMode::GameModeContext& ctx,
                                                    const Course::AutopilotSegment& segment)
    {
        auto& input = ctx.EngineRef.GetInputSystem();
        bool changed = false;
        while (m_NextInput < segment.Inputs.size() && segment.Inputs[m_NextInput].Time <= m_Time + 1e-7)
        {
            const auto& event = segment.Inputs[m_NextInput++];
            changed = true;
            if (event.Kind == Course::AutopilotInputKind::Key)
            {
                const auto index = static_cast<size_t>(event.Key);
                m_Keys[index] = event.Action == Input::InputAction::Pressed;
                input.InjectKeyEvent(event.Key, event.Action);
                if (m_MoveStart < 0 && event.Action == Input::InputAction::Pressed &&
                    (event.Key == Input::KeyCode::W || event.Key == Input::KeyCode::A ||
                     event.Key == Input::KeyCode::S || event.Key == Input::KeyCode::D))
                    m_MoveStart = m_Time;
            }
            else if (event.Kind == Course::AutopilotInputKind::MouseButton)
            {
                m_Buttons[static_cast<size_t>(event.Button)] = event.Action == Input::InputAction::Pressed;
                const auto mouse = input.GetState().GetMouseState();
                input.InjectMouseButton(event.Button, event.Action, mouse.PositionX, mouse.PositionY);
            }
            else if (!ApplyLook(ctx, event.LookX, event.LookY))
                return false;
        }
        // Tickは通常のMapper更新後なので、同じフレームの入力群が読む前に対応表を再評価する。
        return !changed || ctx.EngineRef.GetInputMapper().Update(InputTime(), 0);
    }

    bool DogMovementAutopilotRunner::FitTurn(float& radius, float& speed, uint8_t& gait,
                                              float& arcDegrees, float& turnRate) const
    {
        if (m_TurnSamples.size() < 3)
            return false;
        double meanX = 0, meanZ = 0, meanQ = 0, meanSpeed = 0, meanTurnRate = 0;
        for (const auto& sample : m_TurnSamples)
        {
            meanX += sample.X;
            meanZ += sample.Z;
            meanQ += sample.X * sample.X + sample.Z * sample.Z;
            meanSpeed += sample.Speed;
            meanTurnRate += sample.TurnRate;
            if (sample.Gait != m_TurnSamples.front().Gait || !std::isfinite(sample.TurnRate))
                return false;
        }
        const double count = static_cast<double>(m_TurnSamples.size());
        meanX /= count; meanZ /= count; meanQ /= count; meanSpeed /= count; meanTurnRate /= count;
        double xx = 0, xz = 0, zz = 0, xq = 0, zq = 0;
        for (const auto& sample : m_TurnSamples)
        {
            const double x = sample.X - meanX, z = sample.Z - meanZ;
            const double q = sample.X * sample.X + sample.Z * sample.Z - meanQ;
            xx += x * x; xz += x * z; zz += z * z; xq += x * q; zq += z * q;
        }
        const double determinant = xx * zz - xz * xz;
        if (determinant <= 1e-12 * std::max(1.0, xx * zz))
            return false;
        const double centerX = .5 * (xq * zz - zq * xz) / determinant;
        const double centerZ = .5 * (zq * xx - xq * xz) / determinant;
        const double radiusSquared = meanQ - 2 * (centerX * meanX + centerZ * meanZ) +
                                     centerX * centerX + centerZ * centerZ;
        if (!std::isfinite(radiusSquared) || radiusSquared <= 0 || !std::isfinite(meanSpeed))
            return false;
        radius = static_cast<float>(std::sqrt(radiusSquared));
        speed = static_cast<float>(meanSpeed);
        gait = m_TurnSamples.front().Gait;
        turnRate = static_cast<float>(meanTurnRate);
        double sweep = 0;
        double previous = std::atan2(m_TurnSamples.front().Z - centerZ, m_TurnSamples.front().X - centerX);
        for (size_t i = 1; i < m_TurnSamples.size(); ++i)
        {
            const auto& sample = m_TurnSamples[i];
            const double angle = std::atan2(sample.Z - centerZ, sample.X - centerX);
            sweep += std::remainder(angle - previous, 2.0 * Pi);
            previous = angle;
        }
        arcDegrees = static_cast<float>(std::fabs(sweep) * 180.0 / Pi);
        // 標本の選択には期待半径を使わず、当てはめた円上の連続した弧を測る。
        return std::isfinite(radius) && std::isfinite(speed) && std::isfinite(turnRate) &&
               std::isfinite(arcDegrees) && gait < 4 && arcDegrees >= 60.f;
    }

    bool DogMovementAutopilotRunner::IsTurnAtLimit() const
    {
        for (const auto& sample : m_TurnSamples)
        {
            if (sample.Gait >= 4 || !std::isfinite(sample.Speed) || sample.Speed <= 0 ||
                !std::isfinite(sample.TurnRate))
                return false;
            const auto& gait = m_Params.Gaits[sample.Gait];
            const double limit = std::min(static_cast<double>(gait.TurnRateCap),
                                          gait.LateralAcceleration / sample.Speed);
            if (!std::isfinite(limit) || limit <= 0 ||
                std::fabs(std::fabs(sample.TurnRate) - limit) > std::max(.05, .05 * limit))
                return false;
        }
        return true;
    }

    bool DogMovementAutopilotRunner::FinishStage(GameMode::GameModeContext& ctx, DogMovementSmokeData& data)
    {
        const auto& segment = data.Script.Segments[m_Segment];
        auto* owner = ctx.WorldRef.FindEntityByObjectId(data.CharacterId);
        auto* platform = ctx.WorldRef.FindEntityByObjectId(data.PlatformId);
        auto* cameraOwner = ctx.WorldRef.FindEntityByObjectId(data.CameraOwnerId);
        auto* body = owner ? owner->GetComponent<P::CharacterBodyComponent>() : nullptr;
        auto* driver = owner ? owner->GetComponent<Gameplay::QuadrupedLocomotionComponent>() : nullptr;
        auto* follow = cameraOwner ? cameraOwner->GetComponent<Gameplay::FollowCameraComponent>() : nullptr;
        auto* camera = cameraOwner ? cameraOwner->GetComponent<Component::CameraComponent>() : nullptr;
        if (!owner || !body || !driver || !follow || !camera || !platform)
            return false;
        const float y = owner->GetPosition().y;
        bool passed = true;
        switch (segment.Stage)
        {
        case Course::AutopilotStage::StepClimb:
            passed = std::isfinite(y) && y >= Course::LowStep.TopY() - .05f && MarkerNumber(y) >= .15;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=step_climb height=%.4f top_y=%.4f climbed=%d",
                            double(Course::LowStep.TopY()), double(y), int(passed));
            break;
        case Course::AutopilotStage::StepBlock:
            passed = std::isfinite(m_MaxY) && m_MaxY < .30f && MarkerNumber(m_MaxY) < .30;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=step_block height=%.4f max_y=%.4f blocked=%d",
                            double(Course::HighStep.TopY()), double(m_MaxY), int(passed));
            break;
        case Course::AutopilotStage::SlopeClimb:
            passed = std::isfinite(y) && y - Course::LowSlope.Start.y >= 1.f;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=slope_climb angle_deg=%.4f top_y=%.4f climbed=%d",
                            double(Course::LowSlope.AngleDegrees), double(y), int(passed));
            break;
        case Course::AutopilotStage::SlopeBlock:
            passed = std::isfinite(m_MaxY) && m_MaxY - Course::HighSlope.Start.y < 1.f &&
                     MarkerNumber(m_MaxY) - Course::HighSlope.Start.y < 1.0;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=slope_block angle_deg=%.4f max_y=%.4f blocked=%d",
                            double(Course::HighSlope.AngleDegrees), double(m_MaxY), int(passed));
            break;
        case Course::AutopilotStage::Platform:
        {
            const float drift = (PlatformLocal(*platform, owner->GetPosition()) - m_PlatformLocalStart).Length();
            const float rotated = static_cast<float>((data.PlatformTime - m_PlatformStartTime) *
                                                     Course::Platform.YawDegreesPerSecond);
            passed = std::isfinite(drift) && drift <= .05f && MarkerNumber(drift) <= .05;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=platform drift_m=%.4f rotated_deg=%.4f travel_m=%.4f",
                            double(drift), double(rotated), double(m_PlatformTravel));
            break;
        }
        case Course::AutopilotStage::Gallop:
        {
            const float expected = Course::ExpectedGallopTime(m_Params);
            passed = WithinTolerance(static_cast<float>(m_GallopTime), expected);
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=gallop time_s=%.4f expected_s=%.4f",
                            m_GallopTime, double(expected));
            break;
        }
        case Course::AutopilotStage::Turn:
        {
            float radius = 0, speed = 0, expected = 0, arcDegrees = 0, turnRate = 0;
            uint8_t gait = 0;
            const bool fitted = FitTurn(radius, speed, gait, arcDegrees, turnRate);
            if (gait < 4 && speed > 0)
                expected = Course::ExpectedTurnRadius(speed, m_Params.Gaits[gait].TurnRateCap,
                                                      m_Params.Gaits[gait].LateralAcceleration);
            passed = fitted && IsTurnAtLimit() && WithinTolerance(radius, expected);
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=turn radius_m=%.4f expected_m=%.4f "
                            "speed=%.4f arc_deg=%.4f turn_rate=%.4f gait=%u samples=%zu",
                            double(radius), double(expected), double(speed), double(arcDegrees), double(turnRate),
                            static_cast<unsigned>(gait), m_TurnSamples.size());
            break;
        }
        case Course::AutopilotStage::Jump:
        {
            const float distance = HorizontalLength(m_LandingPosition - m_TakeoffPosition);
            const float gravity = CharacterGravity * body->GetSettings().GravityScale;
            const float expected = Course::ExpectedJumpDistance(m_Params, m_TakeoffSpeed, gravity);
            passed = m_bTookOff && m_bLanded && WithinTolerance(distance, expected);
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=jump landing_m=%.4f expected_m=%.4f",
                            double(distance), double(expected));
            break;
        }
        case Course::AutopilotStage::CameraLow:
            m_LowFov = camera->GetFieldOfView(); m_bHaveLowFov = true;
            passed = std::isfinite(m_LowFov) && std::isfinite(follow->GetCollisionOutput().EffectiveLength) &&
                     std::isfinite(follow->GetPitch());
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=camera_low fov_deg=%.4f arm_m=%.4f pitch_deg=%.4f",
                            double(m_LowFov), double(follow->GetCollisionOutput().EffectiveLength), double(follow->GetPitch()));
            break;
        case Course::AutopilotStage::CameraSprint:
            m_SprintFov = camera->GetFieldOfView(); m_bHaveSprintFov = true;
            passed = std::isfinite(m_SprintFov) && std::isfinite(follow->GetCollisionOutput().EffectiveLength) &&
                     std::isfinite(driver->GetOutput().Speed);
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=camera_sprint fov_deg=%.4f arm_m=%.4f speed=%.4f",
                            double(m_SprintFov), double(follow->GetCollisionOutput().EffectiveLength),
                            double(driver->GetOutput().Speed));
            break;
        case Course::AutopilotStage::CameraWall:
        {
            const float arm = follow->GetCollisionOutput().EffectiveLength;
            passed = std::isfinite(arm) && std::isfinite(camera->GetFieldOfView()) && arm < 3.f - .3f &&
                     MarkerNumber(arm) < 2.7;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=camera_wall fov_deg=%.4f arm_m=%.4f nominal_arm_m=3.0000",
                            double(camera->GetFieldOfView()), double(arm));
            break;
        }
        case Course::AutopilotStage::Hitstop:
            passed = data.LateState && data.LateState->AutopilotStop && data.LateState->AutopilotResumed;
            if (data.LateState)
                data.LateState->TrackingAutopilotHitstop = false;
            break;
        default: passed = false; break;
        }
        return passed;
    }

    void DogMovementAutopilotRunner::NextStage(GameMode::GameModeContext& ctx, DogMovementSmokeData& data)
    {
        ++m_Segment;
        m_Phase = Phase::Ready;
        m_StageElapsed = 0;
        if (m_Segment == data.Script.Segments.size())
        {
            if (!m_bHaveLowFov || !m_bHaveSprintFov || (m_SprintFov <= m_LowFov + 1.f || MarkerNumber(m_SprintFov) <= MarkerNumber(m_LowFov) + 1.0))
            {
                Fail(ctx, data, "camera_sprint");
                return;
            }
            m_Phase = Phase::Finished;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=complete exit_code=0");
            ctx.EngineRef.RequestExit(0);
        }
    }

    void DogMovementAutopilotRunner::Tick(GameMode::GameModeContext& ctx, DogMovementSmokeData& data)
    {
        if (m_Phase == Phase::Finished)
            return;
        const double dt = ctx.EngineRef.GetDeterministicCapture().IsEnabled()
                              ? 1.0 / 60.0 : ctx.EngineRef.GetTimeSystem().GetFrameTimes().Unscaled;
        if (!std::isfinite(dt) || dt < 0)
        {
            Fail(ctx, data, "clock");
            return;
        }
        m_StageElapsed += dt;
        const auto& segment = data.Script.Segments[m_Segment];
        if (m_StageElapsed > segment.Duration + 5.0)
        {
            Fail(ctx, data, m_Phase == Phase::Capture ? "capture_timeout" : Course::StageName(segment.Stage));
            return;
        }
        auto* owner = ctx.WorldRef.FindEntityByObjectId(data.CharacterId);
        auto* platform = ctx.WorldRef.FindEntityByObjectId(data.PlatformId);
        auto* cameraOwner = ctx.WorldRef.FindEntityByObjectId(data.CameraOwnerId);
        auto* body = owner ? owner->GetComponent<P::CharacterBodyComponent>() : nullptr;
        auto* driver = owner ? owner->GetComponent<Gameplay::QuadrupedLocomotionComponent>() : nullptr;
        auto* follow = cameraOwner ? cameraOwner->GetComponent<Gameplay::FollowCameraComponent>() : nullptr;
        if (!owner || !platform || !body || !driver || !follow)
        {
            Fail(ctx, data, Course::StageName(segment.Stage));
            return;
        }
        if (m_Phase == Phase::Capture)
        {
            auto& render = ctx.EngineRef.GetRenderWorld();
            Rendering::CapturedFrame frame;
            if (render.TryConsumeCapturedFrame(frame))
            {
                const char* name = CaptureName(segment.Stage);
                const Container::String path = data.CaptureDir + TEXT("/dog_") + name + TEXT(".png");
                if (frame.RequestId != m_CaptureRequest || !Game::Debug::SaveCapturedFramePng(frame, path))
                {
                    Fail(ctx, data, "capture_save");
                    return;
                }
                NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=capture name=%s path=%s", name, path.c_str());
                NextStage(ctx, data);
            }
            else if (render.GetRenderedFrameCount() - m_CaptureStart >= 120)
                Fail(ctx, data, "capture_timeout");
            return;
        }
        if (m_Phase == Phase::Ready)
        {
            if (!data.ReportedReady || !body->GetState().bReady)
                return;
            ReleaseInputs(ctx);
            if (body->Teleport(segment.Start) != P::EPhysicsResult::Success)
            {
                Fail(ctx, data, Course::StageName(segment.Stage));
                return;
            }
            owner->SetRotation(M::Quaternion(M::Vector3::UnitY, std::remainder(segment.YawDegrees, 360.f) * Pi / 180.f));
            m_LastStep = body->GetState().StepSerial;
            m_StableSteps = 0;
            m_Time = 0; m_NextInput = 0; m_MoveStart = m_GallopTime = -1;
            m_MaxY = segment.Start.y; m_PlatformTravel = 0;
            m_LastTurnStepTime = m_StableTurnTime = 0; m_PreviousTurnSpeed = m_PreviousTurnRate = 0;
            m_PreviousTurnGait = 0;
            m_bTookOff = m_bLanded = m_bWasGrounded = false;
            m_TurnSamples.clear();
            m_Phase = Phase::Settling;
            return;
        }
        const auto& state = body->GetState();
        if (m_Phase == Phase::Settling)
        {
            // Wの進行方向が区間の向きと合うよう、通常の視点入力で向きを揃える。
            const float yawError = std::remainder(segment.YawDegrees + 180.f - follow->GetYaw(), 360.f);
            if (std::fabs(yawError) > .5f)
            {
                if (!ApplyLook(ctx, std::clamp(yawError / MouseSensitivity, -1200.f, 1200.f), 0) ||
                    !ctx.EngineRef.GetInputMapper().Update(InputTime(), 0))
                    Fail(ctx, data, Course::StageName(segment.Stage));
                m_StableSteps = 0;
                return;
            }
            if (state.StepSerial != m_LastStep)
            {
                m_LastStep = state.StepSerial;
                const auto relative = state.Velocity - state.PlatformVelocity;
                if (state.bReady && state.bGrounded && HorizontalLength(relative) < .05f)
                    ++m_StableSteps;
                else
                    m_StableSteps = 0;
            }
            if (m_StableSteps < 4)
                return;
            m_MaxY = owner->GetPosition().y;
            m_PreviousPosition = owner->GetPosition(); m_PreviousVelocity = state.Velocity;
            m_PreviousTurnSpeed = driver->GetOutput().Speed;
            m_PreviousTurnRate = driver->GetOutput().TurnRate;
            m_PreviousTurnGait = driver->GetOutput().Gait;
            m_bWasGrounded = state.bGrounded;
            m_PlatformLocalStart = PlatformLocal(*platform, owner->GetPosition());
            m_PlatformPrevious = platform->GetPosition();
            m_PlatformStartTime = data.PlatformTime;
            if (data.LateState && segment.Stage == Course::AutopilotStage::Hitstop)
            {
                data.LateState->AutopilotStop = data.LateState->AutopilotResumed = false;
                data.LateState->TrackingAutopilotHitstop = true;
            }
            m_Phase = Phase::Running;
            if (!EvaluateInputs(ctx, segment))
                Fail(ctx, data, Course::StageName(segment.Stage));
            return;
        }
        m_Time += dt;
        const auto position = owner->GetPosition();
        m_MaxY = std::max(m_MaxY, position.y);
        if (segment.Stage == Course::AutopilotStage::Platform)
        {
            const auto platformPosition = platform->GetPosition();
            m_PlatformTravel += (platformPosition - m_PlatformPrevious).Length();
            m_PlatformPrevious = platformPosition;
        }
        if (segment.Stage == Course::AutopilotStage::Gallop && m_MoveStart >= 0 && m_GallopTime < 0 &&
            driver->GetOutput().Gait == 3)
            m_GallopTime = m_Time - m_MoveStart;
        if (segment.Stage == Course::AutopilotStage::Turn && state.StepSerial != m_LastStep)
        {
            const auto& output = driver->GetOutput();
            const float speed = output.Speed, turnRate = output.TurnRate;
            const double interval = m_Time - m_LastTurnStepTime;
            const bool moving = m_Keys[static_cast<size_t>(Input::KeyCode::W)] ||
                                m_Keys[static_cast<size_t>(Input::KeyCode::A)] ||
                                m_Keys[static_cast<size_t>(Input::KeyCode::S)] ||
                                m_Keys[static_cast<size_t>(Input::KeyCode::D)];
            if (moving && state.bGrounded && std::isfinite(speed) && speed > .05f && std::isfinite(turnRate))
            {
                // 速度・旋回速度・歩様が安定した0.2秒の後を、期待値と無関係に記録する。
                if (std::fabs(speed - m_PreviousTurnSpeed) <= .01 + .5 * interval &&
                    std::fabs(turnRate) > .1f && turnRate * m_PreviousTurnRate > 0 &&
                    std::fabs(turnRate - m_PreviousTurnRate) <= .03 + .75 * interval &&
                    output.Gait == m_PreviousTurnGait)
                    m_StableTurnTime += interval;
                else
                {
                    m_StableTurnTime = 0;
                    m_TurnSamples.clear();
                }
                if (m_Time >= segment.Duration * .5 && m_StableTurnTime >= .2)
                    m_TurnSamples.push_back({position.x, position.z, speed, turnRate, output.Gait});
            }
            else if (moving)
            {
                m_StableTurnTime = 0;
                m_TurnSamples.clear();
            }
            m_PreviousTurnSpeed = speed;
            m_PreviousTurnRate = turnRate;
            m_PreviousTurnGait = output.Gait;
            m_LastTurnStepTime = m_Time;
            m_LastStep = state.StepSerial;
        }
        if (segment.Stage == Course::AutopilotStage::Jump)
        {
            if (!m_bTookOff && m_bWasGrounded && !state.bGrounded)
            {
                m_bTookOff = true; m_TakeoffPosition = m_PreviousPosition;
                m_TakeoffSpeed = HorizontalLength(m_PreviousVelocity);
            }
            else if (m_bTookOff && !m_bLanded && state.bGrounded)
            {
                m_bLanded = true; m_LandingPosition = position;
            }
            m_bWasGrounded = state.bGrounded;
            m_PreviousPosition = position; m_PreviousVelocity = state.Velocity;
        }
        if (!EvaluateInputs(ctx, segment))
        {
            Fail(ctx, data, Course::StageName(segment.Stage));
            return;
        }
        if (m_Time < segment.Duration)
            return;
        ReleaseInputs(ctx);
        if (!FinishStage(ctx, data))
        {
            Fail(ctx, data, Course::StageName(segment.Stage));
            return;
        }
        if (m_bHaveLowFov && m_bHaveSprintFov && (m_SprintFov <= m_LowFov + 1.f || MarkerNumber(m_SprintFov) <= MarkerNumber(m_LowFov) + 1.0))
        {
            Fail(ctx, data, "camera_sprint");
            return;
        }
        const char* captureName = CaptureName(segment.Stage);
        if (captureName && !data.CaptureDir.empty())
        {
            auto& render = ctx.EngineRef.GetRenderWorld();
            Rendering::FrameCaptureRequest request;
            request.SourceKind = Rendering::FrameCaptureSourceKind::BackBuffer;
            const auto result = render.RequestFrameCapture(request);
            if (!result.IsAccepted())
            {
                Fail(ctx, data, "capture_request");
                return;
            }
            m_CaptureRequest = result.RequestId;
            m_CaptureStart = render.GetRenderedFrameCount();
            m_Phase = Phase::Capture;
            return;
        }
        NextStage(ctx, data);
    }
} // namespace Game::GameModes
