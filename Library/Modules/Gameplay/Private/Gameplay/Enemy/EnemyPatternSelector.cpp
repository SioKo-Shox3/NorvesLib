#include "Gameplay/Enemy/EnemyPatternSelector.h"

#include <cmath>

namespace NorvesLib::Modules::Gameplay::Enemy
{
    uint32_t MakeEnemyRandomSeed(uint64_t runSeed, const Core::Container::String& enemyId)
    {
        constexpr uint64_t kPrime = 1099511628211ULL;
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t shift = 0; shift < 64; shift += 8)
        {
            hash ^= (runSeed >> shift) & 0xffULL;
            hash *= kPrime;
        }
        for (const char byte : enemyId)
        {
            hash ^= static_cast<uint8_t>(byte);
            hash *= kPrime;
        }
        return static_cast<uint32_t>(hash >> 32) ^ static_cast<uint32_t>(hash);
    }

    void EnemyPatternSelector::Reset(const EnemyDefinition& definition, uint32_t seed)
    {
        m_Definition = &definition;
        m_Generator.SetSeed(seed);
        m_Cooldowns.assign(definition.Patterns.size(), 0.0f);
        m_LastPatternIndex = -1;
    }

    void EnemyPatternSelector::Advance(float deltaSeconds)
    {
        if (deltaSeconds < 0.0f || !std::isfinite(deltaSeconds))
        {
            return;
        }
        for (float& remaining : m_Cooldowns)
        {
            remaining = remaining > deltaSeconds ? remaining - deltaSeconds : 0.0f;
        }
    }

    float EnemyPatternSelector::GetCandidateWeight(int32_t index, float distance, bool bHasDistance) const
    {
        const auto& pattern = m_Definition->Patterns[index];
        if (m_Cooldowns[index] > 0.0f ||
            (bHasDistance && !(distance >= pattern.MinDistance && distance <= pattern.MaxDistance)))
        {
            return 0.0f;
        }
        return pattern.Weight * (index == m_LastPatternIndex ? m_Definition->RepeatPenalty : 1.0f);
    }

    int32_t EnemyPatternSelector::Select(float distance, bool bHasDistance)
    {
        if (m_Definition == nullptr)
        {
            return -1;
        }
        float total = 0.0f;
        const auto count = static_cast<int32_t>(m_Cooldowns.size());
        for (int32_t index = 0; index < count; ++index)
        {
            total += GetCandidateWeight(index, distance, bHasDistance);
        }
        if (total <= 0.0f)
        {
            return -1;
        }

        const float r = m_Generator.GetFloat(0.0f, total);
        float cumulative = 0.0f;
        int32_t lastPositiveIndex = -1;
        for (int32_t index = 0; index < count; ++index)
        {
            const float weight = GetCandidateWeight(index, distance, bHasDistance);
            if (weight <= 0.0f)
            {
                continue;
            }
            lastPositiveIndex = index;
            cumulative += weight;
            if (r < cumulative)
            {
                return CommitSelection(index);
            }
        }
        // 分布の丸めで上端値が返っても、末尾の重み 0 の候補は選ばない。
        return CommitSelection(lastPositiveIndex);
    }

    int32_t EnemyPatternSelector::SelectOpening()
    {
        if (m_Definition == nullptr || m_Definition->OpeningPatternId.empty())
        {
            return -1;
        }
        return CommitSelection(FindPatternIndex(*m_Definition, m_Definition->OpeningPatternId));
    }

    int32_t EnemyPatternSelector::CommitSelection(int32_t index)
    {
        if (index >= 0)
        {
            m_Cooldowns[index] = m_Definition->Patterns[index].CooldownSeconds;
            m_LastPatternIndex = index;
        }
        return index;
    }

    int32_t EnemyPatternSelector::GetLastPatternIndex() const
    {
        return m_LastPatternIndex;
    }

    float EnemyPatternSelector::GetCooldownRemaining(int32_t index) const
    {
        if (index < 0 || static_cast<size_t>(index) >= m_Cooldowns.size())
        {
            return 0.0f;
        }
        return m_Cooldowns[index];
    }
} // namespace NorvesLib::Modules::Gameplay::Enemy
