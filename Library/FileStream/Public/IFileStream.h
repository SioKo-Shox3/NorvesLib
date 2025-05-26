#pragma once

#include "FileStreamForward.h"
#include <cstdint>

namespace NorvesLib::FileStream
{

    /**
     * @brief ファイルストリームのインターフェースクラス
     * ファイルの読み書き操作を抽象化します
     */
    class IFileStream
    {
    public:
        virtual ~IFileStream() = default;

        /**
         * @brief ファイルを開く
         * @param filePath ファイルパス
         * @param mode ファイルモード
         * @param access アクセス権
         * @param share 共有モード
         * @return 成功した場合true
         */
        virtual bool Open(const String &filePath, FileMode mode, FileAccess access = FileAccess::ReadWrite, FileShare share = FileShare::Read) = 0;

        /**
         * @brief ファイルを閉じる
         */
        virtual void Close() = 0;

        /**
         * @brief ファイルが開いているかどうか
         * @return 開いている場合true
         */
        virtual bool IsOpen() const = 0;

        /**
         * @brief データを読み取る
         * @param buffer 読み取り先バッファ
         * @param size 読み取りサイズ
         * @return 実際に読み取ったバイト数
         */
        virtual size_t Read(void *buffer, size_t size) = 0;

        /**
         * @brief データを書き込む
         * @param buffer 書き込み元バッファ
         * @param size 書き込みサイズ
         * @return 実際に書き込んだバイト数
         */
        virtual size_t Write(const void *buffer, size_t size) = 0;

        /**
         * @brief ファイルポジションを設定
         * @param offset オフセット
         * @param origin 基準位置
         * @return 新しいファイルポジション
         */
        virtual int64_t Seek(int64_t offset, SeekOrigin origin) = 0;

        /**
         * @brief 現在のファイルポジションを取得
         * @return 現在のファイルポジション
         */
        virtual int64_t Tell() const = 0;

        /**
         * @brief ファイルサイズを取得
         * @return ファイルサイズ
         */
        virtual int64_t GetSize() const = 0;

        /**
         * @brief ファイルの終端に到達しているかどうか
         * @return 終端に到達している場合true
         */
        virtual bool IsEOF() const = 0;

        /**
         * @brief バッファをフラッシュする
         */
        virtual void Flush() = 0;

        /**
         * @brief 文字列を読み取る
         * @return 読み取った文字列
         */
        virtual String ReadString() = 0;

        /**
         * @brief 1行を読み取る
         * @return 読み取った行
         */
        virtual String ReadLine() = 0;

        /**
         * @brief 文字列を書き込む
         * @param str 書き込む文字列
         * @return 書き込んだバイト数
         */
        virtual size_t WriteString(const String &str) = 0;

        /**
         * @brief 1行を書き込む（改行文字付き）
         * @param line 書き込む行
         * @return 書き込んだバイト数
         */
        virtual size_t WriteLine(const String &line) = 0;
    };

} // namespace NorvesLib::FileStream