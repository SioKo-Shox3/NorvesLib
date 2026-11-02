/**
 * @file DefeatBloodSequence.cpp
 * @brief 撃破の血の時間割の検証、合図の発行と滲み・不透明度の評価
 */

#include "Particle/DefeatBloodSequence.h"

#include <cmath>

namespace NorvesLib::Core::Particle
{
    bool DefeatBloodTimeline::IsValid() const
    {
        return std::isfinite(SprayDelaySeconds) && std::isfinite(BleedDelaySeconds) &&
            std::isfinite(BleedDurationSeconds) && std::isfinite(VanishDelaySeconds) &&
            std::isfinite(VanishDurationSeconds) && std::isfinite(MaxBleedRadiusMeters) &&
            SprayDelaySeconds >= 0.0f && BleedDelaySeconds >= SprayDelaySeconds &&
            VanishDelaySeconds >= BleedDelaySeconds && BleedDurationSeconds > 0.0f &&
            VanishDurationSeconds > 0.0f && MaxBleedRadiusMeters > 0.0f;
    }

    bool DefeatBloodSequence::Start(const DefeatBloodTimeline& timeline, const Math::Vector3& position,
        const Math::Vector3& normal)
    {
        if (!timeline.IsValid() || !std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z) || !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
            !std::isfinite(normal.z))
        {
            return false;
        }

        // 有限の float の極端に大きい・小さい法線でも、二乗の桁あふれやゼロへの丸めを避ける。
        const double lengthSquared = static_cast<double>(normal.x) * normal.x +
            static_cast<double>(normal.y) * normal.y + static_cast<double>(normal.z) * normal.z;
        if (lengthSquared == 0.0)
        {
            return false;
        }
        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        const Math::Vector3 normalizedNormal(static_cast<float>(normal.x * inverseLength),
            static_cast<float>(normal.y * inverseLength), static_cast<float>(normal.z * inverseLength));

        m_Timeline = timeline;
        m_Position = position;
        m_Normal = normalizedNormal;
        m_ElapsedSeconds = 0.0;
        m_TickIndex = 0u;
        m_NextCueIndex = 0u;
        m_bStarted = true;
        return true;
    }

    void DefeatBloodSequence::Tick(float deltaSeconds, Container::VariableArray<DefeatBloodCue>& outCues)
    {
        if (!IsActive())
        {
            return;
        }

        ++m_TickIndex;
        if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f)
        {
            m_ElapsedSeconds += static_cast<double>(deltaSeconds);
        }

        const double cueTimes[] = {
            static_cast<double>(m_Timeline.SprayDelaySeconds),
            static_cast<double>(m_Timeline.BleedDelaySeconds),
            static_cast<double>(m_Timeline.VanishDelaySeconds),
            static_cast<double>(m_Timeline.VanishDelaySeconds) + m_Timeline.VanishDurationSeconds
        };
        // 足し上げの丸めで合図が 1 刻み遅れないよう、到達の判定に 1e-9 秒を許容する。
        while (m_NextCueIndex < 4u && m_ElapsedSeconds + 1e-9 >= cueTimes[m_NextCueIndex])
        {
            DefeatBloodCue cue;
            cue.Type = static_cast<DefeatBloodCueType>(m_NextCueIndex);
            cue.TimeSeconds = cueTimes[m_NextCueIndex];
            cue.TickIndex = m_TickIndex;
            outCues.push_back(cue);
            ++m_NextCueIndex;
        }
    }

    bool DefeatBloodSequence::IsActive() const
    {
        return m_bStarted && m_NextCueIndex < 4u;
    }

    bool DefeatBloodSequence::IsFinished() const
    {
        return m_bStarted && m_NextCueIndex == 4u;
    }

    double DefeatBloodSequence::GetElapsedSeconds() const
    {
        return m_ElapsedSeconds;
    }

    float DefeatBloodSequence::GetBleedMask() const
    {
        return EvaluateBleedMask(m_Timeline, m_ElapsedSeconds);
    }

    float DefeatBloodSequence::GetBleedRadiusMeters() const
    {
        return m_Timeline.MaxBleedRadiusMeters * GetBleedMask();
    }

    float DefeatBloodSequence::GetOpacity() const
    {
        return EvaluateOpacity(m_Timeline, m_ElapsedSeconds);
    }

    const Math::Vector3& DefeatBloodSequence::GetPosition() const
    {
        return m_Position;
    }

    const Math::Vector3& DefeatBloodSequence::GetNormal() const
    {
        return m_Normal;
    }

    const DefeatBloodTimeline& DefeatBloodSequence::GetTimeline() const
    {
        return m_Timeline;
    }

    float DefeatBloodSequence::EvaluateBleedMask(const DefeatBloodTimeline& timeline, double elapsedSeconds)
    {
        if (elapsedSeconds <= timeline.BleedDelaySeconds)
        {
            return 0.0f;
        }
        const double timeSinceBleed = elapsedSeconds - timeline.BleedDelaySeconds;
        if (timeSinceBleed >= timeline.BleedDurationSeconds)
        {
            return 1.0f;
        }
        const double remaining = 1.0 - timeSinceBleed / timeline.BleedDurationSeconds;
        return static_cast<float>(1.0 - remaining * remaining);
    }

    float DefeatBloodSequence::EvaluateOpacity(const DefeatBloodTimeline& timeline, double elapsedSeconds)
    {
        if (elapsedSeconds <= timeline.VanishDelaySeconds)
        {
            return 1.0f;
        }
        const double timeSinceVanish = elapsedSeconds - timeline.VanishDelaySeconds;
        if (timeSinceVanish >= timeline.VanishDurationSeconds)
        {
            return 0.0f;
        }
        return static_cast<float>(1.0 - timeSinceVanish / timeline.VanishDurationSeconds);
    }
} // namespace NorvesLib::Core::Particle
