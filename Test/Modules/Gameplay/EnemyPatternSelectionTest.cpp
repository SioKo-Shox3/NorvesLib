// 敵の攻撃選択の再現性・重み・クールダウン・反復抑制・距離帯を反証する。
#include "Gameplay/Enemy/EnemyPatternSelector.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Enemy;

#define PATTERN_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "敵パターン選択の条件違反 %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    EnemyDefinition MakeDefinition()
    {
        EnemyDefinition definition;
        definition.RepeatPenalty = 1.0f;
        EnemyAttackPattern pattern;
        pattern.Id = "near";
        pattern.Weight = 1.0f;
        pattern.MaxDistance = 3.0f;
        definition.Patterns.push_back(pattern);
        pattern.Id = "far";
        pattern.Weight = 3.0f;
        pattern.MinDistance = 3.0f;
        pattern.MaxDistance = 8.0f;
        definition.Patterns.push_back(pattern);
        return definition;
    }

    void SameSeedSequence()
    {
        auto definition = MakeDefinition();
        definition.RepeatPenalty = 0.25f;
        definition.Patterns[0].CooldownSeconds = 0.5f;
        definition.Patterns[1].CooldownSeconds = 0.75f;
        const uint32_t seed = MakeEnemyRandomSeed(0x0123456789abcdefULL, "enemy.alpha");
        EnemyPatternSelector first;
        EnemyPatternSelector second;
        first.Reset(definition, seed);
        second.Reset(definition, seed);
        PATTERN_CHECK(first.GetLastPatternIndex() == -1);
        for (int32_t i = 0; i < 1000; ++i)
        {
            PATTERN_CHECK(first.Select(0.0f, false) == second.Select(0.0f, false));
            first.Advance(0.25f);
            second.Advance(0.25f);
        }
        second.Reset(definition, seed);
        first.Reset(definition, seed);
        PATTERN_CHECK(first.GetCooldownRemaining(0) == 0.0f && first.GetLastPatternIndex() == -1);
        // 候補なし・開幕なしでは乱数列を消費しない。
        PATTERN_CHECK(first.Select(100.0f, true) == -1);
        PATTERN_CHECK(first.SelectOpening() == -1);
        PATTERN_CHECK(first.Select(0.0f, false) == second.Select(0.0f, false));
    }

    void DifferentSeedSequences()
    {
        const auto definition = MakeDefinition();
        EnemyPatternSelector first;
        EnemyPatternSelector otherRun;
        EnemyPatternSelector otherEnemy;
        first.Reset(definition, MakeEnemyRandomSeed(42, "enemy.alpha"));
        otherRun.Reset(definition, MakeEnemyRandomSeed(43, "enemy.alpha"));
        otherEnemy.Reset(definition, MakeEnemyRandomSeed(42, "enemy.beta"));
        bool bRunDiffers = false;
        bool bEnemyDiffers = false;
        for (int32_t i = 0; i < 1000; ++i)
        {
            const int32_t selected = first.Select(0.0f, false);
            bRunDiffers |= selected != otherRun.Select(0.0f, false);
            bEnemyDiffers |= selected != otherEnemy.Select(0.0f, false);
        }
        PATTERN_CHECK(bRunDiffers && bEnemyDiffers);
    }

    void WeightedFrequency()
    {
        const auto definition = MakeDefinition();
        EnemyPatternSelector selector;
        selector.Reset(definition, 100);
        int32_t secondCount = 0;
        for (int32_t i = 0; i < 10000; ++i)
        {
            const int32_t selected = selector.Select(0.0f, false);
            PATTERN_CHECK(selected == 0 || selected == 1);
            secondCount += selected == 1 ? 1 : 0;
        }
        const float fraction = static_cast<float>(secondCount) / 10000.0f;
        PATTERN_CHECK(fraction >= 0.72f && fraction <= 0.78f);
        std::printf("重み 3 の選択割合: %.4f\n", fraction);
    }

    void CooldownExclusion()
    {
        auto definition = MakeDefinition();
        definition.Patterns[0].CooldownSeconds = 2.0f;
        definition.Patterns[1].CooldownSeconds = 2.0f;
        definition.OpeningPatternId = "near";
        EnemyPatternSelector selector;
        selector.Reset(definition, 55);
        PATTERN_CHECK(selector.SelectOpening() == 0);
        PATTERN_CHECK(selector.GetLastPatternIndex() == 0 && selector.GetCooldownRemaining(0) == 2.0f);
        PATTERN_CHECK(selector.Select(0.0f, false) == 1);
        PATTERN_CHECK(selector.Select(0.0f, false) == -1);
        selector.Advance(-1.0f);
        selector.Advance(std::numeric_limits<float>::infinity());
        selector.Advance(std::numeric_limits<float>::quiet_NaN());
        PATTERN_CHECK(selector.GetCooldownRemaining(0) == 2.0f);
        selector.Advance(2.0f);
        PATTERN_CHECK(selector.GetCooldownRemaining(0) == 0.0f && selector.GetCooldownRemaining(1) == 0.0f);
        // 距離帯で単独候補にして、両方が候補へ戻ったことを確かめる。
        PATTERN_CHECK(selector.Select(1.0f, true) == 0);
        PATTERN_CHECK(selector.Select(5.0f, true) == 1);
        selector.Advance(3.0f);
        PATTERN_CHECK(selector.GetCooldownRemaining(0) == 0.0f);
    }

    void RepeatPenaltyExclusion()
    {
        auto definition = MakeDefinition();
        definition.RepeatPenalty = 0.0f;
        EnemyPatternSelector selector;
        selector.Reset(definition, 7);
        int32_t previous = -1;
        for (int32_t i = 0; i < 1000; ++i)
        {
            const int32_t selected = selector.Select(0.0f, false);
            PATTERN_CHECK(selected >= 0 && selected != previous);
            previous = selected;
        }
        // 残る候補の実効重みが 0 なら選択しない。
        const float distance = previous == 0 ? 1.0f : 5.0f;
        PATTERN_CHECK(selector.Select(distance, true) == -1);
        PATTERN_CHECK(selector.GetLastPatternIndex() == previous);
    }

    void DistanceBands()
    {
        const auto definition = MakeDefinition();
        EnemyPatternSelector selector;
        selector.Reset(definition, 11);
        for (int32_t i = 0; i < 100; ++i)
        {
            PATTERN_CHECK(selector.Select(1.0f, true) == 0);
            PATTERN_CHECK(selector.Select(5.0f, true) == 1);
        }
        PATTERN_CHECK(selector.Select(0.0f, true) == 0);
        PATTERN_CHECK(selector.Select(8.0f, true) == 1);
        PATTERN_CHECK(selector.Select(3.0f, true) >= 0);
        PATTERN_CHECK(selector.Select(9.0f, true) == -1);
        PATTERN_CHECK(selector.Select(-1.0f, true) == -1);
        PATTERN_CHECK(selector.Select(9.0f, false) >= 0);
    }
} // namespace

int main()
{
    SameSeedSequence();
    DifferentSeedSequences();
    WeightedFrequency();
    CooldownExclusion();
    RepeatPenaltyExclusion();
    DistanceBands();
    std::puts("EnemyPatternSelectionTest: PASS");
    return 0;
}
