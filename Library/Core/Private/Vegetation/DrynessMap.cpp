#include "Vegetation/DrynessMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Vegetation
{
    namespace
    {
        /** @brief 枯れ度が有限かつ有効な範囲に収まるかを返す。 */
        bool IsValidLevel(float level)
        {
            return std::isfinite(level) && level >= 0.0f && level <= 1.0f;
        }

        /** @brief 源の中心から焦点までの距離の二乗を求める。 */
        double DistanceSquared(const DrynessSource& source, float focusX, float focusZ)
        {
            const double dx = static_cast<double>(source.CenterX) - static_cast<double>(focusX);
            const double dz = static_cast<double>(source.CenterZ) - static_cast<double>(focusZ);
            return dx * dx + dz * dz;
        }
    } // namespace

    DrynessSourceId DrynessMap::AddCircle(float centerX, float centerZ, float radius, float falloff, float level)
    {
        const DrynessSourceId id = AddEllipse(centerX, centerZ, radius, radius, 0.0f, falloff, level);
        if (id != InvalidDrynessSourceId)
        {
            m_Sources.back().Shape = DrynessShape::Circle;
        }
        return id;
    }

    DrynessSourceId DrynessMap::AddEllipse(float centerX, float centerZ, float radiusA, float radiusB, float yaw,
                                           float falloff, float level)
    {
        if (!std::isfinite(centerX) || !std::isfinite(centerZ) ||
            !std::isfinite(radiusA) || radiusA <= 0.0f || !std::isfinite(radiusB) || radiusB <= 0.0f ||
            !std::isfinite(yaw) || !std::isfinite(falloff) || falloff < 0.0f || !IsValidLevel(level) ||
            m_NextId == std::numeric_limits<DrynessSourceId>::max())
        {
            return InvalidDrynessSourceId;
        }

        DrynessSource source;
        source.Id = m_NextId;
        source.Shape = DrynessShape::Ellipse;
        source.CenterX = centerX;
        source.CenterZ = centerZ;
        source.RadiusA = radiusA;
        source.RadiusB = radiusB;
        source.Yaw = yaw;
        source.Falloff = falloff;
        source.TargetLevel = level;
        source.CurrentLevel = level;
        m_Sources.push_back(source);
        ++m_NextId;
        return source.Id;
    }

    bool DrynessMap::SetTarget(DrynessSourceId id, float level, float seconds)
    {
        if (!IsValidLevel(level) || !std::isfinite(seconds) || seconds < 0.0f)
        {
            return false;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                source.TargetLevel = level;
                source.TransitionSeconds = seconds;
                source.RemainingSeconds = seconds;
                if (seconds == 0.0f)
                {
                    source.CurrentLevel = level;
                }
                return true;
            }
        }
        return false;
    }

    bool DrynessMap::SetSourceCenter(DrynessSourceId id, float centerX, float centerZ)
    {
        if (!std::isfinite(centerX) || !std::isfinite(centerZ))
        {
            return false;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                source.CenterX = centerX;
                source.CenterZ = centerZ;
                return true;
            }
        }
        return false;
    }

    bool DrynessMap::Remove(DrynessSourceId id)
    {
        for (auto it = m_Sources.begin(); it != m_Sources.end(); ++it)
        {
            if (it->Id == id)
            {
                m_Sources.erase(it);
                return true;
            }
        }
        return false;
    }

    void DrynessMap::Clear()
    {
        m_Sources.clear();
    }

    void DrynessMap::Tick(float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.RemainingSeconds > 0.0f)
            {
                if (deltaSeconds >= source.RemainingSeconds)
                {
                    source.CurrentLevel = source.TargetLevel;
                    source.RemainingSeconds = 0.0f;
                }
                else
                {
                    source.CurrentLevel += (source.TargetLevel - source.CurrentLevel) *
                        (deltaSeconds / source.RemainingSeconds);
                    source.RemainingSeconds -= deltaSeconds;
                }
            }
        }
    }

    float DrynessMap::Sample(float x, float z) const
    {
        if (!std::isfinite(x) || !std::isfinite(z))
        {
            return 0.0f;
        }

        float result = 0.0f;
        for (const DrynessSource& source : m_Sources)
        {
            float dx = x - source.CenterX;
            float dz = z - source.CenterZ;
            const float cosine = std::cos(source.Yaw);
            const float sine = std::sin(source.Yaw);
            float lx = dx * cosine + dz * sine;
            float lz = -dx * sine + dz * cosine;
            // float の二乗によるあふれや消失を避けて長さを求める。
            float dist = std::hypot(lx, lz);
            float distanceScale = 1.0f;
            if (!std::isfinite(dist))
            {
                // 差や回転後の長さがあふれる場合は 1/4 に縮めて評価する。
                dx = x * 0.25f - source.CenterX * 0.25f;
                dz = z * 0.25f - source.CenterZ * 0.25f;
                lx = dx * cosine + dz * sine;
                lz = -dx * sine + dz * cosine;
                dist = std::hypot(lx, lz);
                distanceScale = 4.0f;
            }
            const float normalizedX = (lx / source.RadiusA) * distanceScale;
            const float normalizedZ = (lz / source.RadiusB) * distanceScale;
            const float rho = std::hypot(normalizedX, normalizedZ);

            float attenuation = 1.0f;
            if (rho > 1.0f)
            {
                if (source.Falloff == 0.0f)
                {
                    attenuation = 0.0f;
                }
                else
                {
                    const float edgeDistance = dist * (1.0f - 1.0f / rho);
                    const float t = std::clamp((edgeDistance / source.Falloff) * distanceScale, 0.0f, 1.0f);
                    attenuation = 1.0f - t * t * (3.0f - 2.0f * t);
                }
            }
            result = std::max(result, source.CurrentLevel * attenuation);
        }
        return result;
    }

    const DrynessSource* DrynessMap::Find(DrynessSourceId id) const
    {
        for (const DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                return &source;
            }
        }
        return nullptr;
    }

    Container::Span<const DrynessSource> DrynessMap::GetSources() const
    {
        return Container::Span<const DrynessSource>(m_Sources.data(), m_Sources.size());
    }

    size_t DrynessMap::GetSourceCount() const
    {
        return m_Sources.size();
    }

    void DrynessMap::SetGpuSourceLimit(uint32_t limit)
    {
        m_GpuSourceLimit = limit;
    }

    uint32_t DrynessMap::GetGpuSourceLimit() const
    {
        return m_GpuSourceLimit;
    }

    void DrynessMap::CollectGpuSources(float focusX, float focusZ, Container::VariableArray<DrynessSource>& out) const
    {
        out.clear();
        const size_t count = std::min(m_Sources.size(), static_cast<size_t>(m_GpuSourceLimit));
        if (count == 0)
        {
            return;
        }

        out.resize(count);
        std::partial_sort_copy(m_Sources.begin(), m_Sources.end(), out.begin(), out.end(),
            [focusX, focusZ](const DrynessSource& lhs, const DrynessSource& rhs)
            {
                const double leftDistance = DistanceSquared(lhs, focusX, focusZ);
                const double rightDistance = DistanceSquared(rhs, focusX, focusZ);
                if (leftDistance < rightDistance)
                {
                    return true;
                }
                if (rightDistance < leftDistance)
                {
                    return false;
                }
                return lhs.Id < rhs.Id;
            });
    }
} // namespace NorvesLib::Core::Vegetation
