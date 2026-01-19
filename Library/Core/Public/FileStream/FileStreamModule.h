#pragma once

#include "FileStreamForward.h"
#include "IFileStream.h"
#include "FileStream.h"
#include "AsyncFileStream.h"

/**
 * @file FileStreamModule.h
 * @brief FileStreamモジュールのメインヘッダーファイル
 *
 * このファイルをインクルードすることで、FileStreamモジュールの
 * すべての機能にアクセスできます。
 */

namespace NorvesLib::FileStream
{
    // ファクトリー関数
    inline FileStreamPtr CreateFileStream(const String &filePath, FileMode mode = FileMode::Read,
                                          FileAccess access = FileAccess::Read, FileShare share = FileShare::Read)
    {
        return FileStream::Create(filePath, mode, access, share);
    }

    inline FileStreamUniquePtr CreateUniqueFileStream(const String &filePath, FileMode mode = FileMode::Read,
                                                      FileAccess access = FileAccess::Read, FileShare share = FileShare::Read)
    {
        return FileStream::CreateUnique(filePath, mode, access, share);
    }

    // 非同期ファイルストリーム用ファクトリー関数
    inline AsyncFileStreamPtr CreateAsyncFileStream(const String &filePath, FileMode mode = FileMode::Read,
                                                    FileAccess access = FileAccess::Read, FileShare share = FileShare::Read)
    {
        return AsyncFileStream::Create(filePath, mode, access, share);
    }

    inline AsyncFileStreamUniquePtr CreateUniqueAsyncFileStream(const String &filePath, FileMode mode = FileMode::Read,
                                                                FileAccess access = FileAccess::Read, FileShare share = FileShare::Read)
    {
        return AsyncFileStream::CreateUnique(filePath, mode, access, share);
    }
} // namespace NorvesLib::FileStream
