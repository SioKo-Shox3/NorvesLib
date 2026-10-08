#pragma once
// double資産変換で用いるthreadのFP前提。設定は変更しない。
#include <cmath>
#include <cfenv>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#define NORVES_SKELETAL_MXCSR 1
#else
#define NORVES_SKELETAL_MXCSR 0
#endif

namespace NorvesLib::Core::Animation::Detail
{
    static inline bool SupportedSkeletalFloatEnvironment() noexcept
    {
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0) ||    \
    defined(__ASSOCIATIVE_MATH__) || defined(__RECIPROCAL_MATH__) || defined(__NO_SIGNED_ZEROS__)
        return false;
#else
        if (!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits != 53 ||
            std::fegetround() != FE_TONEAREST)
        {
            return false;
        }
#if NORVES_SKELETAL_MXCSR
        // x87側だけnearestでも、SSE側の丸めが変更されていれば受理しない。
        if ((_mm_getcsr() & 0x6000u) != 0)
        {
            return false;
        }
#endif
        // コンパイル時の定数畳み込みを避け、現在のthreadのFTZ/DAZも検出する。
        volatile double minimum = std::numeric_limits<double>::min();
        volatile double half = 0.5;
        volatile double subnormal = minimum * half;
        volatile double tiny = std::numeric_limits<double>::denorm_min();
        volatile double one = 1.0;
        volatile double preserved = tiny * one;
        return subnormal != 0 && preserved != 0;
#endif
    }
} // namespace NorvesLib::Core::Animation::Detail
#undef NORVES_SKELETAL_MXCSR
