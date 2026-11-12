// 匂いの痕跡の制御点収集と一様 Catmull-Rom 補間を実装する。
#include "Gameplay/Level/ScentTrailComponent.h"

#include "Object/Entity.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    namespace
    {
        template<typename T>
        T EvaluateSpline(Core::Container::Span<const T> points, float t)
        {
            if (points.empty())
            {
                return T{};
            }
            if (points.size() == 1 || !(t > 0.0f))
            {
                return points[0];
            }
            const size_t last = points.size() - 1;
            if (t >= static_cast<float>(last))
            {
                return points[last];
            }

            const size_t index = static_cast<size_t>(t);
            const float u = t - static_cast<float>(index);
            const T& p0 = points[index == 0 ? 0 : index - 1];
            const T& p1 = points[index];
            const T& p2 = points[index + 1];
            const T& p3 = points[index + 2 > last ? last : index + 2];
            // 接線を隣の制御点の差の半分とし、区間内を三次式で補間する。
            return p1 + ((p2 - p0) + ((p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3)
                + (p1 * 3.0f - p2 * 3.0f + p3 - p0) * u) * u) * (0.5f * u);
        }
    }

    IMPLEMENT_CLASS(ScentTrailComponent, Core::Component::Component)
    IMPLEMENT_CLASS(ScentTrailPointComponent, Core::Component::Component)

    ScentTrailComponent::ScentTrailComponent()
    {
        TargetKey = "";
        Width = 1.0f;
    }

    ScentTrailPointComponent::ScentTrailPointComponent()
    {
        Intensity = 1.0f;
    }

    void ScentTrailComponent::GatherPoints(Core::Container::VariableArray<Math::Vector3>& outPositions,
                                          Core::Container::VariableArray<float>& outIntensities) const
    {
        outPositions.clear();
        outIntensities.clear();
        const Core::Entity* owner = GetOwner();
        if (owner == nullptr)
        {
            return;
        }
        for (const Core::Entity* child : owner->GetChildEntities())
        {
            if (const auto* point = child->GetComponent<ScentTrailPointComponent>())
            {
                outPositions.push_back(child->GetPosition());
                const float intensity = point->getIntensity();
                outIntensities.push_back(intensity);
            }
        }
    }

    Math::Vector3 EvaluateCatmullRom(Core::Container::Span<const Math::Vector3> points, float t)
    {
        return EvaluateSpline(points, t);
    }

    float EvaluateCatmullRom(Core::Container::Span<const float> points, float t)
    {
        return EvaluateSpline(points, t);
    }
} // namespace NorvesLib::Modules::Gameplay::Level
