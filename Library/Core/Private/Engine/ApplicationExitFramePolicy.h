#pragma once

#include "CoreTypes.h"

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Engine::Detail
{
    enum class ExitFrameArgumentKind : uint8_t
    {
        Unrelated,
        LegacyValid,
        LegacyInvalid,
        RenderedValid,
        RenderedInvalid,
        WaitForAssetSettle
    };

    enum class ExitFrameMetric : uint8_t
    {
        None,
        GameThreadFrames,
        RenderedFrames
    };

    struct ExitFrameOptionsAccumulator
    {
        uint64_t LegacyTarget = 0;
        uint64_t RenderedTarget = 0;
        bool bWaitForAssetSettle = false;
    };

    struct ExitFrameArgumentResult
    {
        ExitFrameArgumentKind Kind = ExitFrameArgumentKind::Unrelated;
        uint64_t Value = 0;
    };

    struct ExitFrameSelection
    {
        ExitFrameMetric Metric = ExitFrameMetric::None;
        uint64_t Target = 0;
        bool bWaitForAssetSettle = false;
    };

    inline bool MatchExitFramePrefix(const TCHAR* pText, const TCHAR* pPrefix)
    {
        if (!pText || !pPrefix)
        {
            return false;
        }

        for (; *pPrefix != TEXT('\0'); ++pText, ++pPrefix)
        {
            if (*pText != *pPrefix)
            {
                return false;
            }
        }
        return true;
    }

    inline uint64_t ParsePositiveExitFrameValue(const TCHAR* pText, bool& bValid)
    {
        bValid = false;
        if (!pText || *pText == TEXT('\0'))
        {
            return 0;
        }

        uint64_t value = 0;
        for (const TCHAR* pCharacter = pText; *pCharacter != TEXT('\0'); ++pCharacter)
        {
            if (*pCharacter < TEXT('0') || *pCharacter > TEXT('9'))
            {
                return 0;
            }

            const uint64_t digit = static_cast<uint64_t>(*pCharacter - TEXT('0'));
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            {
                return 0;
            }
            value = value * 10 + digit;
        }

        bValid = value > 0;
        return bValid ? value : 0;
    }

    inline ExitFrameArgumentResult AccumulateExitFrameArgument(ExitFrameOptionsAccumulator& options, const TCHAR* pArgument)
    {
        constexpr TCHAR legacyPrefix[] = TEXT("--exit-after-frames=");
        constexpr TCHAR renderedPrefix[] = TEXT("--exit-after-rendered-frames=");
        constexpr TCHAR waitOption[] = TEXT("--wait-for-asset-settle");

        if (MatchExitFramePrefix(pArgument, waitOption) && pArgument[sizeof(waitOption) / sizeof(TCHAR) - 1] == TEXT('\0'))
        {
            options.bWaitForAssetSettle = true;
            return {ExitFrameArgumentKind::WaitForAssetSettle, 0};
        }

        const bool bLegacy = MatchExitFramePrefix(pArgument, legacyPrefix);
        const bool bRendered = MatchExitFramePrefix(pArgument, renderedPrefix);
        if (!bLegacy && !bRendered)
        {
            return {};
        }

        const TCHAR* pValue = pArgument + (bLegacy ? sizeof(legacyPrefix) / sizeof(TCHAR) - 1 : sizeof(renderedPrefix) / sizeof(TCHAR) - 1);
        bool bValid = false;
        const uint64_t value = ParsePositiveExitFrameValue(pValue, bValid);
        if (!bValid)
        {
            return {bLegacy ? ExitFrameArgumentKind::LegacyInvalid : ExitFrameArgumentKind::RenderedInvalid, 0};
        }

        if (bLegacy)
        {
            options.LegacyTarget = value;
            return {ExitFrameArgumentKind::LegacyValid, value};
        }

        options.RenderedTarget = value;
        return {ExitFrameArgumentKind::RenderedValid, value};
    }

    constexpr ExitFrameSelection SelectExitFrameSelection(const ExitFrameOptionsAccumulator& options)
    {
        if (options.RenderedTarget > 0)
        {
            return {ExitFrameMetric::RenderedFrames, options.RenderedTarget, options.bWaitForAssetSettle};
        }
        if (options.LegacyTarget > 0)
        {
            return {ExitFrameMetric::GameThreadFrames, options.LegacyTarget, false};
        }
        return {};
    }

    inline void ObservePendingAssets(bool bPending, bool& bObserved, bool& bBaselineLatched)
    {
        if (bPending)
        {
            bObserved = true;
            bBaselineLatched = false;
        }
    }

    inline bool EvaluateSettledRenderedExit(bool bPendingAfterFlush, uint64_t rendered, uint64_t additionalTarget,
                                             bool& bObserved, bool& bBaselineLatched, uint64_t& baseline)
    {
        ObservePendingAssets(bPendingAfterFlush, bObserved, bBaselineLatched);
        if (bPendingAfterFlush || !bObserved)
        {
            return false;
        }
        if (!bBaselineLatched)
        {
            baseline = rendered;
            bBaselineLatched = true;
            return false;
        }
        if (rendered < baseline)
        {
            baseline = rendered;
            return false;
        }
        return rendered - baseline >= additionalTarget;
    }
} // namespace NorvesLib::Core::Engine::Detail
