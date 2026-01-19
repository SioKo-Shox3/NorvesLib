#pragma once

#include <shared_mutex>
#include "Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief リードライトロッククラス
 * 
 * 複数の読み取りスレッドを許可しながら、書き込みには排他的ロックを提供します
 */
class ReadWriteLock
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    ReadWriteLock() = default;
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    ReadWriteLock(const ReadWriteLock&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    ReadWriteLock& operator=(const ReadWriteLock&) = delete;
    
    /**
     * @brief デストラクタ
     */
    ~ReadWriteLock() = default;
    
    /**
     * @brief 読み取り用ロックを取得する
     * 書き込みロックが取られていない場合にロックを取得します
     * 複数のスレッドが同時に読み取りロックを取得できます
     */
    void LockShared();
    
    /**
     * @brief 読み取りロック取得を試みる
     * @return 読み取りロックの取得に成功した場合はtrue、それ以外はfalse
     */
    bool TryLockShared();
    
    /**
     * @brief 読み取りロックを解放する
     */
    void UnlockShared();
    
    /**
     * @brief 書き込み用の排他ロックを取得する
     * 他の読み取りまたは書き込みロックが取られていない場合にのみ取得できます
     */
    void LockExclusive();
    
    /**
     * @brief 書き込みロック取得を試みる
     * @return 書き込みロックの取得に成功した場合はtrue、それ以外はfalse
     */
    bool TryLockExclusive();
    
    /**
     * @brief 書き込みロックを解放する
     */
    void UnlockExclusive();
    
    /**
     * @brief 内部のネイティブ共有ミューテックスハンドルを取得
     * @return ネイティブ共有ミューテックスへの参照
     */
    std::shared_mutex& GetNativeMutex();

private:
    std::shared_mutex m_sharedMutex;
};

/**
 * @brief スコープ内での読み取りロック管理クラス
 * 
 * コンストラクタで読み取りロックを取得し、デストラクタで解放します
 */
class SharedLock
{
public:
    /**
     * @brief 読み取りロックを取得するコンストラクタ
     * @param rwLock ロックするリードライトロック
     */
    explicit SharedLock(ReadWriteLock& rwLock);
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    SharedLock(const SharedLock&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    SharedLock& operator=(const SharedLock&) = delete;
    
    /**
     * @brief 読み取りロックを解放するデストラクタ
     */
    ~SharedLock();
    
private:
    ReadWriteLock& m_rwLock;
};

/**
 * @brief スコープ内での書き込みロック管理クラス
 * 
 * コンストラクタで書き込みロックを取得し、デストラクタで解放します
 */
class ExclusiveLock
{
public:
    /**
     * @brief 書き込みロックを取得するコンストラクタ
     * @param rwLock ロックするリードライトロック
     */
    explicit ExclusiveLock(ReadWriteLock& rwLock);
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    ExclusiveLock(const ExclusiveLock&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
    
    /**
     * @brief 書き込みロックを解放するデストラクタ
     */
    ~ExclusiveLock();
    
private:
    ReadWriteLock& m_rwLock;
};

} // namespace NorvesLib::Thread
