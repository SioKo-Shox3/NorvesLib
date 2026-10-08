#pragma once
#include "CameraLateUpdate.h"
#include "GameMode/TGameMode.h"
#include "Physics/CharacterBodyTypes.h"
#include "Rendering/RenderTypes.h"
namespace Game::GameModes
{
    struct DogMovementSmokeData
    {
        NorvesLib::Core::Container::TWeakPtr<Game::CameraLateUpdateSlot> LateSlot;
        NorvesLib::Core::Container::TSharedPtr<Game::CameraLateUpdateState> LateState;
        NorvesLib::Modules::Physics::CharacterDriveMode Drive = NorvesLib::Modules::Physics::CharacterDriveMode::Fixed;
        NorvesLib::Core::Rendering::MaterialHandle Materials[3];
        uint64_t CharacterId = 0, CameraOwnerId = 0;
        bool PushedContext = false, ReportedReady = false;
    };
    class DogMovementSmokeRoutine
    {
      public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext&,
                                                             DogMovementSmokeData&);
        void Tick(NorvesLib::Core::GameMode::GameModeContext&, DogMovementSmokeData&, float);
        void Leave(NorvesLib::Core::GameMode::GameModeContext&, DogMovementSmokeData&,
                   NorvesLib::Core::GameMode::GameModeExitReason);
        static constexpr const char* DebugName = "DogMovementSmoke";

      private:
        static void Stop(NorvesLib::Core::GameMode::GameModeContext&, DogMovementSmokeData&);
    };
    using DogMovementSmokeMode = NorvesLib::Core::GameMode::TGameMode<DogMovementSmokeRoutine, DogMovementSmokeData>;
} // namespace Game::GameModes
