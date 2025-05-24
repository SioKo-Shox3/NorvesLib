#pragma once

#include "IAllocator.h"
#include <vector>

namespace NorvesLib::Memory
{
    /**
     * メモリプール実装クラス
     * 同じサイズのブロックを効率的に割り当てるためのメモリプール
     */
    class MemoryPool : public IAllocator
    {
    public:
        /**
         * コンストラクタ
         * @param blockSize プール内の各ブロックのサイズ（バイト単位）
         * @param blocksPerChunk 一度に確保するブロック数
         * @param alignment メモリアライメント要件（バイト単位、デフォルトは16バイト）
         */
        MemoryPool(size_t blockSize, size_t blocksPerChunk = 64, size_t alignment = 16);
        
        /**
         * デストラクタ
         */
        ~MemoryPool() override;

        /**
         * メモリブロックを割り当てる
         * @param size 要求サイズ (blockSizeより大きい場合はnullptrを返す)
         * @param alignment アライメント要件（無視されます、コンストラクタで指定された値が使用されます）
         * @return 割り当てられたブロックへのポインタ、またはnullptr
         */
        void* Allocate(size_t size, size_t alignment = 16) override;

        /**
         * メモリブロックを解放する
         * @param ptr 解放するブロックへのポインタ
         */
        void Deallocate(void* ptr) override;

        /**
         * 現在割り当てられているメモリの合計サイズを取得
         * @return 割り当てられたメモリの合計サイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

        /**
         * プールのブロックサイズを取得
         * @return ブロックサイズ（バイト単位）
         */
        size_t GetBlockSize() const { return m_blockSize; }

        /**
         * 未使用ブロック数を取得
         * @return 未使用のブロック数
         */
        size_t GetFreeBlockCount() const { return m_freeBlocks.size(); }

        /**
         * 合計ブロック数を取得
         * @return プール内の合計ブロック数
         */
        size_t GetTotalBlockCount() const { return m_totalBlocks; }

    private:
        // チャンク構造体（メモリの一塊）
        struct Chunk {
            void* memory;     // チャンクのメモリポインタ
            size_t blockCount;// チャンク内のブロック数
        };

        size_t m_blockSize;       // 各ブロックのサイズ
        size_t m_alignment;       // アライメント要件
        size_t m_blocksPerChunk;  // 一度に確保するブロック数
        size_t m_totalBlocks;     // 合計ブロック数
        size_t m_allocatedBlocks; // 割り当て済みブロック数

        std::vector<void*> m_freeBlocks;  // 利用可能なブロックのリスト
        std::vector<Chunk> m_chunks;      // 確保したチャンクのリスト

        // 新しいチャンクを確保して、フリーリストに追加する
        void AllocateChunk();
    };
}