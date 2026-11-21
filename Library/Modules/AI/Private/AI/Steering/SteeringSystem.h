#pragma once

#include "AI/Navigation/NavTypes.h"
#include "AI/Spatial/UniformGridHash.h"
#include "AI/Steering/SteeringBehaviors.h"
#include "Debug/DebugConfig.h"

namespace NorvesLib::Modules::AI
{
    class INavigationService;
    class SteeringAgentComponent;
    struct SteeringRequest;

    class SteeringSystem
    {
    public:
        void RegisterAgent(SteeringAgentComponent& agent);
        void UnregisterAgent(SteeringAgentComponent& agent, INavigationService& nav);
        void Reset(INavigationService& nav);
        uint32_t GetAgentCount() const { return static_cast<uint32_t>(m_Records.size()); }
        Core::Container::Span<SteeringAgentComponent* const> GetAgents() const { return m_Agents; }
        static bool CanSteer(const SteeringAgentComponent& agent);
#if NORVES_ENABLE_DEBUG_OUTPUT
        // 登録順の記録を描画中だけ借りる。
        const auto& GetDebugRecord(uint32_t index) const { return m_Records[index]; }
#endif

        void BeginStep(INavigationService& nav);
        void Compute(float dt, const INavigationService& nav);
        // 添字は直近の収集時に有効だった個体の登録順。
        const Spatial::UniformGridHash& GetNeighborHash() const { return m_NeighborHash; }

    private:
        struct AgentRecord
        {
            SteeringAgentComponent* Agent = nullptr;
            uint32_t Revision = 0;
            Steering::KeepDistanceState KeepDistance;
            Steering::WanderState Wander;
            Steering::PathFollowState PathFollow;
            Steering::TrailRing Trail;
            PathHandle Path;
            Math::PolylinePath Polyline;
            bool bPolylinePrepared = false;
            bool bPathFinished = false;
            bool bRepath = false;
            Math::Vector3 LastTarget;
            bool bHasLastTarget = false;
            Math::Vector3 DesiredVelocity;
        };

        struct VectorColumns
        {
            Core::Container::VariableArray<float> X, Y, Z;
            void Clear();
            void Append(const Math::Vector3& value);
            Math::Vector3 Get(size_t index) const { return {X[index], Y[index], Z[index]}; }
        };

        struct StepInput
        {
            size_t RecordIndex;
            Steering::SteeringKinematics Target;
            bool bHasTarget = false;
        };

        struct NeighborScratch
        {
            Core::Container::VariableArray<uint32_t> Indices;
            Core::Container::VariableArray<Math::Vector3> Positions;
        };

        void Gather();
        void GatherNeighbors(uint32_t index, float radius, NeighborScratch& scratch) const;
        Math::Vector3 Solve(uint32_t index, float dt, const INavigationService& nav, NeighborScratch& scratch);
        static Math::Vector3 SolveRequest(AgentRecord& record, const StepInput& input,
                                          const Steering::SteeringKinematics& self, const Steering::SteeringLimits& limits,
                                          Steering::SteeringPlane plane, float dt, const INavigationService& nav);
        static Math::Vector3 FollowPath(AgentRecord& record, const SteeringRequest& request,
                                        const Steering::SteeringKinematics& self, const Steering::SteeringLimits& limits,
                                        Steering::SteeringPlane plane, const INavigationService& nav);

        Core::Container::VariableArray<AgentRecord> m_Records;
        // GetAgents の連続ビューを登録・解除の間は有効に保つ。
        Core::Container::VariableArray<SteeringAgentComponent*> m_Agents;
        VectorColumns m_Positions, m_Velocities;
        Core::Container::VariableArray<StepInput> m_Inputs;
        Core::Container::VariableArray<Math::Vector3> m_Results;
        Spatial::UniformGridHash m_NeighborHash;
    };
}
