#pragma once

/**
 * @file CoreTypes.h
 * @brief NorvesLib の頻繁に使用される型を名前空間なしで使えるようにする
 *
 * このファイルをインクルードすると、Container:: や Core::Container:: のプレフィックスなしで
 * 以下の型・関数が使用できるようになります：
 *
 * コンテナ型:
 *   VariableArray<T>, Array<T>, Vector<T>, FixedArray<T,N>,
 *   List<T>, Map<K,V>, Set<T>, UnorderedMap<K,V>, UnorderedSet<T>,
 *   String, StringView, StringBuilder, Span<T>, Deque<T>, Queue<T>
 *
 * スマートポインタ:
 *   TUniquePtr<T>, TSharedPtr<T>, TWeakPtr<T>
 *   MakeUnique<T>(), MakeShared<T>()
 *
 * ユーティリティ:
 *   IsValid(), IsNull(), ToShared(),
 *   DynamicPointerCast<T>(), StaticPointerCast<T>(), ConstPointerCast<T>()
 *
 * コンセプト:
 *   SmartPointer, UniquePointer, SharedPointer
 *
 * テキスト:
 *   Identity
 *
 * @note このヘッダーは NorvesLib 名前空間内に using 宣言を配置します。
 *       NorvesLib::* 以下の任意のネスト名前空間から修飾なしでアクセスできます。
 */

#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib
{
    // =========================================================================
    // コンテナ型
    // =========================================================================

    using Core::Container::Array;
    using Core::Container::Deque;
    using Core::Container::FixedArray;
    using Core::Container::List;
    using Core::Container::Map;
    using Core::Container::Queue;
    using Core::Container::Set;
    using Core::Container::Span;
    using Core::Container::String;
    using Core::Container::StringBuilder;
    using Core::Container::StringView;
    using Core::Container::UnorderedMap;
    using Core::Container::UnorderedSet;
    using Core::Container::VariableArray;
    using Core::Container::Vector;

    // =========================================================================
    // スマートポインタ型
    // =========================================================================

    using Core::Container::TSharedPtr;
    using Core::Container::TUniquePtr;
    using Core::Container::TWeakPtr;

    // =========================================================================
    // ファクトリー関数
    // =========================================================================

    using Core::Container::MakeShared;
    using Core::Container::MakeUnique;

    // =========================================================================
    // ユーティリティ関数
    // =========================================================================

    using Core::Container::ConstPointerCast;
    using Core::Container::DynamicPointerCast;
    using Core::Container::IsNull;
    using Core::Container::IsValid;
    using Core::Container::StaticPointerCast;
    using Core::Container::ToShared;

    // =========================================================================
    // コンセプト
    // =========================================================================

    using Core::Container::SharedPointer;
    using Core::Container::SmartPointer;
    using Core::Container::UniquePointer;

    // =========================================================================
    // テキスト
    // =========================================================================

    using Core::Identity;

} // namespace NorvesLib
