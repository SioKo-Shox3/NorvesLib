#pragma once

#include "IAllocator.h"
#include <cstdint>
#include <vector>

namespace NorvesLib::Memory
{
    /**
     * TLSF（Two-Level Segregated Fit）メモリアロケーター
     * 高速で効率的な動的メモリ管理を提供する汎用アロケーター
     * O(1)の時間複雑度で割り当てと解放を行うことが可能
     */
    class TLSFAllocator : public IAllocator
    {
    public:
        // 定数
        static constexpr size_t DEFAULT_ALIGNMENT = 16;
        static constexpr int FL_INDEX_MAX = 32;        // 最大ファーストレベルインデックス数
        static constexpr int SL_INDEX_COUNT = 32;      // セカンドレベルインデックス数
        static constexpr size_t BLOCK_HEADER_SIZE = 8; // メモリブロックヘッダーのサイズ

        /**
         * コンストラクタ
         * @param memorySize  管理するメモリプールの合計サイズ（バイト単位）
         * @param minBlockSize 確保する最小ブロックサイズ（バイト単位、デフォルトは16バイト）
         */
        explicit TLSFAllocator(size_t memorySize, size_t minBlockSize = DEFAULT_ALIGNMENT);

        /**
         * デストラクタ
         */
        ~TLSFAllocator() override;

        /**
         * メモリの割り当て
         * @param size 割り当てサイズ（バイト単位）
         * @param alignment アライメント要件（バイト単位、デフォルトは16バイト）
         * @return 割り当てられたメモリブロックへのポインタ
         */
        void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;

        /**
         * メモリの解放
         * @param ptr 解放するメモリブロックポインタ
         */
        void Deallocate(void* ptr) override;

        /**
         * 現在割り当てられているメモリの合計サイズを取得
         * @return 割り当てられているメモリの合計サイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

    private:
        // メモリブロックヘッダー構造体（ブロック管理用）
        struct BlockHeader {
            size_t size;         // ブロックのサイズ（ヘッダーを含まない）
            bool used;           // ブロックが使用中かどうか
            BlockHeader* prev;   // 物理的に前のブロック
            BlockHeader* next;   // 物理的に次のブロック
            BlockHeader* prevFree; // フリーリスト内での前のブロック
            BlockHeader* nextFree; // フリーリスト内での次のブロック
        };

        // メンバ変数
        void* m_memoryPool;                // 管理するメモリプール
        size_t m_memorySize;               // メモリプールの合計サイズ
        size_t m_minBlockSize;             // 最小ブロックサイズ
        size_t m_allocatedSize;            // 現在割り当て中のメモリ合計
        
        // TLSFのフリーリスト配列（二重レベル）
        BlockHeader* m_freeBlocks[FL_INDEX_MAX][SL_INDEX_COUNT];
        
        // ビットマップ（空きブロック追跡用）
        uint32_t m_flBitmap;                       // ファーストレベルビットマップ
        uint32_t m_slBitmap[FL_INDEX_MAX];         // セカンドレベルビットマップ

    private:
        // 内部ヘルパー関数
        
        // メモリブロックをフリーリストから削除する
        void RemoveFromFreeList(BlockHeader* block);
        
        // メモリブロックをフリーリストに挿入する
        void InsertToFreeList(BlockHeader* block);
        
        // 適切なフリーブロックを検索する
        BlockHeader* FindFreeBlock(size_t size);
        
        // ブロックを分割する
        BlockHeader* SplitBlock(BlockHeader* block, size_t size);
        
        // 隣接するフリーブロックを結合する
        BlockHeader* CoalesceBlocks(BlockHeader* block);
        
        // サイズからインデックスを計算する (FL, SL)
        void MappingInsert(size_t size, int& flIndex, int& slIndex);
        
        // サイズに基づいてインデックスを検索する
        void MappingSearch(size_t size, int& flIndex, int& slIndex);
        
        // 指定インデックス以上の最初の空きブロックを見つける
        void FindSuitableBlock(int& flIndex, int& slIndex);
        
        // アライメントに合わせてサイズを調整する
        size_t AlignSize(size_t size, size_t alignment);
        
        // ブロックの使用可能サイズを取得する（実際のペイロードサイズ）
        size_t GetBlockPayloadSize(const BlockHeader* block);
        
        // ヘッダーからペイロードポインタを取得
        void* GetBlockPayload(BlockHeader* block);
        
        // ペイロードポインタからヘッダーを取得
        BlockHeader* GetBlockHeader(void* payload);
    };
}