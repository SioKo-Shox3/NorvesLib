// 物理スナップショットから足元の接地点を問い合わせる。
#include "Animation/GroundQuery.h"
#include "Scene/SceneQuery.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        bool IsValidInput(const Math::Vector3& originAbove, float maxDown)
        {
            return std::isfinite(originAbove.x) && std::isfinite(originAbove.y) && std::isfinite(originAbove.z)
                && std::isfinite(maxDown) && maxDown > 0.0f;
        }

        Scene::PhysicsQueryDesc MakeQuery(const Math::Vector3& originAbove, float maxDown,
            const PhysicsGroundQuerySettings& settings)
        {
            Scene::PhysicsQueryDesc query;
            query.Kind = Scene::EPhysicsQueryKind::RaycastClosest;
            query.Ray = Math::Ray(originAbove, Math::Vector3(0.0f, -1.0f, 0.0f));
            query.MaxDistance = maxDown;
            query.MaxHits = 1;
            query.Filter.LayerMask = settings.LayerMask;
            query.Filter.TriggerPolicy = Scene::EPhysicsQueryTriggerPolicy::Exclude;
            for (size_t index = 0; index < 4; ++index)
            {
                query.Filter.IgnoreBodies[index] = settings.IgnoreBodies[index];
                query.Filter.IgnoreColliders[index] = settings.IgnoreColliders[index];
            }
            return query;
        }

        EGroundQueryStatus ToGroundStatus(Scene::EPhysicsSceneQueryResult result)
        {
            switch (result)
            {
            case Scene::EPhysicsSceneQueryResult::Success:
                return EGroundQueryStatus::Hit;
            case Scene::EPhysicsSceneQueryResult::NoHit:
                return EGroundQueryStatus::NoHit;
            case Scene::EPhysicsSceneQueryResult::NotReady:
                return EGroundQueryStatus::NotReady;
            case Scene::EPhysicsSceneQueryResult::WrongThread:
                return EGroundQueryStatus::WrongThread;
            default:
                return EGroundQueryStatus::Unavailable;
            }
        }

        GroundHit MakeGroundHit(const Scene::PhysicsQueryHit& hit, float maxSlopeDeg)
        {
            GroundHit result;
            if (!std::isfinite(hit.Point.x) || !std::isfinite(hit.Point.y) || !std::isfinite(hit.Point.z)
                || !std::isfinite(hit.Distance) || !std::isfinite(hit.Normal.x)
                || !std::isfinite(hit.Normal.y) || !std::isfinite(hit.Normal.z))
            {
                result.Status = EGroundQueryStatus::Unavailable;
                return result;
            }

            const double x = hit.Normal.x;
            const double y = hit.Normal.y;
            const double z = hit.Normal.z;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length <= 1e-6)
            {
                result.Status = EGroundQueryStatus::Unavailable;
                return result;
            }

            result.Point = hit.Point;
            result.Normal = Math::Vector3(static_cast<float>(x / length), static_cast<float>(y / length),
                static_cast<float>(z / length));
            result.Distance = hit.Distance;
            result.Status = EGroundQueryStatus::Hit;

            constexpr double RadiansToDegrees = 180.0 / 3.14159265358979323846;
            const double slopeDeg = std::acos(std::clamp(y / length, -1.0, 1.0)) * RadiansToDegrees;
            if (slopeDeg > static_cast<double>(maxSlopeDeg))
            {
                result.Status = EGroundQueryStatus::TooSteep;
            }
            return result;
        }
    }

    PhysicsGroundQuery::PhysicsGroundQuery(const Scene::SceneQuery& query, const PhysicsGroundQuerySettings& settings)
        : m_Query(query), m_Settings(settings)
    {
    }

    EGroundQueryStatus PhysicsGroundQuery::Query(const Math::Vector3& originAbove, float maxDown, GroundHit& out) const
    {
        if (!IsValidInput(originAbove, maxDown))
        {
            out = GroundHit{};
            out.Status = EGroundQueryStatus::Unavailable;
            return out.Status;
        }

        const Scene::PhysicsQueryDesc query = MakeQuery(originAbove, maxDown, m_Settings);
        const Scene::EPhysicsSceneQueryResult status = m_Query.ExecuteQuery(query, m_Hits);
        GroundHit result;
        result.Status = ToGroundStatus(status);
        if (status == Scene::EPhysicsSceneQueryResult::Success)
        {
            if (m_Hits.empty())
            {
                result.Status = EGroundQueryStatus::NoHit;
            }
            else
            {
                result = MakeGroundHit(m_Hits[0], m_Settings.MaxSlopeDeg);
            }
        }
        out = result;
        return out.Status;
    }

    void PhysicsGroundQuery::QueryBatch(Container::Span<GroundProbe> probes) const
    {
        if (probes.empty())
        {
            return;
        }

        m_Queries.clear();
        for (GroundProbe& probe : probes)
        {
            probe.Result = GroundHit{};
            if (!IsValidInput(probe.OriginAbove, probe.MaxDown))
            {
                probe.Result.Status = EGroundQueryStatus::Unavailable;
                continue;
            }
            m_Queries.push_back(MakeQuery(probe.OriginAbove, probe.MaxDown, m_Settings));
        }
        if (m_Queries.empty())
        {
            return;
        }

        const Scene::EPhysicsSceneQueryResult status = m_Query.ExecuteBatch(
            Container::Span<const Scene::PhysicsQueryDesc>(m_Queries), m_Hits, m_Results);
        size_t queryIndex = 0;
        for (GroundProbe& probe : probes)
        {
            if (!IsValidInput(probe.OriginAbove, probe.MaxDown))
            {
                continue;
            }
            if (status != Scene::EPhysicsSceneQueryResult::Success)
            {
                probe.Result.Status = ToGroundStatus(status);
            }
            else if (queryIndex >= m_Results.size())
            {
                probe.Result.Status = EGroundQueryStatus::Unavailable;
            }
            else
            {
                const Scene::PhysicsQueryBatchResult& item = m_Results[queryIndex];
                probe.Result.Status = ToGroundStatus(item.Result);
                if (item.Result == Scene::EPhysicsSceneQueryResult::Success)
                {
                    if (item.HitCount == 0)
                    {
                        probe.Result.Status = EGroundQueryStatus::NoHit;
                    }
                    else if (item.FirstHit >= m_Hits.size())
                    {
                        probe.Result.Status = EGroundQueryStatus::Unavailable;
                    }
                    else
                    {
                        probe.Result = MakeGroundHit(m_Hits[item.FirstHit], m_Settings.MaxSlopeDeg);
                    }
                }
            }
            ++queryIndex;
        }
    }

    const PhysicsGroundQuerySettings& PhysicsGroundQuery::GetSettings() const
    {
        return m_Settings;
    }
} // namespace NorvesLib::Core::Animation
