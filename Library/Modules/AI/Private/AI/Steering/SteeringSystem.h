#pragma once

#include "AI/Navigation/NavTypes.h"
#include "AI/Spatial/UniformGridHash.h"
#include "AI/Steering/SteeringBehaviors.h"
#include "Debug/DebugConfig.h"
#include "Object/EntityHandle.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::AI
{
    class INavigationService;
    class SteeringAgentComponent;
    struct SteeringRequest;

    class SteeringSystem
    {
    public:
        using StepAgents = Core::Container::VariableArray<SteeringAgentComponent*>;

        void RegisterAgent(SteeringAgentComponent& agent);
        void UnregisterAgent(SteeringAgentComponent& agent, INavigationService& nav);
        void Reset(INavigationService& nav, const Core::World* world);
        uint32_t GetAgentCount() const { return static_cast<uint32_t>(m_Records.size()); }
        SteeringAgentComponent* ResolveAgent(size_t index, const Core::World& world);
        static bool CanSteer(const SteeringAgentComponent& agent);
#if NORVES_ENABLE_DEBUG_OUTPUT
        // 登録順の記録を描画中だけ借りる。
        const auto& GetDebugRecord(uint32_t index) const { return m_Records[index]; }
#endif

        StepAgents BeginStep(INavigationService& nav, const Core::World& world);
        void Compute(float dt, const INavigationService& nav, const StepAgents& stepAgents);
        // 直近の固定歩で収集したハッシュの添字 i は GetNeighborEntities()[i]・GetNeighborObjectIds()[i] に対応する。
        // どちらもその歩に所有 Entity から読んだ値。ECS の行がまだ無い個体のハンドルは無効。
        const Spatial::UniformGridHash& GetNeighborHash() const { return m_NeighborHash; }
        Core::Container::Span<const Core::EntityHandle> GetNeighborEntities() const { return m_NeighborEntities; }
        Core::Container::Span<const uint64_t> GetNeighborObjectIds() const { return m_NeighborObjectIds; }

    private:
        struct AgentRecord
        {
            // 登録・解除の照合に使う Component の非所有参照。Initialize で登録し、EndPlay/Finalize/デストラクタで外す。
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
            Math::Vector3 PathEndTarget;
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
            Core::Container::VariableArray<uint64_t> Ids;
        };

        void Gather(const StepAgents& stepAgents);
        void GatherNeighbors(uint32_t index, float radius, NeighborScratch& scratch) const;
        Math::Vector3 Solve(uint32_t index, float dt, const INavigationService& nav, const SteeringAgentComponent& agent,
                            NeighborScratch& scratch);
        static Math::Vector3 SolveRequest(AgentRecord& record, const SteeringRequest& request, const StepInput& input,
                                          const Steering::SteeringKinematics& self, const Steering::SteeringLimits& limits,
                                          Steering::SteeringPlane plane, float dt, const INavigationService& nav,
                                          const NavAgentProfile& profile);
        static Math::Vector3 FollowPath(AgentRecord& record, const SteeringRequest& request,
                                        const Steering::SteeringKinematics& self, const Steering::SteeringLimits& limits,
                                        Steering::SteeringPlane plane, const INavigationService& nav, const NavAgentProfile& profile);

        Core::Container::VariableArray<AgentRecord> m_Records;
        Core::Container::VariableArray<Core::EntityHandle> m_NeighborEntities;
        Core::Container::VariableArray<uint64_t> m_NeighborObjectIds;
        VectorColumns m_Positions, m_Velocities;
        Core::Container::VariableArray<StepInput> m_Inputs;
        Core::Container::VariableArray<Math::Vector3> m_Results;
        Spatial::UniformGridHash m_NeighborHash;
    };
}
