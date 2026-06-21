#pragma once

// =============================================================================
// ModuleExport.h — DLL 移行用境界マクロ(静的リンク時は空展開)
//
// NORVES_MODULE_API は将来 DLL 化したときにモジュール境界の安定面が参照する
// エクスポート/インポート指定の予約点である。現状(静的リンク既定)では空展開され
// 無コストになる。マクロを付けても TUniquePtr/Span 等の独自テンプレートを値で
// 渡すシグネチャは ABI 安定ではない点に注意(ModuleRegistry.h の ABI 注記参照)。
// =============================================================================

#if defined(NORVES_BUILD_MODULE_DLL)
#  define NORVES_MODULE_API __declspec(dllexport)
#elif defined(NORVES_USE_MODULE_DLL)
#  define NORVES_MODULE_API __declspec(dllimport)
#else
#  define NORVES_MODULE_API // 静的リンク(現状・既定): 無コスト
#endif
