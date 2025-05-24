#pragma once

#include "Memory/Public/TLSFAllocator.h"

namespace NorvesLib::Core
{
    /**
     * エンジン全体で使用するグローバルメモリアロケーター
     * TLSFアルゴリズムを使用した高性能なメモリ管理を提供
     */
    class GlobalMemoryAllocator
    {
    public:
        /** シングルトンインスタンスの取得 */
        static Memory::TLSFAllocator& Get();

        /** アロケーターの初期化 */
        static void Initialize();

        /** アロケーターの終了処理 */
        static void Shutdown();

    private:
        // 2GB（2,147,483,648バイト）のメモリ容量
        static constexpr size_t GLOBAL_MEMORY_SIZE = 2ULL * 1024ULL * 1024ULL * 1024ULL;

        // シングルトンインスタンス
        static Memory::TLSFAllocator* s_instance;
    };
}