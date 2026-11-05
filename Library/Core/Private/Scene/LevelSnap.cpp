// レベル配置対象を物理問い合わせで地面へ吸着し、離れを検査する。
#include "Scene/LevelSnap.h"
#include "Object/Entity.h"
#include "Scene/SceneQuery.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Scene::LevelSnap
{
    namespace
    {
        bool AreProbeDistancesValid(float maxDrop, float probeLift)
        {
            return std::isfinite(maxDrop) && maxDrop >= 0.0f
                && std::isfinite(probeLift) && probeLift >= 0.0f;
        }
    } // namespace

    bool SnapToGround(const SceneQuery& query, const Math::Vector3& position,
        Math::Vector3& outPosition, Math::Vector3& outNormal, float maxDrop, float probeLift)
    {
        if (!AreProbeDistancesValid(maxDrop, probeLift))
        {
            return false;
        }

        Math::Vector3 origin = position;
        origin.y += probeLift;

        PhysicsQueryDesc desc;
        desc.Kind = EPhysicsQueryKind::RaycastClosest;
        desc.Ray = Math::Ray(origin, Math::Vector3(0.0f, -1.0f, 0.0f));
        desc.MaxDistance = probeLift + maxDrop;
        desc.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Exclude;

        // 地面が物理の高さ場コライダーに変わっても、同じ問い合わせ口で検出する。
        Container::VariableArray<PhysicsQueryHit> hits;
        if (query.ExecuteQuery(desc, hits) != EPhysicsSceneQueryResult::Success || hits.empty())
        {
            return false;
        }

        outPosition = hits[0].Point;
        outNormal = hits[0].Normal;
        return true;
    }

    LevelSnapStats ReSnapAll(const SceneQuery& query, Container::Span<const LevelSnapTarget> targets,
        float maxDrop, float probeLift)
    {
        LevelSnapStats stats;
        const bool bValidDistances = AreProbeDistancesValid(maxDrop, probeLift);
        for (const LevelSnapTarget& target : targets)
        {
            if (target.Target == nullptr || !bValidDistances || !std::isfinite(target.GroundOffset))
            {
                ++stats.Missed;
                continue;
            }

            Math::Transform transform = target.Target->GetWorldTransform();
            Math::Vector3 groundPosition;
            Math::Vector3 groundNormal;
            if (!SnapToGround(query, transform.position, groundPosition, groundNormal, maxDrop, probeLift))
            {
                ++stats.Missed;
                continue;
            }

            transform.position.y = groundPosition.y + target.GroundOffset;
            target.Target->SetWorldTransform(transform);
            ++stats.Snapped;
        }
        return stats;
    }

    bool FindSnapDeviations(const SceneQuery& query, Container::Span<const LevelSnapTarget> targets,
        float maxDrop, float tolerance, Container::VariableArray<LevelSnapDeviation>& outDeviations,
        float probeLift)
    {
        outDeviations.clear();
        if (!AreProbeDistancesValid(maxDrop, probeLift) || !std::isfinite(tolerance) || tolerance < 0.0f)
        {
            return false;
        }

        for (const LevelSnapTarget& target : targets)
        {
            if (target.Target == nullptr)
            {
                continue;
            }

            const Math::Vector3 position = target.Target->GetWorldTransform().position;
            Math::Vector3 groundPosition;
            Math::Vector3 groundNormal;
            LevelSnapDeviation deviation;
            deviation.Target = target.Target;
            deviation.ActualY = position.y;
            if (!SnapToGround(query, position, groundPosition, groundNormal, maxDrop, probeLift))
            {
                deviation.bNoGround = true;
                deviation.Distance = std::isfinite(target.GroundOffset)
                    ? std::fabs(deviation.ActualY - deviation.ExpectedY)
                    : std::numeric_limits<float>::infinity();
                outDeviations.push_back(deviation);
                continue;
            }

            deviation.ExpectedY = groundPosition.y;
            if (!std::isfinite(target.GroundOffset))
            {
                deviation.Distance = std::numeric_limits<float>::infinity();
                outDeviations.push_back(deviation);
                continue;
            }

            deviation.ExpectedY += target.GroundOffset;
            deviation.Distance = std::fabs(deviation.ActualY - deviation.ExpectedY);
            if (deviation.Distance > tolerance)
            {
                outDeviations.push_back(deviation);
            }
        }
        return outDeviations.empty();
    }
} // namespace NorvesLib::Core::Scene::LevelSnap
