#pragma once

#include <mutex>
#include "Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief ミューテックスクラス
 * 
 * 排他的なリソースアクセスを提供するミューテックス実装
 */
class Mutex
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    Mutex() = default;
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    Mutex(const Mutex&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    Mutex& operator=(const Mutex&) = delete;
    
    /**
     * @brief デストラクタ
     */
    ~Mutex() = default;
    
    /**
     * @brief ミューテックスをロックする
     * 
     * 既にロックされている場合は解放されるまで待機します
     */
    void Lock();
    
    /**
     * @brief ミューテックスのロックを試みる
     * 
     * @return ロックに成功した場合はtrue、既にロックされていて失敗した場合はfalse
     */
    bool TryLock();
    
    /**
     * @brief ミューテックスのロックを解除する
     */
    void Unlock();
    
    /**
     * @brief 内部のネイティブミューテックスハンドルを取得
     * 
     * @return ネイティブミューテックスへの参照
     */
    std::mutex& GetNativeMutex();

private:
    std::mutex m_mutex;
};

/**
 * @brief スコープ内でのミューテックスロック管理クラス
 * 
 * コンストラクタでミューテックスをロックし、デストラクタでアンロックします
 */
class ScopedLock
{
public:
    /**
     * @brief ミューテックスをロックするコンストラクタ
     * 
     * @param mutex ロックするミューテックス
     */
    explicit ScopedLock(Mutex& mutex);
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    ScopedLock(const ScopedLock&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    ScopedLock& operator=(const ScopedLock&) = delete;
    
    /**
     * @brief ロックを解放するデストラクタ
     */
    ~ScopedLock();
    
private:
    Mutex& m_mutex;
};

} // namespace NorvesLib::Thread
