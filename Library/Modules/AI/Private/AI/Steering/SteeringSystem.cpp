#include "AI/Steering/SteeringSystem.h"

#include "AI/Navigation/INavigationService.h"
#include "AI/Steering/SteeringAgentComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/CharacterBodyComponent.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::AI
{
    using namespace Steering;

    namespace
    {
        const NavAgentProfile& FindSteeringProfile(const INavigationService& nav, const SteeringAgentComponent& agent,
                                                   const NavAgentProfile& fallback)
        {
            const auto* profile = nav.FindProfile(agent.getNavProfileName()->c_str());
            if (!profile)
            {
                profile = nav.FindProfile("Medium");
            }
            return profile ? *profile : fallback;
        }

        bool ReadTarget(const Core::Entity& owner, Core::EntityHandle handle, SteeringKinematics& target)
        {
            if (!handle.IsValid() || !owner.GetWorld())
            {
                return false;
            }
            const auto* entity = owner.GetWorld()->ResolveEntityHandle(handle);
            if (!entity || entity->IsPendingDestroy())
            {
                return false;
            }
            target.Position = entity->GetWorldTransform().position;
            if (const auto* agent = entity->GetComponent<SteeringAgentComponent>())
            {
                target.Velocity = SteeringSystem::CanSteer(*agent) ? agent->GetSteeringVelocity() : Math::Vector3{};
            }
            else if (const auto* body = entity->GetComponent<Physics::CharacterBodyComponent>();
                     body && !body->IsPendingDestroy())
            {
                target.Velocity = body->GetState().Velocity;
            }
            return true;
        }

        void AddNavigationDanger(ContextMaps& maps, const SteeringKinematics& self,
                                 const INavigationService& nav, const NavAgentProfile& profile)
        {
            if (!nav.IsWalkable(self.Position, profile))
            {
                return;
            }
            const float lookDistance = std::clamp(PlanarLength(self.Velocity) + 1.5f, 2.0f, 6.0f);
            float clearance[kContextDirectionCount];
            for (uint32_t i = 0; i < kContextDirectionCount; ++i)
            {
                const auto end = self.Position + ContextDirection(i) * lookDistance;
                const auto hit = nav.NavRaycast(self.Position, end, profile);
                clearance[i] = hit.bHit ? hit.Fraction * lookDistance : lookDistance;
            }
            AddDangerFromClearance(maps, clearance, lookDistance);
        }
    }

    bool SteeringSystem::CanSteer(const SteeringAgentComponent& agent)
    {
        const auto* owner = agent.GetOwner();
        return owner && owner->IsActiveInHierarchy() && owner->IsTickEnabled() &&
               agent.IsActive() && agent.IsTickEnabled() && !agent.IsPendingDestroy();
    }

    void SteeringSystem::RegisterAgent(SteeringAgentComponent& agent)
    {
        if (!agent.GetOwner())
        {
            return;
        }
        const auto componentId = agent.GetComponentId();
        for (const auto& record : m_Records)
        {
            if (record.Agent == &agent || record.ComponentId == componentId)
            {
                return;
            }
        }
        auto& record = m_Records.emplace_back();
        record.Agent = &agent;
        record.ComponentId = componentId;
        agent.m_bRegistered = true;
    }

    void SteeringSystem::UnregisterAgent(SteeringAgentComponent& agent, INavigationService& nav)
    {
        const auto componentId = agent.GetComponentId();
        const auto it = std::find_if(m_Records.begin(), m_Records.end(), [componentId](const AgentRecord& record)
        {
            return record.ComponentId == componentId;
        });
        if (it != m_Records.end())
        {
            if (it->Path.IsValid())
            {
                nav.ReleasePath(it->Path);
            }
            m_Records.erase(it);
        }
        agent.m_bRegistered = false;
    }

    SteeringAgentComponent* SteeringSystem::ResolveAgent(size_t index, const Core::World& world)
    {
        auto* agent = m_Records[index].Agent;
        const auto* owner = agent ? agent->GetOwner() : nullptr;
        return owner && owner->GetWorld() == &world ? agent : nullptr;
    }

    void SteeringSystem::Reset(INavigationService& nav)
    {
        for (const auto& record : m_Records)
        {
            if (record.Path.IsValid())
            {
                nav.ReleasePath(record.Path);
            }
            if (record.Agent)
            {
                record.Agent->m_bRegistered = false;
            }
        }
        m_Records.clear();
        m_NeighborEntities.clear();
        m_NeighborObjectIds.clear();
        m_Inputs.clear();
        m_Positions.Clear();
        m_Velocities.Clear();
        m_Results.clear();
        m_NeighborHash.Clear();
    }

    SteeringSystem::StepAgents SteeringSystem::BeginStep(INavigationService& nav, const Core::World& world)
    {
        const NavAgentProfile fallback{};
        StepAgents stepAgents;
        stepAgents.reserve(m_Records.size());
        for (size_t i = 0; i < m_Records.size(); ++i)
        {
            auto* agent = ResolveAgent(i, world);
            stepAgents.push_back(agent);
            if (!agent || !CanSteer(*agent))
            {
                continue;
            }
            agent->EnsureMotorBinding();
            auto& record = m_Records[i];
            const auto& request = agent->GetRequest();
            if (record.Revision == request.Revision && !record.bRepath)
            {
                continue;
            }
            if (record.Path.IsValid())
            {
                nav.CancelPath(record.Path);
                nav.ReleasePath(record.Path);
            }
            record = AgentRecord{.Agent = record.Agent, .ComponentId = record.ComponentId, .Revision = request.Revision};
            if (request.Kind == ESteeringRequest::Wander)
            {
                InitializeWander(record.Wander, agent->WanderSeed.Get());
            }
            if (request.Kind == ESteeringRequest::MoveTo)
            {
                const auto start = agent->GetOwner()->GetWorldTransform().position;
                record.Path = nav.RequestPath(start, request.TargetPosition, FindSteeringProfile(nav, *agent, fallback));
            }
        }
        return stepAgents;
    }

    void SteeringSystem::VectorColumns::Clear()
    {
        X.clear();
        Y.clear();
        Z.clear();
    }

    void SteeringSystem::VectorColumns::Append(const Math::Vector3& value)
    {
        X.push_back(value.x);
        Y.push_back(value.y);
        Z.push_back(value.z);
    }

    void SteeringSystem::Gather(const StepAgents& stepAgents)
    {
        m_Inputs.clear();
        m_Positions.Clear();
        m_Velocities.Clear();
        m_NeighborEntities.clear();
        m_NeighborObjectIds.clear();
        for (size_t i = 0; i < stepAgents.size(); ++i)
        {
            auto* agent = stepAgents[i];
            if (!agent)
            {
                continue;
            }
            if (!CanSteer(*agent))
            {
                agent->SetSteeringVelocity(Math::Vector3{});
                continue;
            }
            const auto& owner = *agent->GetOwner();
            m_NeighborEntities.push_back(owner.GetEntityHandle());
            m_NeighborObjectIds.push_back(owner.GetObjectId());
            m_Positions.Append(owner.GetWorldTransform().position);
            m_Velocities.Append(agent->GetSteeringVelocity());
            auto& input = m_Inputs.emplace_back();
            input.RecordIndex = i;
            input.bHasTarget = ReadTarget(owner, agent->GetRequest().TargetEntity, input.Target);
        }
    }

    void SteeringSystem::GatherNeighbors(uint32_t index, float radius, NeighborScratch& scratch) const
    {
        m_NeighborHash.QueryRadius(m_Positions.X[index], m_Positions.Z[index], radius, scratch.Indices, index);
        scratch.Positions.clear();
        scratch.Ids.clear();
        for (uint32_t neighbor : scratch.Indices)
        {
            scratch.Positions.push_back(m_Positions.Get(neighbor));
            scratch.Ids.push_back(m_NeighborObjectIds[neighbor]);
        }
    }

    Math::Vector3 SteeringSystem::FollowPath(AgentRecord& record, const SteeringRequest& request,
                                             const SteeringKinematics& self, const SteeringLimits& limits,
                                             SteeringPlane plane, const INavigationService& nav, const NavAgentProfile& profile)
    {
        const auto status = nav.GetPathStatus(record.Path);
        if (status != ENavPathStatus::Succeeded && status != ENavPathStatus::Partial)
        {
            return {};
        }
        if (nav.IsPathStale(record.Path))
        {
            record.bRepath = true;
            return {};
        }
        if (!record.bPolylinePrepared)
        {
            nav.BuildPathPolyline(record.Path, record.Polyline);
            record.bPolylinePrepared = true;
        }
        if (record.Polyline.IsEmpty())
        {
            return {};
        }
        PathFollowParams params;
        params.Arrive.StopRadius = request.StopRadius;
        if (!record.bPathFinished)
        {
            const auto velocity = PathFollow(self, record.Polyline, limits, params, record.PathFollow, plane);
            record.bPathFinished = IsPathFollowFinished(self, record.Polyline, record.PathFollow, params.Arrive);
            if (!record.bPathFinished)
            {
                return velocity;
            }

            const auto& end = record.Polyline.GetPoints().back();
            record.PathEndTarget = end;
            if (status == ENavPathStatus::Succeeded &&
                (std::hypot(end.x - request.TargetPosition.x, end.z - request.TargetPosition.z) <= 1e-3f ||
                 !nav.NavRaycast(end, request.TargetPosition, profile).bHit))
            {
                record.PathEndTarget = request.TargetPosition;
            }
        }
        record.LastTarget = record.PathEndTarget;
        return Arrive(self, record.PathEndTarget, limits, params.Arrive, plane);
    }

    Math::Vector3 SteeringSystem::SolveRequest(AgentRecord& record, const SteeringRequest& request, const StepInput& input,
                                               const SteeringKinematics& self, const SteeringLimits& limits,
                                               SteeringPlane plane, float dt, const INavigationService& nav,
                                               const NavAgentProfile& profile)
    {
        const bool hasDestination = request.Kind == ESteeringRequest::Arrive || request.Kind == ESteeringRequest::MoveTo;
        record.bHasLastTarget = hasDestination || input.bHasTarget;
        record.LastTarget = hasDestination ? request.TargetPosition : input.Target.Position;
        if (request.TargetEntity.IsValid() && !input.bHasTarget)
        {
            return {};
        }

        switch (request.Kind)
        {
        case ESteeringRequest::Idle:
            return {};
        case ESteeringRequest::Arrive:
            return Arrive(self, request.TargetPosition, limits, {.StopRadius = request.StopRadius}, plane);
        case ESteeringRequest::MoveTo:
            return FollowPath(record, request, self, limits, plane, nav, profile);
        case ESteeringRequest::Pursue:
            return Pursue(self, input.Target, limits, {}, {}, plane);
        case ESteeringRequest::Flee:
            return Flee(self, input.Target.Position, limits, plane);
        case ESteeringRequest::KeepDistance:
            return KeepDistance(self, input.Target, limits, {request.MinDistance, request.MaxDistance},
                                record.KeepDistance, plane);
        case ESteeringRequest::Wander:
            return Wander(self, limits, {}, record.Wander, dt, plane);
        case ESteeringRequest::FollowTrail:
            RecordTrail(record.Trail, input.Target.Position, dt);
            return FollowTrail(self, record.Trail, limits, {.FollowDistance = request.FollowDistance}, plane);
        }
        return {};
    }

    Math::Vector3 SteeringSystem::Solve(uint32_t index, float dt, const INavigationService& nav,
                                        const SteeringAgentComponent& agent, NeighborScratch& scratch)
    {
        const auto& input = m_Inputs[index];
        auto& record = m_Records[input.RecordIndex];
        const auto plane = agent.Dimension.Get() == ESteeringDimension::Flight ? SteeringPlane::Flight : SteeringPlane::Ground;
        const auto& rates = agent.TurnRateBySpeed.Get();
        const auto limits = SanitizeLimits({agent.MaxSpeed.Get(), agent.MaxAcceleration.Get(),
                                            {rates.x, rates.y, rates.z, rates.w}});
        const SteeringKinematics self{m_Positions.Get(index), m_Velocities.Get(index)};
        const NavAgentProfile fallback{};
        const auto& profile = FindSteeringProfile(nav, agent, fallback);
        const auto primary = SolveRequest(record, agent.GetRequest(), input, self, limits, plane, dt, nav, profile);
        const float radius = (std::max)(1.0f, 2.0f * profile.Radius);
        GatherNeighbors(index, radius, scratch);
        const auto separation = Separation(self, scratch.Positions, radius, limits, plane, scratch.Ids, m_NeighborObjectIds[index]);
        const WeightedVelocity items[] = {{primary, 1.0f}, {separation, 1.0f}};
        auto desired = CombineWeighted(items, limits, plane);
        if (plane == SteeringPlane::Ground)
        {
            ContextMaps maps;
            AddInterest(maps, desired, limits.MaxSpeed);
            AddNavigationDanger(maps, self, nav, profile);
            constexpr float neighborRadius = 1.5f;
            GatherNeighbors(index, neighborRadius, scratch);
            AddDangerFromNeighbors(maps, self.Position, scratch.Positions, neighborRadius);
            desired = ResolveContext(maps, desired, limits.MaxSpeed, {}).Velocity;
        }
        record.DesiredVelocity = desired;
        return ApplySteeringLimits(self.Velocity, desired, limits, dt, plane);
    }

    void SteeringSystem::Compute(float dt, const INavigationService& nav, const StepAgents& stepAgents)
    {
        if (!std::isfinite(dt) || dt <= 0.0f)
        {
            return;
        }
        Gather(stepAgents);
        m_NeighborHash.Build(m_Positions.X, m_Positions.Z);
        m_Results.resize(m_Inputs.size());
        NeighborScratch scratch;
        for (uint32_t i = 0; i < m_Inputs.size(); ++i)
        {
            m_Results[i] = Solve(i, dt, nav, *stepAgents[m_Inputs[i].RecordIndex], scratch);
        }
        for (size_t i = 0; i < m_Inputs.size(); ++i)
        {
            stepAgents[m_Inputs[i].RecordIndex]->SetSteeringVelocity(m_Results[i]);
        }
    }
}
