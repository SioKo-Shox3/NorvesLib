#pragma once

#include "GameMode/TGameMode.h"
#include "Math/Vector3.h"

namespace Game::GameModes
{
    struct AINavSmokeData
    {
        uint64_t AgentId = 0;
        uint64_t MovingBoxId = 0;
        uint32_t WalkableCells = 0;
        uint32_t MaxObstacleCells = 0;
        NorvesLib::Math::Vector3 PreviousPosition;
        double Elapsed = 0;
        double Distance = 0;
        bool TileLoaded = false;
        bool PreviousNavDebug = false;
        bool PreviousSteeringDebug = false;
        bool Ready = false;
        bool Finished = false;
    };

    class AINavSmokeRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                                             AINavSmokeData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, AINavSmokeData& data, float dt);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, AINavSmokeData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);
        static constexpr const char* DebugName = "AINavSmoke";
    };

    using AINavSmokeMode = NorvesLib::Core::GameMode::TGameMode<AINavSmokeRoutine, AINavSmokeData>;
}
