#pragma once

#include "IAllocator.h"
#include "GlobalAllocator.h"
#include "ThreadLocalCache.h"
#include "StackAllocator.h"
#include "FrameAllocator.h"
#include "PoolAllocator.h"
#include "SizeClassAllocator.h"
#include "Thread/Public/ThreadLocalStorage.h"
#include <cstdint>
#include <vector>

namespace NorvesLib::Memory
{
    /**
     * @brief メモリシステム
     *
     * NorvesLibのメモリ管理を統括するシステムクラス。
     * グローバルアロケータとスレッドローカルキャッシュを管理し、
     * 高性能なマルチスレッドメモリ確保を提供します。
     *
     * 使用方法:
     * 1. MemorySystem::Initialize() でシステムを初期化
     * 2. MemorySystem::Allocate() / MemorySystem::Deallocate() でメモリ確保・解放
     * 3. MemorySystem::Shutdown() でシステムを終了
     *
     * 各スレッドでは自動的にスレッドローカルキャッシュが作成され、
     * ロックフリーでの高速なメモリ確保が可能です。
     */
    class MemorySystem
    {
    public:
        /**
         * @brief メモリシステムを初期化
         *
         * アプリケーション開始時に一度だけ呼び出してください。
         */
        static void Initialize();

        /**
         * @brief メモリシステムを終了
         *
         * アプリケーション終了時に一度だけ呼び出してください。
         */
        static void Shutdown();

        /**
         * @brief メモリシステムが初期化されているか確認
         * @return 初期化されている場合true
         */
        static bool IsInitialized();

        // ===========================================
        // メモリ確保・解放
        // ===========================================

        /**
         * @brief メモリを確保（スレッドローカルキャッシュ使用）
         * @param size 要求サイズ（バイト単位）
         * @param alignment アライメント要件
         * @return 確保されたメモリへのポインタ、失敗時はnullptr
         */
        static void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment);

        /**
         * @brief メモリを解放
         * @param ptr 解放するポインタ
         */
        static void Deallocate(void *ptr);

        /**
         * @brief メモリを再確保
         * @param ptr 既存のポインタ
         * @param newSize 新しいサイズ
         * @param alignment アライメント要件
         * @return 再確保されたメモリへのポインタ
         */
        static void *Reallocate(void *ptr, size_t newSize, size_t alignment = Config::DefaultAlignment);

        /**
         * @brief 確保されたメモリブロックのサイズを取得
         * @param ptr メモリポインタ
         * @return ブロックサイズ（バイト単位）
         */
        static size_t GetBlockSize(const void *ptr);

        // ===========================================
        // 特殊アロケータアクセス
        // ===========================================

        /**
         * @brief グローバルアロケータを取得
         * @return グローバルアロケータへのポインタ
         */
        static GlobalAllocator *GetGlobalAllocator();

        /**
         * @brief 現在のスレッドのスレッドローカルキャッシュを取得
         * @return スレッドローカルキャッシュへのポインタ
         */
        static ThreadLocalCache *GetThreadLocalCache();

        // ===========================================
        // フレームアロケータ
        // ===========================================

        /**
         * @brief フレームアロケータからメモリを確保
         * @param size 要求サイズ
         * @param alignment アライメント要件
         * @return 確保されたメモリへのポインタ
         */
        static void *AllocateFrame(size_t size, size_t alignment = Config::DefaultAlignment);

        /**
         * @brief フレームを進める（フレームアロケータをスワップ）
         */
        static void AdvanceFrame();

        /**
         * @brief フレームアロケータを取得
         * @return フレームアロケータへのポインタ
         */
        static FrameAllocator *GetFrameAllocator();

        // ===========================================
        // 統計情報
        // ===========================================

        /**
         * @brief 合計確保済みメモリサイズを取得
         * @return 確保済みメモリサイズ（バイト単位）
         */
        static size_t GetTotalAllocatedSize();

        /**
         * @brief 合計確保回数を取得
         * @return 確保回数
         */
        static uint64_t GetTotalAllocationCount();

        /**
         * @brief 合計解放回数を取得
         * @return 解放回数
         */
        static uint64_t GetTotalDeallocationCount();

        // ===========================================
        // ユーティリティ
        // ===========================================

        /**
         * @brief 現在のスレッドのキャッシュをフラッシュ
         *
         * スレッド終了時に呼び出すことを推奨します。
         */
        static void FlushThreadCache();

        /**
         * @brief すべてのスレッドキャッシュをフラッシュ
         *
         * @note これはスレッドセーフではありません。
         *       すべてのワーカースレッドが停止している状態で呼び出してください。
         */
        static void FlushAllThreadCaches();

    private:
        // コンストラクタ・デストラクタは非公開（シングルトン的な使用）
        MemorySystem() = delete;
        ~MemorySystem() = delete;
        MemorySystem(const MemorySystem &) = delete;
        MemorySystem &operator=(const MemorySystem &) = delete;

        /**
         * @brief 現在のスレッド用のキャッシュを作成または取得
         * @return スレッドローカルキャッシュへのポインタ
         */
        static ThreadLocalCache *GetOrCreateThreadCache();

        // 静的メンバ（実装ファイルで定義）
        static bool s_bInitialized;
        static Core::Container::TUniquePtr<GlobalAllocator> s_globalAllocator;
        static Core::Container::TUniquePtr<FrameAllocator> s_frameAllocator;
        static Thread::ThreadLocalStorage<ThreadLocalCache *> s_threadCache;

        // スレッドキャッシュのリスト（統計・クリーンアップ用）
        static Thread::Mutex s_cacheMutex;
        static std::vector<ThreadLocalCache *> s_allThreadCaches;
    };

// ===========================================
// ヘルパーマクロ
// ===========================================

/**
 * @brief メモリシステムからメモリを確保
 */
#define NORVES_ALLOC(size) NorvesLib::Memory::MemorySystem::Allocate(size)

/**
 * @brief メモリシステムからアライメント付きでメモリを確保
 */
#define NORVES_ALLOC_ALIGNED(size, alignment) NorvesLib::Memory::MemorySystem::Allocate(size, alignment)

/**
 * @brief メモリシステムでメモリを解放
 */
#define NORVES_FREE(ptr) NorvesLib::Memory::MemorySystem::Deallocate(ptr)

/**
 * @brief フレームアロケータからメモリを確保
 */
#define NORVES_FRAME_ALLOC(size) NorvesLib::Memory::MemorySystem::AllocateFrame(size)

    // ===========================================
    // 型付きヘルパー関数
    // ===========================================

    /**
     * @brief 型付きメモリ確保
     * @tparam T 確保する型
     * @return 確保されたメモリへのポインタ
     */
    template <typename T>
    T *AllocateTyped()
    {
        return static_cast<T *>(MemorySystem::Allocate(sizeof(T), alignof(T)));
    }

    /**
     * @brief 型付き配列メモリ確保
     * @tparam T 確保する型
     * @param count 要素数
     * @return 確保されたメモリへのポインタ
     */
    template <typename T>
    T *AllocateArray(size_t count)
    {
        return static_cast<T *>(MemorySystem::Allocate(sizeof(T) * count, alignof(T)));
    }

} // namespace NorvesLib::Memory
