#pragma once

/**
 * @file Containers.h
 * @brief すべてのカスタムコンテナ実装のインクルードファイル
 * 
 * このファイルをインクルードすると、GlobalMemoryAllocatorを使用するすべての
 * STLコンテナラッパーが使用できるようになります。
 */

#include "Allocator.h"
#include "VariableArray.h"
#include "FixedArray.h"
#include "List.h"
#include "Map.h"
#include "Set.h"
#include "UnorderedMap.h"
#include "UnorderedSet.h"
#include "String.h"
#include "Span.h"
#include "StringView.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief すべてのコンテナで使われているアロケータがGlobalMemoryAllocatorを
     * 正しく使用していることを確認する
     * 
     * @return 常にtrue（コンパイル時チェック用）
     */
    constexpr bool ValidateContainerAllocators()
    {
        static_assert(std::is_same_v<VariableArray<int>::allocator_type, Allocator<int>>, 
            "VariableArray is not using custom allocator");
        
        // FixedArrayは小さいサイズはスタックに確保するため、常にアロケータをチェックできない
        
        static_assert(std::is_same_v<List<int>::allocator_type, Allocator<int>>, 
            "List is not using custom allocator");
        
        static_assert(std::is_same_v<Map<int, int>::allocator_type, Allocator<std::pair<const int, int>>>, 
            "Map is not using custom allocator");
        
        static_assert(std::is_same_v<Set<int>::allocator_type, Allocator<int>>, 
            "Set is not using custom allocator");
        
        static_assert(std::is_same_v<UnorderedMap<int, int>::allocator_type, Allocator<std::pair<const int, int>>>, 
            "UnorderedMap is not using custom allocator");
        
        static_assert(std::is_same_v<UnorderedSet<int>::allocator_type, Allocator<int>>, 
            "UnorderedSet is not using custom allocator");
        
        static_assert(std::is_same_v<String::allocator_type, Allocator<char>>, 
            "String is not using custom allocator");
        
        // SpanとStringViewはメモリを所有せず、アロケータを使用しない
        
        return true;
    }
}