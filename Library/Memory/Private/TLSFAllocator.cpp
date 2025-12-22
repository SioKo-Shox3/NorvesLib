#include "../Public/TLSFAllocator.h"
#include <cassert>
#include <cstring>
#include <algorithm>
#include <intrin.h> // Visual Studioの組み込み関数用

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // min/maxマクロの無効化
#endif
#include <malloc.h>  // _aligned_malloc, _aligned_free用
#include <Windows.h> // OutputDebugStringA用
#endif

namespace NorvesLib::Memory
{
    // アライメントに合わせてサイズを切り上げる
    inline size_t RoundUpSize(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    } // 最上位ビットの位置を取得（MSB）
    inline int FindMSB(size_t value)
    {
        // 組み込み関数を使用してMSB（Most Significant Bit）を検出
        if (value == 0)
            return -1;
#if defined(_MSC_VER)
        unsigned long index;
#ifdef _WIN64
        _BitScanReverse64(&index, value);
#else
        _BitScanReverse(&index, static_cast<uint32_t>(value));
#endif
        return static_cast<int>(index);
#else
        return static_cast<int>(sizeof(size_t) * 8 - 1 - __builtin_clzll(value));
#endif
    }

    // 最下位ビットの位置を取得（LSB）
    inline int FindLSB(uint32_t value)
    {
        // 組み込み関数を使用してLSB（Least Significant Bit）を検出
        if (value == 0)
            return -1;
#if defined(_MSC_VER)
        unsigned long index;
        _BitScanForward(&index, value);
        return index;
#else
        return __builtin_ctz(value);
#endif
    } // コンストラクタ
    TLSFAllocator::TLSFAllocator(size_t memorySize, size_t minBlockSize)
        : m_memoryPool(nullptr), m_memorySize(memorySize), m_minBlockSize((std::max)(minBlockSize, DEFAULT_ALIGNMENT)), m_allocatedSize(0), m_flBitmap(0)
    {
        // メモリサイズを最小ブロックサイズにアラインする
        m_memorySize = RoundUpSize(m_memorySize, m_minBlockSize);

        // ビットマップを初期化
        std::memset(m_slBitmap, 0, sizeof(m_slBitmap));

        // フリーリスト配列を初期化
        std::memset(m_freeBlocks, 0, sizeof(m_freeBlocks));

        // メモリプールを確保（アラインメント考慮）
#ifdef _WIN32
        m_memoryPool = _aligned_malloc(m_memorySize, DEFAULT_ALIGNMENT);
#else
        m_memoryPool = std::aligned_alloc(DEFAULT_ALIGNMENT, m_memorySize);
#endif
        if (!m_memoryPool)
        {
            throw std::bad_alloc();
        }

        // 初期ブロックヘッダー設定
        BlockHeader *firstBlock = reinterpret_cast<BlockHeader *>(m_memoryPool);
        firstBlock->size = m_memorySize - sizeof(BlockHeader);
        firstBlock->used = false;
        firstBlock->prev = nullptr;
        firstBlock->next = nullptr;
        firstBlock->prevFree = nullptr;
        firstBlock->nextFree = nullptr;

        // 初期ブロックをフリーリストに追加
        InsertToFreeList(firstBlock);
    }

    // デストラクタ
    TLSFAllocator::~TLSFAllocator()
    {
        if (m_memoryPool)
        {
#ifdef _WIN32
            _aligned_free(m_memoryPool);
#else
            std::free(m_memoryPool);
#endif
            m_memoryPool = nullptr;
        }
    } // アライメントに合わせてサイズを調整
    size_t TLSFAllocator::AlignSize(size_t size, size_t alignment)
    {
        // 最小ブロックサイズ以上かつアライメントに合わせる
        return (std::max)(m_minBlockSize, RoundUpSize(size, alignment));
    }

    // メモリの割り当て
    void *TLSFAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
            return nullptr;

        // サイズをアライメントに調整
        size_t alignedSize = AlignSize(size, alignment);

        // 適切なフリーブロックを探す
        BlockHeader *block = FindFreeBlock(alignedSize);
        if (!block)
        {
            return nullptr; // 適切なブロックが見つからない
        }        // ブロックが必要なサイズよりもずっと大きい場合は分割
        if ((block->size - alignedSize) >= (m_minBlockSize + sizeof(BlockHeader)))
        {
            block = SplitBlock(block, alignedSize);
        }
        else
        {
            // 分割しない場合は、ここでフリーリストから削除
            RemoveFromFreeList(block);
        }

        // ブロックを使用中としてマーク
        block->used = true;

        // 割り当てサイズを更新
        m_allocatedSize += block->size;

        // ペイロードポインタを返す
        void *payload = GetBlockPayload(block);

#ifdef _WIN32
        // デバッグ情報: メモリ確保をトレース
        printf("ALLOCATE: Ptr=%p, Block=%p, Size=%zu\n", payload, block, block->size);
        fflush(stdout);
#endif

        return payload;
    } // メモリの解放
    void TLSFAllocator::Deallocate(void *ptr)
    {
        if (!ptr)
            return;

        // ペイロードポインタからブロックヘッダーを取得
        BlockHeader *block = GetBlockHeader(ptr);

        // 有効なポインタかチェック
        assert(block >= m_memoryPool &&
               reinterpret_cast<char *>(block) < (reinterpret_cast<char *>(m_memoryPool) + m_memorySize));

#ifdef _WIN32
        // デバッグ情報: すべてのDeallocationをトレース
        printf("DEALLOCATE ATTEMPT: Ptr=%p, Block=%p, Size=%zu, Used=%s\n",
                  ptr, block, block->size, block->used ? "true" : "false");
        fflush(stdout);

        // スタックトレースを取得（簡易版）
        void *stack[10];
        WORD numFrames = CaptureStackBackTrace(0, 10, stack, nullptr);
        printf("CALL STACK:\n");
        for (WORD i = 0; i < numFrames; ++i)
        {
            printf("  Frame %d: %p\n", i, stack[i]);
        }
        fflush(stdout);
#endif        // デバッグ情報: ダブルフリーエラーの詳細を出力
        if (!block->used)
        {
#ifdef _WIN32
            printf("DOUBLE FREE DETECTED!\nPtr: %p\nBlock: %p\nSize: %zu\nUsed: %s\n",
                      ptr, block, block->size, block->used ? "true" : "false");
            printf("This indicates that the same memory block is being freed twice!\n");
            fflush(stdout);
#endif
            // 一時的にアサーションをより詳細にする
            assert(false && "Double free detected - block is already free!");
        }
        assert(block->used);

#ifdef _WIN32
        // デバッグ情報: 正常なメモリ解放をトレース
        printf("DEALLOCATE SUCCESS: Ptr=%p, Block=%p, Size=%zu\n", ptr, block, block->size);
        fflush(stdout);
#endif

        // 割り当てサイズを更新
        m_allocatedSize -= block->size;

        // ブロックを未使用としてマーク
        block->used = false;

        // 隣接する空きブロックと結合
        block = CoalesceBlocks(block);

        // フリーリストに追加
        InsertToFreeList(block);
    }

    // 割り当て済みメモリサイズを取得
    size_t TLSFAllocator::GetAllocatedSize() const
    {
        return m_allocatedSize;
    }    // フリーリストからブロックを削除
    void TLSFAllocator::RemoveFromFreeList(BlockHeader *block)
    {
        int flIndex, slIndex;
        MappingInsert(block->size, flIndex, slIndex);

#ifdef _WIN32
        // デバッグ: ブロックがフリーリストに存在するかチェック
        bool found = false;
        BlockHeader *current = m_freeBlocks[flIndex][slIndex];
        while (current)
        {
            if (current == block)
            {
                found = true;
                break;
            }
            current = current->nextFree;
        }
          if (!found)
        {
            printf("WARNING: Block %p not found in free list during removal!\n", block);
            fflush(stdout);
            assert(false && "Block not found in free list!");
            return;
        }
#endif

        // リストから削除
        if (block->prevFree)
        {
            block->prevFree->nextFree = block->nextFree;
        }
        else
        {
            m_freeBlocks[flIndex][slIndex] = block->nextFree;
        }

        if (block->nextFree)
        {
            block->nextFree->prevFree = block->prevFree;
        }

        // この要素がリスト最後の要素だった場合、ビットマップを更新
        if (!m_freeBlocks[flIndex][slIndex])
        {
            m_slBitmap[flIndex] &= ~(1 << slIndex);
            if (m_slBitmap[flIndex] == 0)
            {
                m_flBitmap &= ~(1 << flIndex);
            }
        }

        // 前後のフリーリストポインタをクリア
        block->prevFree = nullptr;
        block->nextFree = nullptr;
    }// フリーリストにブロックを挿入
    void TLSFAllocator::InsertToFreeList(BlockHeader *block)
    {
        int flIndex, slIndex;
        MappingInsert(block->size, flIndex, slIndex);

#ifdef _WIN32
        // デバッグ: 既にフリーリストに存在するかチェック
        BlockHeader *current = m_freeBlocks[flIndex][slIndex];        while (current)
        {
            if (current == block)
            {
                printf("WARNING: Block %p already exists in free list!\n", block);
                fflush(stdout);
                assert(false && "Block already in free list!");
                return;
            }
            current = current->nextFree;
        }
#endif

        // リンクリストの先頭に挿入
        block->nextFree = m_freeBlocks[flIndex][slIndex];
        if (m_freeBlocks[flIndex][slIndex])
        {
            m_freeBlocks[flIndex][slIndex]->prevFree = block;
        }

        m_freeBlocks[flIndex][slIndex] = block;
        block->prevFree = nullptr;

        // ビットマップを更新
        m_flBitmap |= (1 << flIndex);
        m_slBitmap[flIndex] |= (1 << slIndex);
    }// サイズからマッピングインデックスを計算（挿入用）
    void TLSFAllocator::MappingInsert(size_t size, int &flIndex, int &slIndex)
    {
        // ファーストレベルインデックス（サイズのMSB）
        flIndex = FindMSB(size);

        // セカンドレベルインデックス（サイズの詳細部分）
        if (flIndex < SL_INDEX_COUNT)
        {
            slIndex = static_cast<int>(size >> (flIndex - SL_INDEX_COUNT + 1));
        }
        else
        {
            slIndex = static_cast<int>(size >> flIndex);
        }
        slIndex &= (SL_INDEX_COUNT - 1);
    }

    // サイズからマッピングインデックスを計算（検索用）
    void TLSFAllocator::MappingSearch(size_t size, int &flIndex, int &slIndex)
    {
        // ファーストレベルインデックス（サイズのMSB）
        flIndex = FindMSB(size);

        // セカンドレベルインデックス（サイズの詳細部分）
        if (flIndex < SL_INDEX_COUNT)
        {
            slIndex = 0;
        }
        else
        {
            slIndex = static_cast<int>(size >> (flIndex - SL_INDEX_COUNT + 1));
            slIndex &= (SL_INDEX_COUNT - 1);
        }
    }

    // 指定インデックス以上の最初の空きブロックを見つける
    void TLSFAllocator::FindSuitableBlock(int &flIndex, int &slIndex)
    {
        // 同じFLインデックスで、より大きなSLインデックスをまず探す
        uint32_t slBitmap = m_slBitmap[flIndex] & (~0u << slIndex);

        // 同じFL行に適切なブロックがない場合、より上位のFLを探す
        if (slBitmap == 0)
        {
            // 上位のFLでフリーブロックを探す
            uint32_t flBitmap = m_flBitmap & (~0u << (flIndex + 1));
            if (flBitmap == 0)
            {
                // 十分な大きさのブロックが見つからない
                flIndex = -1;
                slIndex = -1;
                return;
            }

            // 最下位のセットビットを見つける
            flIndex = FindLSB(flBitmap);
            // そのFL行の最小のSLセルを選択
            slIndex = FindLSB(m_slBitmap[flIndex]);
        }
        else
        {
            // 同じFL行で次に大きなSLを選択
            slIndex = FindLSB(slBitmap);
        }
    }

    // 適切なフリーブロックを検索
    TLSFAllocator::BlockHeader *TLSFAllocator::FindFreeBlock(size_t size)
    {
        int flIndex, slIndex;
        MappingSearch(size, flIndex, slIndex);

        // 適切なインデックスを見つける
        FindSuitableBlock(flIndex, slIndex);

        // 適切なブロックが見つからない
        if (flIndex < 0 || slIndex < 0)
        {
            return nullptr;
        }

        // 見つかったブロックを返す
        return m_freeBlocks[flIndex][slIndex];
    }    // ブロックを分割する
    TLSFAllocator::BlockHeader *TLSFAllocator::SplitBlock(BlockHeader *block, size_t size)
    {
        // 分割可能なサイズかチェック
        if (block->size < size + sizeof(BlockHeader) + m_minBlockSize)
        {
            return block; // 分割できない
        }

        // まず元のブロックをフリーリストから削除
        RemoveFromFreeList(block);

        // 分割位置を計算
        size_t remainingSize = block->size - size - sizeof(BlockHeader);
        block->size = size; // 元のブロックのサイズを更新

        // 新しいブロックを作成
        BlockHeader *newBlock = reinterpret_cast<BlockHeader *>(
            reinterpret_cast<char *>(block) + sizeof(BlockHeader) + block->size);

        // 新ブロックのヘッダを設定
        newBlock->size = remainingSize;
        newBlock->used = false;
        newBlock->prev = block;
        newBlock->next = block->next;
        newBlock->prevFree = nullptr;
        newBlock->nextFree = nullptr;

        // リンクを更新
        if (block->next)
        {
            block->next->prev = newBlock;
        }
        block->next = newBlock;

        // 新しいブロックをフリーリストに追加
        InsertToFreeList(newBlock);

        return block;
    }

    // 隣接する空きブロックを結合
    TLSFAllocator::BlockHeader *TLSFAllocator::CoalesceBlocks(BlockHeader *block)
    {
        // 後続ブロックと結合
        if (block->next && !block->next->used)
        {
            BlockHeader *nextBlock = block->next;

            // 後続ブロックをフリーリストから削除
            RemoveFromFreeList(nextBlock);

            // ブロックを結合
            block->size += sizeof(BlockHeader) + nextBlock->size;
            block->next = nextBlock->next;

            // 次のブロックのprevポインタを更新
            if (nextBlock->next)
            {
                nextBlock->next->prev = block;
            }
        }

        // 前のブロックと結合
        if (block->prev && !block->prev->used)
        {
            BlockHeader *prevBlock = block->prev;

            // 前ブロックをフリーリストから削除
            RemoveFromFreeList(prevBlock);

            // ブロックを結合
            prevBlock->size += sizeof(BlockHeader) + block->size;
            prevBlock->next = block->next;

            // 次のブロックのprevポインタを更新
            if (block->next)
            {
                block->next->prev = prevBlock;
            }

            block = prevBlock; // 前のブロックを返す
        }

        return block;
    } // ブロックの使用可能サイズを取得
    size_t TLSFAllocator::GetBlockPayloadSize(const BlockHeader *block) const
    {
        return block->size;
    }

    // ヘッダーからペイロードポインタを取得
    void *TLSFAllocator::GetBlockPayload(BlockHeader *block)
    {
        return reinterpret_cast<char *>(block) + sizeof(BlockHeader);
    } // ペイロードポインタからヘッダーを取得
    TLSFAllocator::BlockHeader *TLSFAllocator::GetBlockHeader(void *payload)
    {
        return reinterpret_cast<BlockHeader *>(
            reinterpret_cast<char *>(payload) - sizeof(BlockHeader));
    }

    // 指定されたポインタのブロックサイズを取得
    size_t TLSFAllocator::GetBlockSize(void *ptr) const
    {
        if (!ptr)
            return 0;

        // ペイロードポインタからブロックヘッダーを取得
        BlockHeader *block = const_cast<TLSFAllocator *>(this)->GetBlockHeader(ptr);

        // 有効なポインタかチェック
        if (block < m_memoryPool ||
            reinterpret_cast<char *>(block) >= (reinterpret_cast<char *>(m_memoryPool) + m_memorySize))
        {
            return 0; // 無効なポインタ
        }

        // 使用中のブロックかチェック
        if (!block->used)
        {
            return 0; // 未使用ブロック
        }

        return GetBlockPayloadSize(block);
    }
}