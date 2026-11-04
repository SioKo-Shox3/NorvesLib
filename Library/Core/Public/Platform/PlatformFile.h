#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core::Platform
{
    enum class FileErrorKind
    {
        None,
        InvalidPath,
        NotFound,
        SharingViolation,
        AccessDenied,
        IoError,
        VerifyMismatch
    };

    struct FileOperationResult
    {
        bool bSuccess = false;
        FileErrorKind Kind = FileErrorKind::None;
        uint32_t NativeError = 0;
        Container::String Message;
    };

    /**
     * @brief 原子的な書き込みと読み込みに使う操作。戻り値は Win32 エラーコードで、0 は成功。
     * ハンドルは CreateNew 成功時だけ取得する。Close は失敗時も所有権を消費し、再び閉じない。
     * Write は渡された全バイトを書き込み、ReadAll は成功時だけ出力を更新する。
     */
    class IFileOperations
    {
    public:
        using Handle = void*;
        virtual ~IFileOperations() = default;
        virtual uint32_t CreateNew(const Container::WideString& path, Handle& outHandle) = 0;
        virtual uint32_t Write(Handle handle, Container::Span<const uint8_t> bytes) = 0;
        virtual uint32_t Flush(Handle handle) = 0;
        virtual uint32_t Close(Handle handle) = 0;
        virtual uint32_t Replace(const Container::WideString& path, const Container::WideString& temporary,
            const Container::WideString& backup) = 0;
        virtual uint32_t MoveReplace(const Container::WideString& from, const Container::WideString& to) = 0;
        virtual uint32_t Delete(const Container::WideString& path) = 0;
        virtual uint32_t ReadAll(const Container::WideString& path, Container::VariableArray<uint8_t>& outBytes) = 0;
        virtual void Sleep(uint32_t milliseconds) = 0;
    };

    IFileOperations& GetDefaultFileOperations();

    /** @brief 同じディレクトリの一時ファイルから置換し、読み戻して内容を検証する。パスは UTF-8。 */
    FileOperationResult WriteFileAtomic(const Container::String& path, Container::Span<const uint8_t> bytes, bool bKeepBackup);
    FileOperationResult WriteFileAtomic(const Container::String& path, Container::Span<const uint8_t> bytes,
        bool bKeepBackup, IFileOperations& ops);

    /** @brief ファイル全体を読む。失敗時は出力を変更しない。パスは UTF-8。 */
    FileOperationResult ReadFileBytes(const Container::String& path, Container::VariableArray<uint8_t>& outBytes);
    FileOperationResult ReadFileBytes(const Container::String& path, Container::VariableArray<uint8_t>& outBytes, IFileOperations& ops);

    /** @brief 直下の「ファイル名.tmp-数字-数字」に一致する一時ファイルを削除し、削除数を返す。 */
    uint32_t CleanupAtomicWriteTemporaries(const Container::String& directory);
    bool FileExists(const Container::String& path);
    FileOperationResult RenameFileNoReplace(const Container::String& from, const Container::String& to);
    FileOperationResult DeleteFileIfExists(const Container::String& path);
} // namespace NorvesLib::Core::Platform
