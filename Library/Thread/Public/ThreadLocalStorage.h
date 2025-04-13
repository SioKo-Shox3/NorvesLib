#pragma once

#include <memory>
#include <functional>
#include "Core/Public/Container/Containers.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace NorvesLib::Thread
{

/**
 * @brief スレッドローカルストレージクラス
 * 
 * 各スレッドに固有のデータを保存するためのクラス。
 * スレッド固有の変数を作成して管理することができます。
 * 
 * @tparam T 格納する値の型
 */
template<typename T>
class ThreadLocalStorage
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    ThreadLocalStorage()
    {
        // スレッド固有データの初期化
#ifdef _WIN32
        m_tlsIndex = TlsAlloc();
        if (m_tlsIndex == TLS_OUT_OF_INDEXES)
        {
            throw std::runtime_error("Failed to create thread local storage");
        }
#else
        if (pthread_key_create(&m_key, &ThreadLocalStorage::DestroyValue) != 0)
        {
            throw std::runtime_error("Failed to create thread local storage");
        }
#endif
    }
    
    /**
     * @brief 初期化関数を指定するコンストラクタ
     * 
     * @param initializer スレッド固有データの初期化関数
     */
    explicit ThreadLocalStorage(std::function<T()> initializer)
        : m_initializer(std::move(initializer))
    {
        // スレッド固有データの初期化
#ifdef _WIN32
        m_tlsIndex = TlsAlloc();
        if (m_tlsIndex == TLS_OUT_OF_INDEXES)
        {
            throw std::runtime_error("Failed to create thread local storage");
        }
#else
        if (pthread_key_create(&m_key, &ThreadLocalStorage::DestroyValue) != 0)
        {
            throw std::runtime_error("Failed to create thread local storage");
        }
#endif
    }
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    ThreadLocalStorage(const ThreadLocalStorage&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    ThreadLocalStorage& operator=(const ThreadLocalStorage&) = delete;
    
    /**
     * @brief デストラクタ
     */
    ~ThreadLocalStorage()
    {
#ifdef _WIN32
        // Windows実装の場合、現在のプロセスのすべてのスレッドのデータを解放
        // 注: これは理想的ではないが、Windowsではスレッド終了時のコールバックを
        // 簡単に登録できないため、このような実装になっています
        TlsFree(m_tlsIndex);
#else
        pthread_key_delete(m_key);
#endif
    }
    
    /**
     * @brief スレッド固有のデータを取得
     * 
     * @return スレッド固有のデータへの参照
     * @note 初めてアクセスした時に値が初期化されます
     */
    T& Get()
    {
#ifdef _WIN32
        void* value = TlsGetValue(m_tlsIndex);
        if (value == nullptr || GetLastError() != ERROR_SUCCESS)
        {
            // スレッド用の新しい値を作成
            T* newValue = new T();
            if (m_initializer)
            {
                *newValue = m_initializer();
            }
            if (!TlsSetValue(m_tlsIndex, newValue))
            {
                delete newValue;
                throw std::runtime_error("Failed to set thread local value");
            }
            value = newValue;
        }
#else
        void* value = pthread_getspecific(m_key);
        if (value == nullptr)
        {
            // スレッド用の新しい値を作成
            T* newValue = new T();
            if (m_initializer)
            {
                *newValue = m_initializer();
            }
            pthread_setspecific(m_key, newValue);
            value = newValue;
        }
#endif
        return *static_cast<T*>(value);
    }
    
    /**
     * @brief スレッド固有のデータを設定
     * 
     * @param value 設定する値
     */
    void Set(const T& value)
    {
#ifdef _WIN32
        void* oldValue = TlsGetValue(m_tlsIndex);
        if (oldValue != nullptr)
        {
            delete static_cast<T*>(oldValue);
        }
        
        T* newValue = new T(value);
        if (!TlsSetValue(m_tlsIndex, newValue))
        {
            delete newValue;
            throw std::runtime_error("Failed to set thread local value");
        }
#else
        void* oldValue = pthread_getspecific(m_key);
        if (oldValue != nullptr)
        {
            delete static_cast<T*>(oldValue);
        }
        
        T* newValue = new T(value);
        pthread_setspecific(m_key, newValue);
#endif
    }
    
private:
#ifdef _WIN32
    // Windowsのスレッドローカルストレージインデックス
    DWORD m_tlsIndex;
#else
    // POSIXのスレッド固有データキー
    pthread_key_t m_key;
#endif
    
    // 初期化関数
    std::function<T()> m_initializer;
    
#ifndef _WIN32
    // 値破棄用の静的関数（POSIXのみ）
    static void DestroyValue(void* value)
    {
        if (value != nullptr)
        {
            delete static_cast<T*>(value);
        }
    }
#endif
};

} // namespace NorvesLib::Thread