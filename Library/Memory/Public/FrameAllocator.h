#pragma once

#include "IAllocator.h"
#include <cstdint>

namespace NorvesLib::Memory
{
    /**
     * @brief フレームアロケータ
     *
     * フレーム単位（またはフェーズ単位）でメモリを確保し、一括解放するアロケータ。
     * ダブルバッファリングをサポートし、現在のフレームと前のフレームのメモリを
     * 同時に保持できます。
     *
     * 主な用途:
     * - フレームごとの一時データ（レンダリングコマンド、パーティクル等）
     * - フレームをまたぐ一時計算結果
     * - 短命なオブジェクトの高速確保
     *
     * @note このアロケータは非スレッドセーフです。
     *       マルチスレッドで使用する場合は各スレッドに専用のインスタンスを持たせてください。
     */
    class FrameAllocator : public INonThreadSafeAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         * @param sizePerBuffer 各バッファのサイズ（バイト単位）
         * @param bDoubleBuffered ダブルバッファリングを有効にするか
         */
        explicit FrameAllocator(
            size_t sizePerBuffer = Config::DefaultFrameAllocatorSize,
            bool bDoubleBuffered = true);

        /**
         * @brief デストラクタ
         */
        ~FrameAllocator() override;

        // コピー禁止
        FrameAllocator(const FrameAllocator &) = delete;
        FrameAllocator &operator=(const FrameAllocator &) = delete;

        // ムーブ可能
        FrameAllocator(FrameAllocator &&other) noexcept;
        FrameAllocator &operator=(FrameAllocator &&other) noexcept;

        /**
         * @brief メモリの割り当て
         * @param size 割り当てるサイズ（バイト単位）
         * @param alignment アライメント要件
         * @return 割り当てられたメモリへのポインタ、失敗時はnullptr
         */
        void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment) override;

        /**
         * @brief メモリの解放
         *
         * @note フレームアロケータでは個別の解放はサポートされません。
         *       SwapBuffers()またはReset()を使用してください。
         * @param ptr 解放するポインタ（このアロケータでは無視されます）
         */
        void Deallocate(void *ptr) override;

        /**
         * @brief フレームを切り替え、前のフレームのバッファをリセット
         *
         * ダブルバッファリング有効時：
         * - 現在のバッファと前のバッファを交換
         * - 新しい現在のバッファ（前のフレームで使っていたバッファ）をリセット
         *
         * シングルバッファリング時：
         * - 現在のバッファをリセット
         */
        void SwapBuffers();

        /**
         * @brief 両方のバッファをリセット
         */
        void Reset() override;

        /**
         * @brief 現在のバッファのみをリセット
         */
        void ResetCurrentBuffer();

        /**
         * @brief 割り当てられたメモリの合計サイズを取得
         * @return 現在のバッファで割り当てられたメモリサイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

        /**
         * @brief 合計管理メモリサイズを取得
         * @return 全バッファの合計サイズ（バイト単位）
         */
        size_t GetTotalSize() const override;

        /**
         * @brief 各バッファのサイズを取得
         * @return バッファサイズ（バイト単位）
         */
        size_t GetBufferSize() const;

        /**
         * @brief 現在のバッファインデックスを取得
         * @return バッファインデックス（0 または 1）
         */
        uint32_t GetCurrentBufferIndex() const;

        /**
         * @brief ダブルバッファリングが有効かどうか
         * @return ダブルバッファリング有効時true
         */
        bool IsDoubleBuffered() const;

        /**
         * @brief アロケータの種類を取得
         * @return AllocatorType::Frame
         */
        AllocatorType GetType() const override;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        bool OwnsMemory(const void *ptr) const override;

        /**
         * @brief 現在のフレーム番号を取得
         * @return フレーム番号
         */
        uint64_t GetFrameNumber() const;

    private:
        static constexpr uint32_t MaxBuffers = 2;

        struct Buffer
        {
            void *memory;
            size_t offset;
        };

        Buffer m_buffers[MaxBuffers]; ///< バッファ配列
        size_t m_bufferSize;          ///< 各バッファのサイズ
        uint32_t m_currentBuffer;     ///< 現在のバッファインデックス
        uint32_t m_numBuffers;        ///< バッファ数（1 or 2）
        uint64_t m_frameNumber;       ///< フレーム番号
    };

} // namespace NorvesLib::Memory
