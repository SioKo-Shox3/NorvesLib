// 束ねたテストの main を、元の引数の形（引数なし／argc・argv）に合わせて呼ぶ。
// TestBundle/CMakeLists.txt の norves_add_test_bundle が生成するソースから使う。
#pragma once

#include <type_traits>

namespace NorvesLib::Test
{
    template <typename TFunction>
    int InvokeBundledMain(TFunction* function, int argumentCount, char** arguments)
    {
        if constexpr (std::is_invocable_r_v<int, TFunction*, int, char**>)
        {
            return function(argumentCount, arguments);
        }
        else
        {
            (void)argumentCount;
            (void)arguments;
            return function();
        }
    }
} // namespace NorvesLib::Test
