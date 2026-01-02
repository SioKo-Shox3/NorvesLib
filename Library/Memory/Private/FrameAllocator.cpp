#include "../Public/FrameAllocator.h"
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
    FrameAllocator::FrameAllocator(size_t sizePerBuffer, bool bDoubleBuffered)
        : m_bufferSize(AlignUp(sizePerBuffer, Config::PageSize)), m_currentBuffer(0), m_numBuffers(bDoubleBuffered ? 2 : 1), m_frameNumber(0)
    {
        assert(sizePerBuffer > 0 && "Buffer size must be greater than zero");
        assert(m_bufferSize <= Config::MaxAllocationSize && "Buffer size exceeds maximum allocation size");

        // すべてのバッファを初期化
        for (uint32_t i = 0; i < MaxBuffers; ++i)
        {
            m_buffers[i].memory = nullptr;
            m_buffers[i].offset = 0;
        }

        // 必要なバッファを確保
        for (uint32_t i = 0; i < m_numBuffers; ++i)
        {
#ifdef _WIN32
            m_buffers[i].memory = _aligned_malloc(m_bufferSize, Config::DefaultAlignment);
#else
            m_buffers[i].memory = std::aligned_alloc(Config::DefaultAlignment, m_bufferSize);
#endif

            if (!m_buffers[i].memory)
            {
                // 確保済みのバッファを解放
                for (uint32_t j = 0; j < i; ++j)
                {
#ifdef _WIN32
                    _aligned_free(m_buffers[j].memory);
#else
                    std::free(m_buffers[j].memory);
#endif
                    m_buffers[j].memory = nullptr;
                }
                throw std::bad_alloc();
            }
        }
    }

    FrameAllocator::~FrameAllocator()
    {
        for (uint32_t i = 0; i < m_numBuffers; ++i)
        {
            if (m_buffers[i].memory)
            {
#ifdef _WIN32
                _aligned_free(m_buffers[i].memory);
#else
                std::free(m_buffers[i].memory);
#endif
                m_buffers[i].memory = nullptr;
            }
        }
    }

    FrameAllocator::FrameAllocator(FrameAllocator &&other) noexcept
        : m_bufferSize(other.m_bufferSize), m_currentBuffer(other.m_currentBuffer), m_numBuffers(other.m_numBuffers), m_frameNumber(other.m_frameNumber)
    {
        for (uint32_t i = 0; i < MaxBuffers; ++i)
        {
            m_buffers[i] = other.m_buffers[i];
            other.m_buffers[i].memory = nullptr;
            other.m_buffers[i].offset = 0;
        }
        other.m_bufferSize = 0;
        other.m_currentBuffer = 0;
        other.m_numBuffers = 0;
        other.m_frameNumber = 0;
    }

    FrameAllocator &FrameAllocator::operator=(FrameAllocator &&other) noexcept
    {
        if (this != &other)
        {
            // 既存のバッファを解放
            for (uint32_t i = 0; i < m_numBuffers; ++i)
            {
                if (m_buffers[i].memory)
                {
#ifdef _WIN32
                    _aligned_free(m_buffers[i].memory);
#else
                    std::free(m_buffers[i].memory);
#endif
                }
            }

            // 移動
            m_bufferSize = other.m_bufferSize;
            m_currentBuffer = other.m_currentBuffer;
            m_numBuffers = other.m_numBuffers;
            m_frameNumber = other.m_frameNumber;

            for (uint32_t i = 0; i < MaxBuffers; ++i)
            {
                m_buffers[i] = other.m_buffers[i];
                other.m_buffers[i].memory = nullptr;
                other.m_buffers[i].offset = 0;
            }

            other.m_bufferSize = 0;
            other.m_currentBuffer = 0;
            other.m_numBuffers = 0;
            other.m_frameNumber = 0;
        }
        return *this;
    }

    void *FrameAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
        {
            return nullptr;
        }

        Buffer &currentBuffer = m_buffers[m_currentBuffer];
        if (!currentBuffer.memory)
        {
            return nullptr;
        }

        // 現在のアドレスを計算
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(currentBuffer.memory) + currentBuffer.offset;

        // アライメント調整
        uintptr_t alignedAddr = AlignUp(currentAddr, alignment);
        size_t alignmentPadding = alignedAddr - currentAddr;

        // 必要な合計サイズ
        size_t totalRequired = alignmentPadding + size;

        // 容量チェック
        if (currentBuffer.offset + totalRequired > m_bufferSize)
        {
            return nullptr; // メモリ不足
        }

        // オフセットを更新
        currentBuffer.offset += totalRequired;

        return reinterpret_cast<void *>(alignedAddr);
    }

    void FrameAllocator::Deallocate(void *ptr)
    {
        // フレームアロケータでは個別の解放はサポートしない
        (void)ptr;

        // デバッグビルドでは警告を出す
        assert(false && "FrameAllocator does not support individual deallocation. Use SwapBuffers() or Reset() instead.");
    }

    void FrameAllocator::SwapBuffers()
    {
        ++m_frameNumber;

        if (m_numBuffers == 1)
        {
            // シングルバッファ：単にリセット
            m_buffers[0].offset = 0;
        }
        else
        {
            // ダブルバッファ：次のバッファに切り替え、それをリセット
            m_currentBuffer = (m_currentBuffer + 1) % m_numBuffers;
            m_buffers[m_currentBuffer].offset = 0;
        }
    }

    void FrameAllocator::Reset()
    {
        for (uint32_t i = 0; i < m_numBuffers; ++i)
        {
            m_buffers[i].offset = 0;
        }
        m_currentBuffer = 0;
        m_frameNumber = 0;
    }

    void FrameAllocator::ResetCurrentBuffer()
    {
        m_buffers[m_currentBuffer].offset = 0;
    }

    size_t FrameAllocator::GetAllocatedSize() const
    {
        return m_buffers[m_currentBuffer].offset;
    }

    size_t FrameAllocator::GetTotalSize() const
    {
        return m_bufferSize * m_numBuffers;
    }

    size_t FrameAllocator::GetBufferSize() const
    {
        return m_bufferSize;
    }

    uint32_t FrameAllocator::GetCurrentBufferIndex() const
    {
        return m_currentBuffer;
    }

    bool FrameAllocator::IsDoubleBuffered() const
    {
        return m_numBuffers == 2;
    }

    AllocatorType FrameAllocator::GetType() const
    {
        return AllocatorType::Frame;
    }

    bool FrameAllocator::OwnsMemory(const void *ptr) const
    {
        if (!ptr)
        {
            return false;
        }

        uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);

        for (uint32_t i = 0; i < m_numBuffers; ++i)
        {
            if (!m_buffers[i].memory)
            {
                continue;
            }

            uintptr_t bufferStart = reinterpret_cast<uintptr_t>(m_buffers[i].memory);
            uintptr_t bufferEnd = bufferStart + m_bufferSize;

            if (ptrAddr >= bufferStart && ptrAddr < bufferEnd)
            {
                return true;
            }
        }

        return false;
    }

    uint64_t FrameAllocator::GetFrameNumber() const
    {
        return m_frameNumber;
    }

} // namespace NorvesLib::Memory
