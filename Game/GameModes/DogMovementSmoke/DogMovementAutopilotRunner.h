#pragma once

#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "DogMovementAutopilotScript.h"
#include "Locomotion/QuadrupedLocomotionParams.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}
namespace Game::GameModes
{
    struct DogMovementSmokeData;

    class DogMovementAutopilotRunner
    {
      public:
        bool Initialize(NorvesLib::Core::GameMode::GameModeContext& ctx, DogMovementSmokeData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, DogMovementSmokeData& data);
        void Stop(NorvesLib::Core::GameMode::GameModeContext& ctx);

      private:
        enum class Phase : uint8_t { Ready, Settling, Running, Capture, Finished };
        struct TurnSample
        {
            double X = 0, Z = 0, Speed = 0, TurnRate = 0;
            uint8_t Gait = 0;
        };

        void Fail(NorvesLib::Core::GameMode::GameModeContext& ctx, DogMovementSmokeData& data, const char* reason);
        void ReleaseInputs(NorvesLib::Core::GameMode::GameModeContext& ctx);
        bool ApplyLook(NorvesLib::Core::GameMode::GameModeContext& ctx, float x, float y);
        bool EvaluateInputs(NorvesLib::Core::GameMode::GameModeContext& ctx,
                            const DogMovementAutopilot::AutopilotSegment& segment);
        bool FinishStage(NorvesLib::Core::GameMode::GameModeContext& ctx, DogMovementSmokeData& data);
        void NextStage(NorvesLib::Core::GameMode::GameModeContext& ctx, DogMovementSmokeData& data);
        bool FitTurn(float& radius, float& speed, uint8_t& gait, float& arcDegrees, float& turnRate) const;
        bool IsTurnAtLimit() const;

        Phase m_Phase = Phase::Ready;
        size_t m_Segment = 0, m_NextInput = 0;
        double m_StageElapsed = 0, m_Time = 0, m_MoveStart = -1, m_GallopTime = -1;
        uint64_t m_LastStep = 0, m_CaptureStart = 0, m_CaptureRequest = 0;
        unsigned m_StableSteps = 0;
        bool m_Keys[static_cast<size_t>(NorvesLib::Core::Input::KeyCode::Count)] = {};
        bool m_Buttons[static_cast<size_t>(NorvesLib::Core::Input::MouseButton::Count)] = {};
        bool m_bOverrideFocus = false, m_bWasGrounded = false, m_bTookOff = false, m_bLanded = false;
        bool m_bHaveLowFov = false, m_bHaveSprintFov = false;
        float m_MaxY = 0, m_LowFov = 0, m_SprintFov = 0, m_TakeoffSpeed = 0, m_PlatformTravel = 0;
        double m_PlatformStartTime = 0, m_LastTurnStepTime = 0, m_StableTurnTime = 0;
        float m_PreviousTurnSpeed = 0, m_PreviousTurnRate = 0;
        uint8_t m_PreviousTurnGait = 0;
        NorvesLib::Math::Vector3 m_PlatformLocalStart, m_PlatformPrevious;
        NorvesLib::Math::Vector3 m_PreviousPosition, m_PreviousVelocity, m_TakeoffPosition, m_LandingPosition;
        NorvesLib::Core::Locomotion::QuadrupedLocomotionParams m_Params;
        NorvesLib::Core::Container::VariableArray<TurnSample> m_TurnSamples;
    };
} // namespace Game::GameModes
