#pragma once

#include "Core/Public/Container/Containers.h"
#include "Thread/Public/Task.h"

namespace NorvesLib::FileStream
{
    // 前方宣言
    class IFileStream;
    class FileStream;
    class AsyncFileStream;
    class Path;
    class Directory;

    // スマートポインタの定義
    using namespace NorvesLib::Core::Container;
    using FileStreamPtr = TSharedPtr<IFileStream>;
    using FileStreamUniquePtr = TUniquePtr<IFileStream>;
    using AsyncFileStreamPtr = TSharedPtr<AsyncFileStream>;
    using AsyncFileStreamUniquePtr = TUniquePtr<AsyncFileStream>;

    // 非同期操作用の型定義
    using AsyncReadTask = NorvesLib::Thread::TaskPtr;
    using AsyncWriteTask = NorvesLib::Thread::TaskPtr;

    // 列挙型
    enum class FileMode : uint8_t
    {
        Read,
        Write,
        Append,
        ReadWrite
    };

    enum class FileAccess : uint8_t
    {
        Read,
        Write,
        ReadWrite
    };

    enum class FileShare : uint8_t
    {
        None,
        Read,
        Write,
        ReadWrite,
        Delete
    };

    enum class SeekOrigin : uint8_t
    {
        Begin,
        Current,
        End
    };

    // 非同期操作結果構造体
    struct AsyncReadResult
    {
        size_t bytesRead = 0;
        bool bSuccess = false;
        String errorMessage;
    };

    struct AsyncWriteResult
    {
        size_t bytesWritten = 0;
        bool bSuccess = false;
        String errorMessage;
    };

} // namespace NorvesLib::FileStream