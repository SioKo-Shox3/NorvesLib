#pragma once

#include <cstdint>

namespace NorvesLib::RHI::Detail
{
    inline bool TryAllocateSubmissionSerial(uint64_t currentSerial, uint64_t& outSerial)
    {
        outSerial = 0;
        constexpr uint64_t MaxSubmissionSerial = ~uint64_t{0};
        if (currentSerial == MaxSubmissionSerial)
        {
            return false;
        }

        outSerial = currentSerial + 1;
        return true;
    }
} // namespace NorvesLib::RHI::Detail
