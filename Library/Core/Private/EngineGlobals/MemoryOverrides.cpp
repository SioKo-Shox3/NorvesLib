// メモリオーバーライドの実装では、標準関数を使用する必要があるため
// オーバーライドを無効化
#define DISABLE_MEMORY_OVERRIDES

#include "EngineGlobals/MemoryOverrides.h"
#include "Memory/MemorySystem.h"
#include <cstring>

namespace NorvesLib::Memory
{
    // ===========================================
    // 静的変数
    // ===========================================

    static bool s_bInitialized = false;

    // ===========================================
    // 初期化・終了処理
    // ===========================================

    void Initialize()
    {
        if (s_bInitialized)
        {
            return;
        }

        MemorySystem::Initialize();
        s_bInitialized = true;
    }

    void Shutdown()
    {
        if (!s_bInitialized)
        {
            return;
        }

        MemorySystem::Shutdown();
        s_bInitialized = false;
    }

    bool IsInitialized()
    {
        return s_bInitialized;
    }

    // ===========================================
    // メモリ確保・解放関数
    // ===========================================

    void *Malloc(size_t size)
    {
        return AlignedMalloc(size, DefaultAlignment);
    }

    void *AlignedMalloc(size_t size, size_t alignment)
    {
        if (s_bInitialized)
        {
            return MemorySystem::Allocate(size, alignment);
        }

        // MemorySystemが初期化されていない場合は直接OSから割り当て
#ifdef _WIN32
        return ::_aligned_malloc(size, alignment);
#else
        void *ptr = nullptr;
        if (posix_memalign(&ptr, alignment, size) != 0)
        {
            return nullptr;
        }
        return ptr;
#endif
    }

    void Free(void *ptr)
    {
        AlignedFree(ptr);
    }

    void AlignedFree(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        if (s_bInitialized)
        {
            MemorySystem::Deallocate(ptr);
            return;
        }

        // MemorySystemが初期化されていない場合は直接OSに返却
#ifdef _WIN32
        ::_aligned_free(ptr);
#else
        ::free(ptr);
#endif
    }

    void *Calloc(size_t num, size_t size)
    {
        size_t totalSize = num * size;
        void *ptr = Malloc(totalSize);
        if (ptr)
        {
            std::memset(ptr, 0, totalSize);
        }
        return ptr;
    }

    void *Realloc(void *ptr, size_t newSize)
    {
        return AlignedRealloc(ptr, newSize, DefaultAlignment);
    }

    void *AlignedRealloc(void *ptr, size_t newSize, size_t alignment)
    {
        if (s_bInitialized)
        {
            return MemorySystem::Reallocate(ptr, newSize, alignment);
        }

        // MemorySystemが初期化されていない場合
        if (!ptr)
        {
            return AlignedMalloc(newSize, alignment);
        }

        if (newSize == 0)
        {
            AlignedFree(ptr);
            return nullptr;
        }

#ifdef _WIN32
        return ::_aligned_realloc(ptr, newSize, alignment);
#else
        // POSIXにはaligned_reallocがないため、手動で実装
        void *newPtr = nullptr;
        if (posix_memalign(&newPtr, alignment, newSize) != 0)
        {
            return nullptr;
        }

        // 古いサイズは不明なため、新しいサイズ分だけコピー
        // 実際には古いブロックのサイズを取得すべきだが、
        // 初期化前なのでシンプルに実装
        std::memcpy(newPtr, ptr, newSize);
        ::free(ptr);
        return newPtr;
#endif
    }

    size_t GetBlockSize(const void *ptr)
    {
        if (s_bInitialized)
        {
            return MemorySystem::GetBlockSize(ptr);
        }
        return 0;
    }

    size_t GetTotalAllocatedSize()
    {
        if (s_bInitialized)
        {
            return MemorySystem::GetTotalAllocatedSize();
        }
        return 0;
    }

} // namespace NorvesLib::Memory

// ===========================================
// グローバル new/delete 演算子オーバーライド
// ===========================================

// 通常の operator new
void *operator new(std::size_t size)
{
    void *ptr = NorvesLib::Memory::Malloc(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

// nothrow 版 operator new
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return NorvesLib::Memory::Malloc(size);
}

// アライメント指定版 operator new (C++17)
void *operator new(std::size_t size, std::align_val_t alignment)
{
    void *ptr = NorvesLib::Memory::AlignedMalloc(size, static_cast<size_t>(alignment));
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

// アライメント指定版 nothrow operator new (C++17)
void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return NorvesLib::Memory::AlignedMalloc(size, static_cast<size_t>(alignment));
}

// 通常の operator delete
void operator delete(void *ptr) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// サイズ付き operator delete (C++14)
void operator delete(void *ptr, std::size_t) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// nothrow 版 operator delete
void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// アライメント指定版 operator delete (C++17)
void operator delete(void *ptr, std::align_val_t) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}

// アライメント・サイズ指定版 operator delete (C++17)
void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}

// アライメント・nothrow 版 operator delete (C++17)
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}

// 配列版 operator new[]
void *operator new[](std::size_t size)
{
    void *ptr = NorvesLib::Memory::Malloc(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

// 配列版 nothrow operator new[]
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return NorvesLib::Memory::Malloc(size);
}

// 配列版 アライメント指定 operator new[] (C++17)
void *operator new[](std::size_t size, std::align_val_t alignment)
{
    void *ptr = NorvesLib::Memory::AlignedMalloc(size, static_cast<size_t>(alignment));
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

// 配列版 アライメント指定 nothrow operator new[] (C++17)
void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return NorvesLib::Memory::AlignedMalloc(size, static_cast<size_t>(alignment));
}

// 配列版 operator delete[]
void operator delete[](void *ptr) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// 配列版 サイズ付き operator delete[] (C++14)
void operator delete[](void *ptr, std::size_t) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// 配列版 nothrow operator delete[]
void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    NorvesLib::Memory::Free(ptr);
}

// 配列版 アライメント指定 operator delete[] (C++17)
void operator delete[](void *ptr, std::align_val_t) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}

// 配列版 アライメント・サイズ指定 operator delete[] (C++17)
void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}

// 配列版 アライメント・nothrow operator delete[] (C++17)
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    NorvesLib::Memory::AlignedFree(ptr);
}
