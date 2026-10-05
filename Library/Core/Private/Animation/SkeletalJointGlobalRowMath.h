#pragma once

#include "SkeletalBindRowMath.h"
#include "Container/Span.h"
#include <cstdint>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core::Animation::Detail
{
    // 有効な借用領域を渡す。local/global/scratchは別領域で、pass中のlocal/parentは不変。
    // scratchは開始時0、2は既に完成したglobalを表す。falseでは内部出力が部分更新され得る。
    // 再帰深さは親chain長。callerは資産のjoint数上限を守り、失敗後の再評価ではscratchを全0へ戻す。
    template <class ParentIndexAt>
    bool BuildJointGlobalRow(uint32_t jointIndex, const ParentIndexAt& parentIndexAt,
                             Container::Span<const Math::Matrix4x4> localRows,
                             Container::Span<Math::Matrix4x4> globalRows, Container::Span<uint8_t> visitState)
    {
        static_assert(std::is_same_v<std::invoke_result_t<const ParentIndexAt&, uint32_t>, int32_t>);
        static_assert(std::is_nothrow_invocable_v<const ParentIndexAt&, uint32_t>);
        const size_t count = localRows.size();
        if (count > std::numeric_limits<uint32_t>::max() || jointIndex >= count || globalRows.size() != count ||
            visitState.size() != count || localRows.data() == nullptr || globalRows.data() == nullptr ||
            visitState.data() == nullptr)
        {
            return false;
        }
        if (visitState[jointIndex] == 2)
        {
            return true;
        }
        if (visitState[jointIndex] != 0)
        {
            return false;
        }
        visitState[jointIndex] = 1;
        const int32_t parentIndex = parentIndexAt(jointIndex);
        if (parentIndex < -1)
        {
            return false;
        }
        if (parentIndex >= 0)
        {
            if (static_cast<size_t>(parentIndex) >= count ||
                !BuildJointGlobalRow(static_cast<uint32_t>(parentIndex), parentIndexAt, localRows, globalRows,
                                     visitState))
            {
                return false;
            }
            globalRows[jointIndex] = localRows[jointIndex] * globalRows[static_cast<size_t>(parentIndex)];
        }
        else
        {
            globalRows[jointIndex] = localRows[jointIndex];
        }
        if (!IsFiniteMatrix(globalRows[jointIndex]))
        {
            return false;
        }
        visitState[jointIndex] = 2;
        return true;
    }
} // namespace NorvesLib::Core::Animation::Detail
