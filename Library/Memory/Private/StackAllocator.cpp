#include "../Public/StackAllocator.h"
#include <cassert>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace NorvesLib::Memory
{
    StackAllocator::StackAllocator(size_t size)
        : m_memory(nullptr), m_totalSize(AlignUp(size, Config::PageSize)), m_currentOffset(0)
    {
        assert(size > 0 && "Stack size must be greater than zero");
        assert(m_totalSize <= Config::MaxAllocationSize && "Stack size exceeds maximum allocation size");

#ifdef _WIN32
        m_memory = _aligned_malloc(m_totalSize, Config::DefaultAlignment);
#else
        m_memory = std::aligned_alloc(Config::DefaultAlignment, m_totalSize);
#endif

        if (!m_memory)
        {
            throw std::bad_alloc();
        }
    }

    StackAllocator::~StackAllocator()
    {
        if (m_memory)
        {
#ifdef _WIN32
            _aligned_free(m_memory);
#else
            std::free(m_memory);
#endif
            m_memory = nullptr;
        }
    }

    StackAllocator::StackAllocator(StackAllocator &&other) noexcept
        : m_memory(other.m_memory), m_totalSize(other.m_totalSize), m_currentOffset(other.m_currentOffset)
    {
        other.m_memory = nullptr;
        other.m_totalSize = 0;
        other.m_currentOffset = 0;
    }

    StackAllocator &StackAllocator::operator=(StackAllocator &&other) noexcept
    {
        if (this != &other)
        {
            // 既存のメモリを解放
            if (m_memory)
            {
#ifdef _WIN32
                _aligned_free(m_memory);
#else
                std::free(m_memory);
#endif
            }

            // 移動
            m_memory = other.m_memory;
            m_totalSize = other.m_totalSize;
            m_currentOffset = other.m_currentOffset;

            other.m_memory = nullptr;
            other.m_totalSize = 0;
            other.m_currentOffset = 0;
        }
        return *this;
    }

    void *StackAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0 || !m_memory)
        {
            return nullptr;
        }

        // 現在のアドレスを計算
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_memory) + m_currentOffset;

        // アライメント調整
        uintptr_t alignedAddr = AlignUp(currentAddr, alignment);
        size_t alignmentPadding = alignedAddr - currentAddr;

        // 必要な合計サイズ
        size_t totalRequired = alignmentPadding + size;

        // 容量チェック
        if (m_currentOffset + totalRequired > m_totalSize)
        {
            return nullptr; // メモリ不足
        }

        // オフセットを更新
        m_currentOffset += totalRequired;

        return reinterpret_cast<void *>(alignedAddr);
    }

    void StackAllocator::Deallocate(void *ptr)
    {
        // スタックアロケータでは個別の解放はサポートしない
        // FreeToMarker()を使用する必要がある
        (void)ptr;

        // デバッグビルドでは警告を出す
        assert(false && "StackAllocator does not support individual deallocation. Use FreeToMarker() instead.");
    }

    StackAllocator::Marker StackAllocator::GetMarker() const
    {
        return m_currentOffset;
    }

    void StackAllocator::FreeToMarker(Marker marker)
    {
        assert(marker <= m_currentOffset && "Invalid marker: cannot free to a position ahead of current offset");
        m_currentOffset = marker;
    }

    void StackAllocator::Reset()
    {
        m_currentOffset = 0;
    }

    size_t StackAllocator::GetAllocatedSize() const
    {
        return m_currentOffset;
    }

    size_t StackAllocator::GetTotalSize() const
    {
        return m_totalSize;
    }

    AllocatorType StackAllocator::GetType() const
    {
        return AllocatorType::Stack;
    }

    bool StackAllocator::OwnsMemory(const void *ptr) const
    {
        if (!m_memory || !ptr)
        {
            return false;
        }

        uintptr_t memStart = reinterpret_cast<uintptr_t>(m_memory);
        uintptr_t memEnd = memStart + m_totalSize;
        uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);

        return ptrAddr >= memStart && ptrAddr < memEnd;
    }

} // namespace NorvesLib::Memory
