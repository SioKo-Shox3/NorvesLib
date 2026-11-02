/**
 * @file DefeatBloodSequenceTest.cpp
 * @brief 撃破の血の合図の時刻・固定刻みの再現性と滲みの単調性の CPU 試験
 */

#include "Particle/DefeatBloodSequence.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Particle;
using NorvesLib::Core::Container::VariableArray;
using NorvesLib::Math::Vector3;

namespace
{
    VariableArray<DefeatBloodCue> RunSequence(float deltaSeconds)
    {
        const DefeatBloodTimeline timeline;
        DefeatBloodSequence sequence;
        const bool bStarted = sequence.Start(timeline, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        assert(bStarted);
        VariableArray<DefeatBloodCue> cues;
        float previousMask = sequence.GetBleedMask();
        for (uint32_t tick = 0u; tick < 1000u && sequence.IsActive(); ++tick)
        {
            sequence.Tick(deltaSeconds, cues);
            const float mask = sequence.GetBleedMask();
            assert(mask >= previousMask);
            const double elapsed = sequence.GetElapsedSeconds();
            if (elapsed < timeline.BleedDelaySeconds)
            {
                assert(mask == 0.0f);
            }
            if (elapsed >= static_cast<double>(timeline.BleedDelaySeconds) + timeline.BleedDurationSeconds)
            {
                // 広がりきった後は、消滅の間も滲みの半径を保つ。
                assert(mask == 1.0f);
            }
            assert(sequence.GetBleedRadiusMeters() == timeline.MaxBleedRadiusMeters * mask);
            previousMask = mask;
        }
        assert(sequence.IsFinished());
        return cues;
    }
} // namespace

int main()
{
    // 同じ固定刻みなら合図の型・時刻・刻み番号が再現し、各合図は最初に届く刻みで 1 回ずつ出る。
    const DefeatBloodTimeline timeline;
    constexpr float deltaSeconds = 1.0f / 60.0f;
    const auto first = RunSequence(deltaSeconds);
    const auto second = RunSequence(deltaSeconds);
    assert(first.size() == 4u);
    assert(second.size() == first.size());
    const DefeatBloodCueType expectedTypes[] = {
        DefeatBloodCueType::Spray, DefeatBloodCueType::Bleed,
        DefeatBloodCueType::Vanish, DefeatBloodCueType::Finished
    };
    const double expectedTimes[] = {
        static_cast<double>(timeline.SprayDelaySeconds),
        static_cast<double>(timeline.BleedDelaySeconds),
        static_cast<double>(timeline.VanishDelaySeconds),
        static_cast<double>(timeline.VanishDelaySeconds) + timeline.VanishDurationSeconds
    };
    for (uint32_t index = 0u; index < 4u; ++index)
    {
        assert(first[index].Type == expectedTypes[index]);
        assert(first[index].TimeSeconds == expectedTimes[index]);
        const double firstTick = std::ceil((expectedTimes[index] - 1e-9) / static_cast<double>(deltaSeconds));
        const uint64_t expectedTick = firstTick < 1.0 ? 1u : static_cast<uint64_t>(firstTick);
        assert(first[index].TickIndex == expectedTick);
        assert(first[index].Type == second[index].Type);
        assert(first[index].TimeSeconds == second[index].TimeSeconds);
        assert(first[index].TickIndex == second[index].TickIndex);
    }

    // 刻み幅を変えても時間割そのものの時刻は変わらない。
    const auto slower = RunSequence(1.0f / 30.0f);
    assert(slower.size() == first.size());
    for (uint32_t index = 0u; index < 4u; ++index)
    {
        assert(slower[index].TimeSeconds == first[index].TimeSeconds);
    }

    // 不正な順序・長さの時間割と、長さゼロの法線を拒む。
    DefeatBloodSequence sequence;
    DefeatBloodTimeline invalid = timeline;
    invalid.SprayDelaySeconds = 1.0f;
    bool bStarted = sequence.Start(invalid, Vector3(), Vector3(0.0f, 1.0f, 0.0f));
    assert(!bStarted);
    invalid = timeline;
    invalid.BleedDurationSeconds = 0.0f;
    bStarted = sequence.Start(invalid, Vector3(), Vector3(0.0f, 1.0f, 0.0f));
    assert(!bStarted);
    bStarted = sequence.Start(timeline, Vector3(), Vector3());
    assert(!bStarted);

    std::cout << "DefeatBloodSequenceTest: 検査に合格しました\n";
    return 0;
}
