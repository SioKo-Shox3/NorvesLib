#include "Engine/TimeScaleFadeMath.h"
#include "Container/VariableArray.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Engine
{
    namespace
    {
        struct Coefficient
        {
            double Mantissa = 0;
            int64_t Exponent = 0;
        };
        bool AddExponent(int64_t value, int delta, int64_t& out)
        {
            if ((delta > 0 && value > std::numeric_limits<int64_t>::max() - delta) ||
                (delta < 0 && value < std::numeric_limits<int64_t>::min() - delta))
                return false;
            out = value + delta;
            return true;
        }
        bool Multiply(Coefficient value, double factor, Coefficient& out)
        {
            if (value.Mantissa == 0 || factor == 0)
            {
                out = {};
                return true;
            }
            int factorExponent = 0, normalizeExponent = 0;
            const double factorMantissa = std::frexp(factor, &factorExponent);
            Coefficient result;
            result.Mantissa = std::frexp(value.Mantissa * factorMantissa, &normalizeExponent);
            if (!AddExponent(value.Exponent, factorExponent + normalizeExponent, result.Exponent))
                return false;
            out = result;
            return true;
        }
        bool Add(Coefficient a, Coefficient b, Coefficient& out)
        {
            if (a.Mantissa == 0)
            {
                out = b;
                return true;
            }
            if (b.Mantissa == 0)
            {
                out = a;
                return true;
            }
            if (a.Exponent < b.Exponent)
                std::swap(a, b);
            const uint64_t difference = static_cast<uint64_t>(a.Exponent) - static_cast<uint64_t>(b.Exponent);
            const double small = difference > 1075 ? 0 : std::ldexp(b.Mantissa, -static_cast<int>(difference));
            int normalization = 0;
            Coefficient result;
            result.Mantissa = std::frexp(a.Mantissa + small, &normalization);
            if (!AddExponent(a.Exponent, normalization, result.Exponent))
                return false;
            out = result;
            return true;
        }
    } // namespace
    TimeScaleMathResult AverageLinearTimeScales(Container::Span<const LinearTimeScale> factors, double& out)
    {
        bool zero = false;
        for (const auto& factor : factors)
        {
            if (!std::isfinite(factor.Begin) || !std::isfinite(factor.End) || factor.Begin < 0 || factor.End < 0)
                return TimeScaleMathResult::InvalidArgument;
            zero = zero || (factor.Begin == 0 && factor.End == 0);
        }
        if (zero)
        {
            out = 0;
            return TimeScaleMathResult::Success;
        }
        Container::VariableArray<Coefficient> coefficients;
        coefficients.push_back({.5, 1});
        for (const auto& factor : factors)
        {
            if (factor.Begin == factor.End)
            {
                for (auto& value : coefficients)
                    if (!Multiply(value, factor.Begin, value))
                        return TimeScaleMathResult::Overflow;
            }
            else
            {
                const size_t degree = coefficients.size();
                coefficients.push_back({});
                for (size_t k = degree;; --k)
                {
                    Coefficient left, right;
                    if (k < degree && (!Multiply(coefficients[k], factor.Begin, left) ||
                                       !Multiply(left, double(degree - k) / double(degree), left)))
                        return TimeScaleMathResult::Overflow;
                    if (k > 0 && (!Multiply(coefficients[k - 1], factor.End, right) ||
                                  !Multiply(right, double(k) / double(degree), right)))
                        return TimeScaleMathResult::Overflow;
                    if (!Add(left, right, coefficients[k]))
                        return TimeScaleMathResult::Overflow;
                    if (k == 0)
                        break;
                }
            }
        }
        // 各係数の指数を独立に保持し、後続因子で再び重要になる小さい係数を失わない。
        int64_t exponent = std::numeric_limits<int64_t>::min();
        for (const auto& value : coefficients)
            if (value.Mantissa)
                exponent = std::max(exponent, value.Exponent);
        double mean = 0, correction = 0, largest = 0;
        for (const auto& value : coefficients)
        {
            if (!value.Mantissa)
                continue;
            const uint64_t difference = static_cast<uint64_t>(exponent) - static_cast<uint64_t>(value.Exponent);
            const double normalized = difference > 1075 ? 0 : std::ldexp(value.Mantissa, -static_cast<int>(difference));
            largest = std::max(largest, normalized);
            const double term = normalized / coefficients.size() - correction;
            const double next = mean + term;
            correction = (next - mean) - term;
            mean = next;
        }
        mean = std::min(mean, largest);
        if (exponent > std::numeric_limits<int>::max())
            return TimeScaleMathResult::Overflow;
        if (exponent < std::numeric_limits<int>::min())
        {
            out = 0;
            return TimeScaleMathResult::Success;
        }
        const double result = std::ldexp(mean, static_cast<int>(exponent));
        if (!std::isfinite(result))
            return TimeScaleMathResult::Overflow;
        out = result;
        return TimeScaleMathResult::Success;
    }
} // namespace NorvesLib::Core::Engine
