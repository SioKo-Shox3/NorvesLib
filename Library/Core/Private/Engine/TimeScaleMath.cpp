#include "Engine/TimeScaleMath.h"
#include "Container/VariableArray.h"
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Engine
{
    TimeScaleMathResult QuantizeTimeScale(double scale, uint32_t& out)
    {
        if (!std::isfinite(scale) || scale < 0)
            return TimeScaleMathResult::InvalidArgument;
        const double rounded = std::round(scale * TimeScaleDenominator);
        if (!std::isfinite(rounded) || rounded > std::numeric_limits<uint32_t>::max())
            return TimeScaleMathResult::Overflow;
        out = static_cast<uint32_t>(rounded);
        return TimeScaleMathResult::Success;
    }
    TimeScaleMathResult ComposeTimeScales(Container::Span<const uint32_t> numerators, uint32_t& out)
    {
        if (numerators.empty())
        {
            out = TimeScaleDenominator;
            return TimeScaleMathResult::Success;
        }
        for (const auto value : numerators)
            if (value == 0)
            {
                out = 0;
                return TimeScaleMathResult::Success;
            }
        // 積は任意長整数。大きい倍率の後に小さい倍率が来ても中間overflowで拒否しない。
        size_t count = 0;
        for (const auto value : numerators)
            if (value != TimeScaleDenominator)
                ++count;
        if (count == 0)
        {
            out = TimeScaleDenominator;
            return TimeScaleMathResult::Success;
        }
        if (count > (std::numeric_limits<size_t>::max() - 32) / 32)
            return TimeScaleMathResult::Overflow;
        Container::VariableArray<uint32_t> limbs;
        limbs.push_back(1);
        for (const auto value : numerators)
        {
            if (value == TimeScaleDenominator)
                continue;
            uint64_t carry = 0;
            for (auto& limb : limbs)
            {
                const uint64_t product = uint64_t(limb) * value + carry;
                limb = static_cast<uint32_t>(product);
                carry = product >> 32;
            }
            if (carry)
                limbs.push_back(static_cast<uint32_t>(carry));
        }
        const size_t shift = 16 * (count - 1);
        const auto bit = [&](size_t at) { return at / 32 < limbs.size() && ((limbs[at / 32] >> (at % 32)) & 1u) != 0; };
        size_t bits = (limbs.size() - 1) * 32;
        for (uint32_t top = limbs.back(); top; top >>= 1)
            ++bits;
        if (bits > shift + 32)
            return TimeScaleMathResult::Overflow;
        uint32_t result = 0;
        for (unsigned i = 0; i < 32; ++i)
            if (bit(shift + i))
                result |= uint32_t(1) << i;
        if (shift && bit(shift - 1))
        {
            if (result == std::numeric_limits<uint32_t>::max())
                return TimeScaleMathResult::Overflow;
            ++result;
        }
        out = result;
        return TimeScaleMathResult::Success;
    }
    TimeScaleMathResult ScaleTimeNanoseconds(int64_t raw, uint32_t numerator, uint32_t& carry, int64_t& out)
    {
        if (raw < 0 || carry >= TimeScaleDenominator)
            return TimeScaleMathResult::InvalidArgument;
        constexpr uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        const uint64_t whole = static_cast<uint64_t>(raw) / TimeScaleDenominator;
        const uint64_t remainder = static_cast<uint64_t>(raw) % TimeScaleDenominator;
        if (numerator && whole > maximum / numerator)
            return TimeScaleMathResult::Overflow;
        const uint64_t integral = whole * numerator;
        const uint64_t fractional = remainder * numerator + carry;
        const uint64_t additional = fractional / TimeScaleDenominator;
        if (additional > maximum - integral)
            return TimeScaleMathResult::Overflow;
        const auto nextCarry = static_cast<uint32_t>(fractional % TimeScaleDenominator);
        out = static_cast<int64_t>(integral + additional);
        carry = nextCarry;
        return TimeScaleMathResult::Success;
    }
    TimeScaleMathResult AccumulateTimeSegments(Container::Span<const ConstantTimeScaleSegment> segments,
                                               uint32_t& carry, int64_t& out)
    {
        if (carry >= TimeScaleDenominator)
            return TimeScaleMathResult::InvalidArgument;
        uint32_t candidateCarry = carry;
        int64_t total = 0;
        for (const auto& segment : segments)
        {
            int64_t value = 0;
            const auto status =
                ScaleTimeNanoseconds(segment.DeltaNanoseconds, segment.Numerator, candidateCarry, value);
            if (status != TimeScaleMathResult::Success)
                return status;
            if (value > std::numeric_limits<int64_t>::max() - total)
                return TimeScaleMathResult::Overflow;
            total += value;
        }
        out = total;
        carry = candidateCarry;
        return TimeScaleMathResult::Success;
    }
} // namespace NorvesLib::Core::Engine
