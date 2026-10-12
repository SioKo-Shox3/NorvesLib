#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    inline constexpr uint32_t MaximumGPUTimestampScopesPerFrame = 128u;

    struct GPUTimestampScopeHandle
    {
        uint32_t FrameSlotIndex = UINT32_MAX;
        uint32_t ScopeIndex = UINT32_MAX;
        uint64_t FrameNumber = 0u;

        bool IsValid() const
        {
            return FrameSlotIndex != UINT32_MAX &&
                   ScopeIndex < MaximumGPUTimestampScopesPerFrame;
        }
    };

    struct GPUTimestampResult
    {
        uint64_t FrameNumber = 0u;
        Core::Container::String ScopeName;
        float DurationMs = 0.0f;
        bool bValid = false;
        // 区間の開始と終了の GPU の時刻（tick。有効なビットで切った値）。同じキューの時刻はリストをまたいで比べられる
        uint64_t BeginTimestamp = 0u;
        uint64_t EndTimestamp = 0u;
        // 1 tick の長さ（ns）と時刻の有効なビット数。0 は時刻を持たない結果
        float TimestampPeriodNs = 0.0f;
        uint32_t TimestampValidBits = 0u;
    };

    // begin から end までの tick 数（有効なビットで一周する時刻の差）
    inline uint64_t CalculateGPUTimestampTickDelta(uint64_t begin, uint64_t end, uint32_t validBits)
    {
        if (validBits == 0u || validBits > 64u)
        {
            return 0u;
        }
        const uint64_t mask = validBits == 64u ? UINT64_MAX : ((uint64_t{1} << validBits) - 1u);
        return ((end & mask) - (begin & mask)) & mask;
    }

    /**
     * @brief リストごとに取った GPU の時間を、同じフレームの同じ区間の名前で 1 つに合わせる
     * @param perListResults 提出の順に並べたリストごとの結果（先頭のリストが先）
     * @param listCount リストの数
     * @param outResults 合わせた結果（最初に現れた順）
     *
     * あるリストの (フレーム, 名前) の区間は、それより前のリストから来た同じ (フレーム, 名前) の区間のうち、
     * まだこのリストの区間と合わせていない最初のものと 1 つになる（同じリストの中の同名の区間は別々に残る）。
     * 合わせた区間は、先頭のリストの開始から、合わせた区間のうち最も遅い終了まで。
     * 後のリストの区間が先頭の開始より前に始まる・自分の開始より前に終わる・時刻の単位が違う、
     * のどれかのときは順が壊れているので無効にする。1 つのリストにしか無い区間はそのまま通す。
     */
    inline void MergeGPUTimestampResultsAcrossCommandLists(
        const Core::Container::VariableArray<GPUTimestampResult>* perListResults,
        uint32_t listCount,
        Core::Container::VariableArray<GPUTimestampResult>& outResults)
    {
        outResults.clear();
        if (perListResults == nullptr)
        {
            return;
        }

        // 合わせた区間ごとに、最後に足したリストの番号
        Core::Container::VariableArray<uint32_t> lastListIndices;
        for (uint32_t listIndex = 0u; listIndex < listCount; ++listIndex)
        {
            for (const GPUTimestampResult& part : perListResults[listIndex])
            {
                size_t mergedIndex = outResults.size();
                for (size_t index = 0u; index < outResults.size(); ++index)
                {
                    if (lastListIndices[index] < listIndex &&
                        outResults[index].FrameNumber == part.FrameNumber &&
                        outResults[index].ScopeName == part.ScopeName)
                    {
                        mergedIndex = index;
                        break;
                    }
                }
                if (mergedIndex == outResults.size())
                {
                    outResults.push_back(part);
                    lastListIndices.push_back(listIndex);
                    continue;
                }

                GPUTimestampResult& merged = outResults[mergedIndex];
                lastListIndices[mergedIndex] = listIndex;
                if (!merged.bValid || !part.bValid ||
                    merged.TimestampValidBits == 0u ||
                    merged.TimestampValidBits != part.TimestampValidBits ||
                    merged.TimestampPeriodNs != part.TimestampPeriodNs)
                {
                    merged.bValid = false;
                    merged.DurationMs = 0.0f;
                    continue;
                }

                const uint32_t validBits = merged.TimestampValidBits;
                // 一周の半分を超える差は、前後が逆になったものとみなす
                const uint64_t halfRange = uint64_t{1} << (validBits - 1u);
                const uint64_t partBegin = CalculateGPUTimestampTickDelta(merged.BeginTimestamp, part.BeginTimestamp, validBits);
                const uint64_t partEnd = CalculateGPUTimestampTickDelta(merged.BeginTimestamp, part.EndTimestamp, validBits);
                const uint64_t mergedEnd = CalculateGPUTimestampTickDelta(merged.BeginTimestamp, merged.EndTimestamp, validBits);
                if (partBegin > partEnd || partEnd >= halfRange)
                {
                    merged.bValid = false;
                    merged.DurationMs = 0.0f;
                    continue;
                }
                if (partEnd > mergedEnd)
                {
                    merged.EndTimestamp = part.EndTimestamp;
                }
                const double durationMs =
                    static_cast<double>(CalculateGPUTimestampTickDelta(merged.BeginTimestamp, merged.EndTimestamp, validBits)) *
                    static_cast<double>(merged.TimestampPeriodNs) / 1000000.0;
                merged.DurationMs = static_cast<float>(durationMs);
            }
        }
    }
} // namespace NorvesLib::RHI
