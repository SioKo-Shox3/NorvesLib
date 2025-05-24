#pragma once

#include "GlobalMemoryAllocator.h"
#include <cstdlib>
#include <new>

/**
 * このヘッダーファイルは、組み込みのメモリ確保・解放関数を
 * NorvesLib::Core::GlobalMemoryAllocatorを使用するように再定義します
 * 
 * このヘッダーを含めると、以下の関数が置き換えられます：
 * - operator new / delete
 * - malloc / calloc / realloc / free
 */

#ifndef NORVES_LIB_DEFAULT_ALIGNMENT
#define NORVES_LIB_DEFAULT_ALIGNMENT 16
#endif

// C++のnew/deleteオペレータのオーバーロード

// 基本のnew
void* operator new(std::size_t size)
{
    void* ptr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// 配置new
void* operator new(std::size_t size, std::align_val_t alignment)
{
    void* ptr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size, static_cast<std::size_t>(alignment));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// noexceptのnew
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size);
}

// noexceptの配置new
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size, static_cast<std::size_t>(alignment));
}

// 基本のdelete
void operator delete(void* ptr) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// サイズ指定のdelete (C++14)
void operator delete(void* ptr, std::size_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// アライメント指定のdelete (C++17)
void operator delete(void* ptr, std::align_val_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// サイズとアライメント指定のdelete (C++17)
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// nothrowのdelete
void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// nothrowでアライメント指定のdelete
void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// 配列のnew
void* operator new[](std::size_t size)
{
    void* ptr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// アライメント指定の配列new
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    void* ptr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size, static_cast<std::size_t>(alignment));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// noexceptの配列new
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size);
}

// noexceptでアライメント指定の配列new
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size, static_cast<std::size_t>(alignment));
}

// 配列のdelete
void operator delete[](void* ptr) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// サイズ指定の配列delete
void operator delete[](void* ptr, std::size_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// アライメント指定の配列delete
void operator delete[](void* ptr, std::align_val_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// サイズとアライメント指定の配列delete
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// nothrowの配列delete
void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// nothrowでアライメント指定の配列delete
void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept
{
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
}

// C言語のメモリ関数の再定義

extern "C" 
{

// malloc関数の置き換え
void* malloc(size_t size)
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size);
}

// calloc関数の置き換え
void* calloc(size_t num, size_t size)
{
    size_t totalSize = num * size;
    void* ptr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(totalSize);
    if (ptr) {
        std::memset(ptr, 0, totalSize);
    }
    return ptr;
}

// realloc関数の置き換え
void* realloc(void* ptr, size_t newSize)
{
    if (!ptr) {
        return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(newSize);
    }
    
    if (newSize == 0) {
        NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
        return nullptr;
    }
    
    // 現在の実装では正確なサイズを取得できないため、新しいメモリを確保してコピー
    void* newPtr = NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(newSize);
    if (!newPtr) {
        return nullptr;
    }
    
    // 元のサイズが不明なため、新しいサイズをコピー（安全側に倒す）
    // 注意: これは最適ではなく、元のブロックサイズを追跡する方法が理想的
    std::memcpy(newPtr, ptr, newSize);
    NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
    
    return newPtr;
}

// free関数の置き換え
void free(void* ptr)
{
    if (ptr) {
        NorvesLib::Core::GlobalMemoryAllocator::Get().Deallocate(ptr);
    }
}

// aligned_alloc関数の置き換え
void* aligned_alloc(size_t alignment, size_t size)
{
    return NorvesLib::Core::GlobalMemoryAllocator::Get().Allocate(size, alignment);
}

} // extern "C"