#pragma once

/**
 * @file MemoryOverrides.h
 * @brief NorvesLibのメモリ管理インターフェース
 *
 * このファイルは、NorvesLibのメモリ管理関数を提供します。
 * 標準のmalloc/freeは使用せず、NorvesLib::Memory名前空間の関数を使用してください。
 *
 * 使用例:
 *   void* ptr = NorvesLib::Memory::Malloc(size);
 *   NorvesLib::Memory::Free(ptr);
 *
 *   void* aligned = NorvesLib::Memory::AlignedMalloc(size, alignment);
 *   NorvesLib::Memory::AlignedFree(aligned);
 */

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

// 前方宣言
namespace NorvesLib::Memory
{
    class MemorySystem;
} // namespace NorvesLib::Memory

namespace NorvesLib::Memory
{
    // ===========================================
    // 定数定義
    // ===========================================

    /**
     * @brief デフォルトのアライメント値
     */
    constexpr size_t DefaultAlignment = 16;

    // ===========================================
    // 初期化・終了処理
    // ===========================================

    /**
     * @brief メモリシステムを初期化
     *
     * アプリケーション開始時に呼び出してください。
     * 初期化前でも Malloc/Free は動作しますが、
     * 最適化されていないOS直接呼び出しになります。
     */
    void Initialize();

    /**
     * @brief メモリシステムを終了
     *
     * アプリケーション終了時に呼び出してください。
     */
    void Shutdown();

    /**
     * @brief メモリシステムが初期化されているかを確認
     * @return 初期化されていればtrue
     */
    bool IsInitialized();

    // ===========================================
    // メモリ確保・解放関数
    // ===========================================

    /**
     * @brief メモリを確保（NorvesLib版malloc）
     * @param size 確保するサイズ（バイト）
     * @return 確保されたメモリへのポインタ、失敗時はnullptr
     */
    void *Malloc(size_t size);

    /**
     * @brief アライメント指定でメモリを確保
     * @param size 確保するサイズ（バイト）
     * @param alignment アライメント要件
     * @return 確保されたメモリへのポインタ、失敗時はnullptr
     */
    void *AlignedMalloc(size_t size, size_t alignment);

    /**
     * @brief メモリを解放（NorvesLib版free）
     * @param ptr 解放するポインタ
     */
    void Free(void *ptr);

    /**
     * @brief アライメント指定で確保したメモリを解放
     * @param ptr 解放するポインタ
     */
    void AlignedFree(void *ptr);

    /**
     * @brief ゼロ初期化してメモリを確保（NorvesLib版calloc）
     * @param num 要素数
     * @param size 各要素のサイズ（バイト）
     * @return 確保されたメモリへのポインタ、失敗時はnullptr
     */
    void *Calloc(size_t num, size_t size);

    /**
     * @brief メモリを再確保（NorvesLib版realloc）
     * @param ptr 既存のポインタ（nullptrの場合はMallocと同等）
     * @param newSize 新しいサイズ（バイト）
     * @return 再確保されたメモリへのポインタ、失敗時はnullptr
     */
    void *Realloc(void *ptr, size_t newSize);

    /**
     * @brief アライメント指定でメモリを再確保
     * @param ptr 既存のポインタ
     * @param newSize 新しいサイズ（バイト）
     * @param alignment アライメント要件
     * @return 再確保されたメモリへのポインタ、失敗時はnullptr
     */
    void *AlignedRealloc(void *ptr, size_t newSize, size_t alignment);

    /**
     * @brief 確保されたブロックのサイズを取得
     * @param ptr メモリポインタ
     * @return ブロックサイズ（バイト）
     */
    size_t GetBlockSize(const void *ptr);

    /**
     * @brief 合計確保済みメモリサイズを取得
     * @return 確保済みメモリサイズ（バイト）
     */
    size_t GetTotalAllocatedSize();

    // ===========================================
    // 型付きメモリ確保ヘルパー
    // ===========================================

    /**
     * @brief 型Tのメモリを確保（コンストラクタは呼ばない）
     * @tparam T 型
     * @param count 要素数
     * @return 確保されたメモリへのポインタ
     */
    template <typename T>
    T *TypedMalloc(size_t count = 1)
    {
        return static_cast<T *>(AlignedMalloc(sizeof(T) * count, alignof(T)));
    }

    /**
     * @brief 型Tのメモリを解放（デストラクタは呼ばない）
     * @tparam T 型
     * @param ptr 解放するポインタ
     */
    template <typename T>
    void TypedFree(T *ptr)
    {
        AlignedFree(ptr);
    }

    // ===========================================
    // オブジェクト生成・破棄ヘルパー
    // ===========================================

    /**
     * @brief 型Tのオブジェクトを生成（コンストラクタを呼ぶ）
     * @tparam T 型
     * @tparam Args コンストラクタ引数の型
     * @param args コンストラクタ引数
     * @return 生成されたオブジェクトへのポインタ
     */
    template <typename T, typename... Args>
    T *New(Args &&...args)
    {
        void *ptr = AlignedMalloc(sizeof(T), alignof(T));
        if (!ptr)
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    /**
     * @brief 型Tのオブジェクトを破棄（デストラクタを呼ぶ）
     * @tparam T 型
     * @param ptr 破棄するオブジェクトへのポインタ
     */
    template <typename T>
    void Delete(T *ptr)
    {
        if (ptr)
        {
            ptr->~T();
            AlignedFree(ptr);
        }
    }

    /**
     * @brief 型Tの配列を生成（各要素のデフォルトコンストラクタを呼ぶ）
     * @tparam T 型
     * @param count 要素数
     * @return 生成された配列へのポインタ
     */
    template <typename T>
    T *NewArray(size_t count)
    {
        if (count == 0)
        {
            return nullptr;
        }

        // 要素数を格納するための追加スペースを確保
        size_t headerSize = sizeof(size_t);
        size_t alignment = alignof(T) > alignof(size_t) ? alignof(T) : alignof(size_t);
        size_t totalSize = headerSize + sizeof(T) * count;

        void *rawPtr = AlignedMalloc(totalSize, alignment);
        if (!rawPtr)
        {
            return nullptr;
        }

        // 先頭に要素数を格納
        *static_cast<size_t *>(rawPtr) = count;

        // 実際の配列ポインタ
        T *arrayPtr = reinterpret_cast<T *>(static_cast<char *>(rawPtr) + headerSize);

        // 各要素を構築
        for (size_t i = 0; i < count; ++i)
        {
            new (&arrayPtr[i]) T();
        }

        return arrayPtr;
    }

    /**
     * @brief 型Tの配列を破棄（各要素のデストラクタを呼ぶ）
     * @tparam T 型
     * @param ptr 破棄する配列へのポインタ
     */
    template <typename T>
    void DeleteArray(T *ptr)
    {
        if (!ptr)
        {
            return;
        }

        // ヘッダーから要素数を取得
        size_t headerSize = sizeof(size_t);
        void *rawPtr = static_cast<void *>(reinterpret_cast<char *>(ptr) - headerSize);
        size_t count = *static_cast<size_t *>(rawPtr);

        // 逆順でデストラクタを呼ぶ
        for (size_t i = count; i > 0; --i)
        {
            ptr[i - 1].~T();
        }

        AlignedFree(rawPtr);
    }

} // namespace NorvesLib::Memory

// ===========================================
// グローバル new/delete 演算子オーバーライド
// ===========================================

/**
 * グローバルのnew/delete演算子をオーバーライドして、
 * すべてのメモリ確保をNorvesLibのメモリシステム経由にします。
 *
 * 注意: これらの演算子はCPPファイル(MemoryOverrides.cpp)で定義されています。
 * ヘッダーでは宣言のみ行います。
 */

// operator new/delete の宣言はシステムヘッダーで既に行われているため、
// ここでは追加の宣言は不要です。
// 実装は MemoryOverrides.cpp で行います。
