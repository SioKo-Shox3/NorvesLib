#pragma once

/**
 * @file CoreModule.h
 * @brief NorvesLib Core モジュールのメインインクルードファイル
 *
 * このファイルは Core モジュールの全ての主要機能へのアクセスを提供します。
 * Core モジュールを使用する際は、このファイルをインクルードしてください。
 */

// Container 機能
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Container/FixedArray.h"
#include "Container/List.h"
#include "Container/Map.h"
#include "Container/Set.h"
#include "Container/UnorderedMap.h"
#include "Container/UnorderedSet.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/Span.h"
#include "Container/Deque.h"
#include "Container/Queue.h"

// Object システム
#include "Object/Object.h"

// テキスト処理
#include "Text/IdentityPool.h"

// ログ機能
#include "Logging/LoggingModule.h"
#include "Logging/Logger.h"
#include "Logging/LogMacros.h"

// デバッグ機能
#include "Debug/DebugOutput.h"

/**
 * @namespace NorvesLib::Core
 * @brief Core モジュールの名前空間
 *
 * NorvesLib の基盤となる機能を提供します。
 * コンテナ、オブジェクトシステム、ログ機能、デバッグ機能などが含まれます。
 */
namespace NorvesLib::Core
{
    /**
     * @brief Core モジュールの初期化
     *
     * Core モジュールの内部システムを初期化します。
     * アプリケーション開始時に一度だけ呼び出してください。
     *
     * @return 初期化が成功した場合は true、失敗した場合は false
     */
    bool Initialize();

    /**
     * @brief Core モジュールの終了処理
     *
     * Core モジュールのリソースをクリーンアップします。
     * アプリケーション終了時に呼び出してください。
     */
    void Shutdown();

} // namespace NorvesLib::Core
