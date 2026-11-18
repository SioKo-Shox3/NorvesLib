// 資産ルートの解決と配下のパス検査に用いる共通処理を定義する。
#pragma once

#include "Container/Containers.h"

#include <Windows.h>

namespace Game::Bridge::AssetPath
{
    namespace Container = NorvesLib::Core::Container;

    // Windows ハンドルを所有し、解放漏れを防ぐ。
    struct NativeHandle
    {
        HANDLE Value = INVALID_HANDLE_VALUE;

        explicit NativeHandle(HANDLE handle = INVALID_HANDLE_VALUE) : Value(handle) {}
        NativeHandle(const NativeHandle&) = delete;
        NativeHandle& operator=(const NativeHandle&) = delete;
        ~NativeHandle() { Close(); }

        bool Close()
        {
            if (Value == INVALID_HANDLE_VALUE)
            {
                return true;
            }
            const HANDLE handle = Value;
            Value = INVALID_HANDLE_VALUE;
            return CloseHandle(handle) != 0;
        }
    };

    // UTF-8 の文字列を、不正な文字と埋め込み NUL を拒否してワイド文字列へ変換する。
    bool Utf8ToWide(const Container::String& text, Container::WideString& out);

    // 資産ルートを UTF-8 から絶対ワイドパスへ変換する。
    bool ResolveRoot(const Container::String& root, Container::WideString& out);

    // 空・先頭区切り・`:` 等の禁止文字・`..`・末尾が `.` や空白の要素（単独の `.` を除く）を拒否する。
    bool IsRelativeAssetPath(const Container::String& path);

    // 開いたハンドルから正規化済みの最終パスを取得する。
    bool FinalPath(HANDLE handle, Container::WideString& out);

    // 大文字小文字を区別せず、区切りを含めて最終パスがルート配下か判定する。
    bool IsWithinRoot(const Container::WideString& root, const Container::WideString& path);

    // ルートをディレクトリとして開き、その最終パスを取得する。
    bool OpenRoot(const Container::WideString& root, Container::WideString& outFinal);

    // ワイドパスを既定のコードページへ変換し、置換が必要な変換を拒否する。
    bool WideToFileStreamPath(const Container::WideString& path, Container::String& out);

    // パス末尾の ASCII 大文字を小文字として指定の拡張子と比較する。
    bool HasExtension(const Container::String& path, const char* extension);

} // namespace Game::Bridge::AssetPath
