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
        bool CanSteer(const SteeringAgentComponent& agent)
        {
            const auto* owner = agent.GetOwner();
            return owner && owner->IsActive() && !owner->IsPendingDestroy() &&
                   agent.IsActive() && !agent.IsPendingDestroy();
        }

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
            const auto* agent = entity->GetComponent<SteeringAgentComponent>();
            if (agent && !agent->IsPendingDestroy())
            {
                target.Velocity = agent->GetSteeringVelocity();
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

    void SteeringSystem::RegisterAgent(SteeringAgentComponent& agent)
    {
        if (!agent.m_bRegistered)
        {
            // ビューの容量を先に確保し、記録の追加後は割り当てなしでポインタを追加する。
            if (m_Agents.size() == m_Agents.capacity())
            {
                m_Agents.reserve(m_Agents.size() * 2 + 1);
            }
            m_Records.emplace_back().Agent = &agent;
            m_Agents.push_back(&agent);
            agent.m_bRegistered = true;
        }
    }

    void SteeringSystem::UnregisterAgent(SteeringAgentComponent& agent, INavigationService& nav)
    {
        const auto it = std::find(m_Agents.begin(), m_Agents.end(), &agent);
        if (it != m_Agents.end())
        {
            const auto record = m_Records.begin() + (it - m_Agents.begin());
            if (record->Path.IsValid())
            {
                nav.ReleasePath(record->Path);
            }
            m_Records.erase(record);
            m_Agents.erase(it);
            agent.m_bRegistered = false;
        }
    }

    void SteeringSystem::Reset(INavigationService& nav)
    {
        for (auto& record : m_Records)
        {
            if (record.Path.IsValid())
            {
                nav.ReleasePath(record.Path);
            }
            record.Agent->m_bRegistered = false;
        }
        m_Records.clear();
        m_Agents.clear();
        m_Inputs.clear();
        m_Positions.Clear();
        m_Velocities.Clear();
        m_Results.clear();
        m_NeighborHash.Clear();
    }

    void SteeringSystem::BeginStep(INavigationService& nav)
    {
        const NavAgentProfile fallback{};
        for (auto& record : m_Records)
        {
            auto& agent = *record.Agent;
            if (!CanSteer(agent))
            {
                continue;
            }
            agent.EnsureMotorBinding();
            const auto& request = agent.GetRequest();
            if (record.Revision == request.Revision && !record.bRepath)
            {
                continue;
            }
            if (record.Path.IsValid())
            {
                nav.CancelPath(record.Path);
                nav.ReleasePath(record.Path);
            }
            record = AgentRecord{};
            record.Agent = &agent;
            record.Revision = request.Revision;
            if (request.Kind == ESteeringRequest::Wander)
            {
                InitializeWander(record.Wander, agent.WanderSeed.Get());
            }
            if (request.Kind == ESteeringRequest::MoveTo)
            {
                const auto start = agent.GetOwner()->GetWorldTransform().position;
                record.Path = nav.RequestPath(start, request.TargetPosition, FindSteeringProfile(nav, agent, fallback));
            }
        }
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

    void SteeringSystem::Gather()
    {
        m_Inputs.clear();
        m_Positions.Clear();
        m_Velocities.Clear();
        for (size_t i = 0; i < m_Records.size(); ++i)
        {
            const auto& agent = *m_Records[i].Agent;
            if (!CanSteer(agent))
            {
                continue;
            }
            const auto& owner = *agent.GetOwner();
            m_Positions.Append(owner.GetWorldTransform().position);
            m_Velocities.Append(agent.GetSteeringVelocity());
            auto& input = m_Inputs.emplace_back();
            input.RecordIndex = i;
            input.bHasTarget = ReadTarget(owner, agent.GetRequest().TargetEntity, input.Target);
        }
    }

    void SteeringSystem::GatherNeighbors(uint32_t index, float radius, NeighborScratch& scratch) const
    {
        m_NeighborHash.QueryRadius(m_Positions.X[index], m_Positions.Z[index], radius, scratch.Indices, index);
        scratch.Positions.clear();
        for (uint32_t neighbor : scratch.Indices)
        {
            scratch.Positions.push_back(m_Positions.Get(neighbor));
        }
    }

    Math::Vector3 SteeringSystem::FollowPath(AgentRecord& record, const SteeringRequest& request,
                                             const SteeringKinematics& self, const SteeringLimits& limits,
                                             SteeringPlane plane, const INavigationService& nav)
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
        }
        return Arrive(self, request.TargetPosition, limits, params.Arrive, plane);
    }

    Math::Vector3 SteeringSystem::SolveRequest(AgentRecord& record, const StepInput& input,
                                               const SteeringKinematics& self, const SteeringLimits& limits,
                                               SteeringPlane plane, float dt, const INavigationService& nav)
    {
        const auto& request = record.Agent->GetRequest();
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
            return FollowPath(record, request, self, limits, plane, nav);
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

    Math::Vector3 SteeringSystem::Solve(uint32_t index, float dt, const INavigationService& nav, NeighborScratch& scratch)
    {
        const auto& input = m_Inputs[index];
        auto& record = m_Records[input.RecordIndex];
        const auto& agent = *record.Agent;
        const auto plane = agent.Dimension.Get() == ESteeringDimension::Flight ? SteeringPlane::Flight : SteeringPlane::Ground;
        const auto& rates = agent.TurnRateBySpeed.Get();
        const auto limits = SanitizeLimits({agent.MaxSpeed.Get(), agent.MaxAcceleration.Get(),
                                            {rates.x, rates.y, rates.z, rates.w}});
        const SteeringKinematics self{m_Positions.Get(index), m_Velocities.Get(index)};
        const auto primary = SolveRequest(record, input, self, limits, plane, dt, nav);
        const NavAgentProfile fallback{};
        const auto& profile = FindSteeringProfile(nav, agent, fallback);
        const float radius = (std::max)(1.0f, 2.0f * profile.Radius);
        GatherNeighbors(index, radius, scratch);
        const WeightedVelocity items[] = {{primary, 1.0f},
                                          {Separation(self, scratch.Positions, radius, limits, plane), 1.0f}};
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

    void SteeringSystem::Compute(float dt, const INavigationService& nav)
    {
        if (!std::isfinite(dt) || dt <= 0.0f)
        {
            return;
        }
        Gather();
        m_NeighborHash.Build(m_Positions.X, m_Positions.Z);
        m_Results.resize(m_Inputs.size());
        NeighborScratch scratch;
        for (uint32_t i = 0; i < m_Inputs.size(); ++i)
        {
            m_Results[i] = Solve(i, dt, nav, scratch);
        }
        for (size_t i = 0; i < m_Inputs.size(); ++i)
        {
            m_Records[m_Inputs[i].RecordIndex].Agent->SetSteeringVelocity(m_Results[i]);
        }
    }
}
