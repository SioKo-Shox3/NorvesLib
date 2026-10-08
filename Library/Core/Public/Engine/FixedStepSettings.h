#pragma once
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Core::Engine
{
    inline constexpr uint32_t DefaultFixedUpdateRateHz = 60;
    inline constexpr bool IsSupportedFixedUpdateRate(uint32_t rate)
    {
        return rate == 60 || rate == 120;
    }
    enum class FixedUpdateArgumentResult : uint8_t
    {
        Unrecognized,
        Invalid,
        Valid
    };
    // 比較用起動引数。失敗時rateは保持する。文字型に依存しないASCII照合。
    template <typename Char> FixedUpdateArgumentResult ParseFixedUpdateRateArgument(const Char* text, uint32_t& rate)
    {
        if (!text)
            return FixedUpdateArgumentResult::Unrecognized;
        constexpr char prefix[] = "--fixed-update-hz=";
        size_t i = 0;
        for (; i < sizeof(prefix) - 1; ++i)
            if (text[i] != static_cast<Char>(prefix[i]))
                return prefix[i] == '=' && text[i] == Char{} ? FixedUpdateArgumentResult::Invalid
                                                             : FixedUpdateArgumentResult::Unrecognized;
        const auto* value = text + i;
        if (value[0] == static_cast<Char>('6') && value[1] == static_cast<Char>('0') && value[2] == Char{})
        {
            rate = 60;
            return FixedUpdateArgumentResult::Valid;
        }
        if (value[0] == static_cast<Char>('1') && value[1] == static_cast<Char>('2') &&
            value[2] == static_cast<Char>('0') && value[3] == Char{})
        {
            rate = 120;
            return FixedUpdateArgumentResult::Valid;
        }
        return FixedUpdateArgumentResult::Invalid;
    }
} // namespace NorvesLib::Core::Engine
