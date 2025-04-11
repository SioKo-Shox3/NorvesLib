#include "../Public/MemoryPool.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace NorvesLib::Memory
{
    // アライメントに合わせてサイズを切り上げる
    inline size_t AlignSize(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    // コンストラクタ
    MemoryPool::MemoryPool(size_t blockSize, size_t blocksPerChunk, size_t alignment)
        : m_blockSize(std::max(AlignSize(blockSize, alignment), alignment))
        , m_alignment(alignment)
        , m_blocksPerChunk(blocksPerChunk)
        , m_totalBlocks(0)
        , m_allocatedBlocks(0)
    {
        // 最初のチャンクを割り当てる
        AllocateChunk();
    }

    // デストラクタ
    MemoryPool::~MemoryPool()
    {
        // すべてのチャンクを解放
        for (auto& chunk : m_chunks)
        {
            std::free(chunk.memory);
        }
    }

    // 新しいチャンクを確保
    void MemoryPool::AllocateChunk()
    {
        // ブロックのアライメントを考慮したメモリを確保
        void* chunkMemory = std::aligned_alloc(m_alignment, m_blockSize * m_blocksPerChunk);
        if (!chunkMemory)
        {
            throw std::bad_alloc();
        }

        // チャンク情報を登録
        Chunk newChunk;
        newChunk.memory = chunkMemory;
        newChunk.blockCount = m_blocksPerChunk;
        m_chunks.push_back(newChunk);
        
        // 各ブロックをフリーリストに追加
        char* blockPtr = static_cast<char*>(chunkMemory);
        for (size_t i = 0; i < m_blocksPerChunk; ++i)
        {
            m_freeBlocks.push_back(blockPtr);
            blockPtr += m_blockSize;
        }

        // 合計ブロック数を更新
        m_totalBlocks += m_blocksPerChunk;
    }

    // メモリブロックを割り当てる
    void* MemoryPool::Allocate(size_t size, size_t /*alignment*/)
    {
        // 要求サイズがブロックサイズを超える場合はnullptr
        if (size > m_blockSize)
        {
            return nullptr;
        }

        // フリーリストが空の場合、新しいチャンクを確保
        if (m_freeBlocks.empty())
        {
            AllocateChunk();
        }

        // フリーリストから1つ取得
        void* block = m_freeBlocks.back();
        m_freeBlocks.pop_back();
        m_allocatedBlocks++;
        
        return block;
    }

    // メモリブロックを解放
    void MemoryPool::Deallocate(void* ptr)
    {
        if (!ptr)
        {
            return;
        }

        // プール管理外のポインタでないことを確認（デバッグ用）
        #ifndef NDEBUG
        bool validPtr = false;
        for (const auto& chunk : m_chunks)
        {
            char* start = static_cast<char*>(chunk.memory);
            char* end = start + (m_blockSize * chunk.blockCount);
            if (ptr >= start && ptr < end && ((static_cast<char*>(ptr) - start) % m_blockSize == 0))
            {
                validPtr = true;
                break;
            }
        }
        assert(validPtr && "Invalid pointer deallocated from MemoryPool");
        #endif

        // ブロックをフリーリストに戻す
        m_freeBlocks.push_back(ptr);
        m_allocatedBlocks--;
    }

    // 割り当て済みメモリサイズを取得
    size_t MemoryPool::GetAllocatedSize() const
    {
        return m_allocatedBlocks * m_blockSize;
    }
}