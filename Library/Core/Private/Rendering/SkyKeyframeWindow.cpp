// 太陽高度を挟むキーフレームと先読み・退去の対象を決める。
#include "Rendering/SkyKeyframeWindow.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 有限な float の和を二重精度で比較し、和の丸めに隠れた差も判定する。
        int CompareFloatSum(double left, double right, double target)
        {
            const double sum = left + right;
            if (sum < target)
            {
                return -1;
            }
            if (sum > target)
            {
                return 1;
            }
            const double rightPart = sum - left;
            const double error = (left - (sum - rightPart)) + (right - rightPart);
            if (error < 0.0)
            {
                return -1;
            }
            if (error > 0.0)
            {
                return 1;
            }
            return 0;
        }
    }

    Container::VariableArray<float> MakeDefaultSkyKeyframeAltitudes()
    {
        return Container::VariableArray<float>{
            0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
            8.0f, 10.0f, 12.0f, 15.0f, 18.0f, 22.0f, 26.0f, 30.0f,
            35.0f, 40.0f, 45.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f
        };
    }

    SkyKeyframeWindow::SkyKeyframeWindow()
    {
        m_Settings.AltitudesDegrees = MakeDefaultSkyKeyframeAltitudes();
        ClearKeyState();
    }

    bool SkyKeyframeWindow::Configure(const SkyKeyframeWindowSettings& settings)
    {
        bool bValid = true;
        if (settings.AltitudesDegrees.empty())
        {
            m_Settings.AltitudesDegrees = MakeDefaultSkyKeyframeAltitudes();
        }
        else
        {
            bool bValidAltitudes = settings.AltitudesDegrees.size() >= 2u;
            for (size_t index = 0u; bValidAltitudes && index < settings.AltitudesDegrees.size(); ++index)
            {
                const float altitude = settings.AltitudesDegrees[index];
                if (!std::isfinite(altitude) ||
                    (index > 0u && !(settings.AltitudesDegrees[index - 1u] < altitude)))
                {
                    bValidAltitudes = false;
                }
            }

            if (bValidAltitudes)
            {
                m_Settings.AltitudesDegrees = settings.AltitudesDegrees;
            }
            else
            {
                m_Settings.AltitudesDegrees = MakeDefaultSkyKeyframeAltitudes();
                bValid = false;
            }
        }

        if (std::isfinite(settings.JumpThresholdDegrees) && settings.JumpThresholdDegrees > 0.0f)
        {
            m_Settings.JumpThresholdDegrees = settings.JumpThresholdDegrees;
        }
        else
        {
            m_Settings.JumpThresholdDegrees = 10.0f;
            bValid = false;
        }

        if (settings.MaxPendingBuilds >= 1u)
        {
            m_Settings.MaxPendingBuilds = settings.MaxPendingBuilds;
        }
        else
        {
            m_Settings.MaxPendingBuilds = 2u;
            bValid = false;
        }

        ClearKeyState();
        ++m_Generation;
        return bValid;
    }

    uint32_t SkyKeyframeWindow::Reset(float anchorAltitudeDegrees)
    {
        const float anchor = std::isfinite(anchorAltitudeDegrees) ? anchorAltitudeDegrees : 0.0f;
        m_KeyAltitudes = m_Settings.AltitudesDegrees;
        const auto position = std::lower_bound(m_KeyAltitudes.begin(), m_KeyAltitudes.end(), anchor);
        m_AnchorIndex = static_cast<uint32_t>(position - m_KeyAltitudes.begin());
        if (position == m_KeyAltitudes.end() || *position != anchor)
        {
            m_KeyAltitudes.insert(position, anchor);
        }

        m_KeyStates.assign(m_KeyAltitudes.size(), KeyState{});
        m_LastAltitudeDegrees = anchor;
        m_LastIntervalLowerIndex = FindIntervalLowerIndex(anchor);
        m_LastSelection = SkyKeyframeSelection{};
        ++m_Generation;
        return m_AnchorIndex;
    }

    bool SkyKeyframeWindow::HasAnchor() const
    {
        return m_AnchorIndex != SkyKeyframeInvalidIndex;
    }

    uint32_t SkyKeyframeWindow::GetAnchorIndex() const
    {
        return m_AnchorIndex;
    }

    uint64_t SkyKeyframeWindow::GetGeneration() const
    {
        return m_Generation;
    }

    size_t SkyKeyframeWindow::GetKeyCount() const
    {
        return m_KeyAltitudes.size();
    }

    float SkyKeyframeWindow::GetKeyAltitude(uint32_t index) const
    {
        return index < m_KeyAltitudes.size() ? m_KeyAltitudes[index] : 0.0f;
    }

    bool SkyKeyframeWindow::IsAnchor(uint32_t index) const
    {
        return HasAnchor() && index == m_AnchorIndex;
    }

    bool SkyKeyframeWindow::IsJump(float altitudeDegrees) const
    {
        if (!HasAnchor())
        {
            return true;
        }
        if (!std::isfinite(altitudeDegrees))
        {
            return false;
        }
        const double altitude = static_cast<double>(altitudeDegrees);
        const double previous = static_cast<double>(m_LastAltitudeDegrees);
        const double threshold = static_cast<double>(m_Settings.JumpThresholdDegrees);
        return altitude >= previous ? CompareFloatSum(altitude, -previous, threshold) > 0
                                    : CompareFloatSum(previous, -altitude, threshold) > 0;
    }

    SkyKeyframeSelection SkyKeyframeWindow::Update(float altitudeDegrees)
    {
        const float altitude = std::isfinite(altitudeDegrees) ? altitudeDegrees : m_LastAltitudeDegrees;
        const uint32_t lower = FindIntervalLowerIndex(altitude);
        uint32_t upper = lower;
        if (altitude > m_KeyAltitudes.front() && altitude < m_KeyAltitudes.back())
        {
            upper = lower + 1u;
        }

        SkyKeyframeSelection selection;
        selection.KeyA = lower;
        selection.KeyB = upper;
        if (lower != upper)
        {
            const double lowAltitude = static_cast<double>(m_KeyAltitudes[lower]);
            const double highAltitude = static_cast<double>(m_KeyAltitudes[upper]);
            const double lowDistance = static_cast<double>(altitude) - lowAltitude;
            const double highDistance = highAltitude - static_cast<double>(altitude);
            const double interval = highAltitude - lowAltitude;
            // 小さい側を先に計算し、1 に近い値の丸めで寄与が消えるのを防ぐ。
            if (lowDistance <= highDistance)
            {
                selection.WeightB = static_cast<float>(lowDistance / interval);
                selection.WeightA = 1.0f - selection.WeightB;
            }
            else
            {
                selection.WeightA = static_cast<float>(highDistance / interval);
                selection.WeightB = 1.0f - selection.WeightA;
            }
        }

        const bool bResidentA = IsResident(lower);
        const bool bResidentB = IsResident(upper);
        if ((selection.WeightA == 0.0f || bResidentA) && (selection.WeightB == 0.0f || bResidentB))
        {
            if (selection.WeightA == 0.0f && !bResidentA)
            {
                selection.KeyA = selection.KeyB;
            }
            if (selection.WeightB == 0.0f && !bResidentB)
            {
                selection.KeyB = selection.KeyA;
            }
            selection.bValid = true;
        }
        else
        {
            selection.bClamped = true;
            m_bLateWarningRaised = true;
            uint32_t closest = SkyKeyframeInvalidIndex;
            if (bResidentA != bResidentB)
            {
                closest = bResidentA ? lower : upper;
            }
            else
            {
                for (size_t index = 0u; index < m_KeyStates.size(); ++index)
                {
                    if (!m_KeyStates[index].bResident)
                    {
                        continue;
                    }
                    const uint32_t candidate = static_cast<uint32_t>(index);
                    if (closest == SkyKeyframeInvalidIndex || IsCloserKey(candidate, closest, altitude))
                    {
                        closest = candidate;
                    }
                }
            }

            selection.KeyA = closest;
            selection.KeyB = closest;
            selection.WeightA = 1.0f;
            selection.WeightB = 0.0f;
            selection.bValid = closest != SkyKeyframeInvalidIndex;
        }

        m_LastAltitudeDegrees = altitude;
        m_LastIntervalLowerIndex = lower;
        m_LastSelection = selection;
        return selection;
    }

    void SkyKeyframeWindow::CollectRequests(Container::VariableArray<uint32_t>& outIndices) const
    {
        outIndices.clear();
        const size_t pendingCount = GetPendingCount();
        if (pendingCount >= m_Settings.MaxPendingBuilds)
        {
            return;
        }

        uint32_t first = 0u;
        uint32_t last = 0u;
        GetDesiredRange(first, last);
        uint32_t candidates[4] = {};
        size_t candidateCount = 0u;
        for (size_t index = first; index <= last; ++index)
        {
            const KeyState& state = m_KeyStates[index];
            if (!state.bResident && !state.bPending && !state.bFailed)
            {
                candidates[candidateCount++] = static_cast<uint32_t>(index);
            }
        }
        std::sort(candidates, candidates + candidateCount, [this](uint32_t left, uint32_t right)
        {
            return IsCloserKey(left, right, m_LastAltitudeDegrees);
        });

        const size_t remaining = static_cast<size_t>(m_Settings.MaxPendingBuilds) - pendingCount;
        for (size_t index = 0u; index < candidateCount && index < remaining; ++index)
        {
            outIndices.push_back(candidates[index]);
        }
    }

    void SkyKeyframeWindow::CollectEvictions(Container::VariableArray<uint32_t>& outIndices) const
    {
        outIndices.clear();
        uint32_t first = 0u;
        uint32_t last = 0u;
        GetDesiredRange(first, last);
        for (size_t index = 0u; index < m_KeyStates.size(); ++index)
        {
            if (m_KeyStates[index].bResident && (index < first || index > last) &&
                index != m_LastSelection.KeyA && index != m_LastSelection.KeyB)
            {
                outIndices.push_back(static_cast<uint32_t>(index));
            }
        }
    }

    void SkyKeyframeWindow::MarkPending(uint32_t index)
    {
        if (index < m_KeyStates.size())
        {
            m_KeyStates[index].bPending = true;
        }
    }

    void SkyKeyframeWindow::MarkResident(uint32_t index)
    {
        if (index < m_KeyStates.size())
        {
            m_KeyStates[index].bPending = false;
            m_KeyStates[index].bResident = true;
        }
    }

    void SkyKeyframeWindow::MarkFailed(uint32_t index)
    {
        if (index < m_KeyStates.size())
        {
            m_KeyStates[index].bPending = false;
            m_KeyStates[index].bFailed = true;
        }
    }

    void SkyKeyframeWindow::MarkEvicted(uint32_t index)
    {
        if (index < m_KeyStates.size())
        {
            m_KeyStates[index].bResident = false;
        }
    }

    bool SkyKeyframeWindow::IsResident(uint32_t index) const
    {
        return index < m_KeyStates.size() && m_KeyStates[index].bResident;
    }

    bool SkyKeyframeWindow::IsPending(uint32_t index) const
    {
        return index < m_KeyStates.size() && m_KeyStates[index].bPending;
    }

    size_t SkyKeyframeWindow::GetPendingCount() const
    {
        size_t count = 0u;
        for (const KeyState& state : m_KeyStates)
        {
            if (state.bPending)
            {
                ++count;
            }
        }
        return count;
    }

    bool SkyKeyframeWindow::ConsumeLateWarning()
    {
        if (m_bLateWarningRaised && !m_bLateWarningConsumed)
        {
            m_bLateWarningConsumed = true;
            return true;
        }
        return false;
    }

    void SkyKeyframeWindow::ClearKeyState()
    {
        m_KeyAltitudes = m_Settings.AltitudesDegrees;
        m_KeyStates.assign(m_KeyAltitudes.size(), KeyState{});
        m_LastAltitudeDegrees = 0.0f;
        m_LastIntervalLowerIndex = FindIntervalLowerIndex(m_LastAltitudeDegrees);
        m_LastSelection = SkyKeyframeSelection{};
        m_AnchorIndex = SkyKeyframeInvalidIndex;
    }

    uint32_t SkyKeyframeWindow::FindIntervalLowerIndex(float altitudeDegrees) const
    {
        if (altitudeDegrees <= m_KeyAltitudes.front())
        {
            return 0u;
        }
        if (altitudeDegrees >= m_KeyAltitudes.back())
        {
            return static_cast<uint32_t>(m_KeyAltitudes.size() - 1u);
        }
        const auto upper = std::upper_bound(m_KeyAltitudes.begin(), m_KeyAltitudes.end(), altitudeDegrees);
        return static_cast<uint32_t>(upper - m_KeyAltitudes.begin() - 1);
    }

    bool SkyKeyframeWindow::IsCloserKey(uint32_t left, uint32_t right, float altitudeDegrees) const
    {
        if (left == right)
        {
            return false;
        }
        const bool bLeftIsLower = left < right;
        const uint32_t lower = bLeftIsLower ? left : right;
        const uint32_t upper = bLeftIsLower ? right : left;
        if (altitudeDegrees <= m_KeyAltitudes[lower])
        {
            return bLeftIsLower;
        }
        if (altitudeDegrees >= m_KeyAltitudes[upper])
        {
            return !bLeftIsLower;
        }

        // 高度を挟む二つの距離は、中点との大小で比べる。
        const int comparison = CompareFloatSum(static_cast<double>(m_KeyAltitudes[lower]),
                                               static_cast<double>(m_KeyAltitudes[upper]),
                                               2.0 * static_cast<double>(altitudeDegrees));
        if (comparison == 0)
        {
            return bLeftIsLower;
        }
        return (comparison > 0) == bLeftIsLower;
    }

    void SkyKeyframeWindow::GetDesiredRange(uint32_t& first, uint32_t& last) const
    {
        first = m_LastIntervalLowerIndex > 0u ? m_LastIntervalLowerIndex - 1u : 0u;
        const size_t lastIndex = std::min(m_KeyAltitudes.size() - 1u,
                                          static_cast<size_t>(m_LastIntervalLowerIndex) + 2u);
        last = static_cast<uint32_t>(lastIndex);
    }
} // namespace NorvesLib::Core::Rendering の終端
