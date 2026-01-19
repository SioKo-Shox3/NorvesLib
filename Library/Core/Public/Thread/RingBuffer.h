#pragma once

#include <atomic>
#include <memory>
#include <type_traits>
#include "Container/Containers.h"
#include "Atomic.h"

namespace NorvesLib::Thread
{

    /**
     * @brief スレッドセーフなリングバッファ
     *
     * 単一プロデューサ・単一コンシューマ（SPSC）向けにロックフリーに実装された
     * スレッドセーフなリングバッファです。
     *
     * @tparam T 格納する要素の型
     * @tparam Capacity バッファのサイズ（2の累乗である必要があります）
     */
    template <typename T, size_t Capacity>
    class RingBuffer
    { // サイズが2のべき乗であることを静的アサート
        static_assert((Capacity & (Capacity - 1)) == 0, "RingBuffer capacity must be a power of 2");

        // POD型か、トリビアルコピー可能な型のみサポート（ログエントリなど特殊用途では例外）
        // static_assert(std::is_trivially_copyable_v<T>, "RingBuffer only supports trivially copyable types");

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        RingBuffer()
            : m_writeIndex(0), m_readIndex(0)
        {
        }

        /**
         * @brief コピーコンストラクタ（削除）
         */
        RingBuffer(const RingBuffer &) = delete;

        /**
         * @brief コピー代入演算子（削除）
         */
        RingBuffer &operator=(const RingBuffer &) = delete;

        /**
         * @brief デストラクタ
         */
        ~RingBuffer() = default;

        /**
         * @brief バッファに要素を書き込む
         *
         * @param item 書き込む要素
         * @return 書き込みに成功した場合はtrue、バッファが満杯の場合はfalse
         */
        bool TryWrite(const T &item)
        {
            const size_t currentWrite = m_writeIndex.Load(std::memory_order_relaxed);
            const size_t nextWrite = (currentWrite + 1) & (Capacity - 1);

            // バッファが満杯の場合は失敗
            if (nextWrite == m_readIndex.Load(std::memory_order_acquire))
            {
                return false;
            }

            m_buffer[currentWrite] = item;
            m_writeIndex.Store(nextWrite, std::memory_order_release);
            return true;
        }

        /**
         * @brief バッファから要素を読み取る
         *
         * @param item 読み取った要素を格納する変数への参照
         * @return 読み取りに成功した場合はtrue、バッファが空の場合はfalse
         */
        bool TryRead(T &item)
        {
            const size_t currentRead = m_readIndex.Load(std::memory_order_relaxed);

            // バッファが空の場合は失敗
            if (currentRead == m_writeIndex.Load(std::memory_order_acquire))
            {
                return false;
            }

            item = m_buffer[currentRead];
            m_readIndex.Store((currentRead + 1) & (Capacity - 1), std::memory_order_release);
            return true;
        }

        /**
         * @brief バッファが空かどうかを判断する
         *
         * @return バッファが空の場合はtrue、そうでない場合はfalse
         */
        bool IsEmpty() const
        {
            return m_readIndex.Load(std::memory_order_acquire) ==
                   m_writeIndex.Load(std::memory_order_acquire);
        }

        /**
         * @brief バッファが満杯かどうかを判断する
         *
         * @return バッファが満杯の場合はtrue、そうでない場合はfalse
         */
        bool IsFull() const
        {
            const size_t nextWrite = (m_writeIndex.Load(std::memory_order_acquire) + 1) & (Capacity - 1);
            return nextWrite == m_readIndex.Load(std::memory_order_acquire);
        }

        /**
         * @brief バッファ内の要素数を取得する
         *
         * @return バッファ内の要素数
         * @note この関数は近似値を返す場合があります（別スレッドが同時に読み書きしている場合）
         */
        size_t GetSize() const
        {
            const size_t writeIndex = m_writeIndex.Load(std::memory_order_acquire);
            const size_t readIndex = m_readIndex.Load(std::memory_order_acquire);

            if (writeIndex >= readIndex)
            {
                return writeIndex - readIndex;
            }
            else
            {
                return Capacity - (readIndex - writeIndex);
            }
        }

        /**
         * @brief リングバッファの容量（最大要素数）を返す
         *
         * @return リングバッファの容量
         */
        constexpr size_t GetCapacity() const
        {
            return Capacity - 1; // 実際に格納できる要素は(Capacity-1)個
        }

        /**
         * @brief バッファをクリアする
         *
         * @note このメソッドは他のスレッドがアクセスしていない時にのみ呼び出すべきです
         */
        void Clear()
        {
            m_writeIndex.Store(0, std::memory_order_relaxed);
            m_readIndex.Store(0, std::memory_order_relaxed);
        }

    private:
        // データバッファ
        T m_buffer[Capacity];

        // キャッシュラインを分離するためのパディング
        char m_padding1[64 - (Capacity * sizeof(T)) % 64];

        // 書き込みインデックス
        Atomic<size_t> m_writeIndex;

        // キャッシュラインを分離するためのパディング
        char m_padding2[64 - sizeof(Atomic<size_t>)];

        // 読み取りインデックス
        Atomic<size_t> m_readIndex;

        // キャッシュラインを分離するためのパディング
        char m_padding3[64 - sizeof(Atomic<size_t>)];
    };

} // namespace NorvesLib::Thread
