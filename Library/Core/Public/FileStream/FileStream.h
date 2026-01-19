#pragma once

#include "IFileStream.h"
#include <fstream>

namespace NorvesLib::FileStream
{

    /**
     * @brief ファイルストリームの具象実装クラス
     * 標準ライブラリのfstreamを使用してファイル操作を実装します
     */
    class FileStream : public IFileStream
    {
    public:
        FileStream() = default;
        ~FileStream() override;

        // コピー・ムーブコンストラクタとオペレータ
        FileStream(const FileStream &) = delete;
        FileStream &operator=(const FileStream &) = delete;
        FileStream(FileStream &&other) noexcept;
        FileStream &operator=(FileStream &&other) noexcept;

        /**
         * @brief ファイルストリームを作成
         * @param filePath ファイルパス
         * @param mode ファイルモード
         * @param access アクセス権
         * @param share 共有モード
         * @return ファイルストリームのスマートポインタ
         */
        static FileStreamPtr Create(const String &filePath, FileMode mode, FileAccess access = FileAccess::ReadWrite, FileShare share = FileShare::Read);

        /**
         * @brief ファイルストリームを作成（ユニークポインタ版）
         * @param filePath ファイルパス
         * @param mode ファイルモード
         * @param access アクセス権
         * @param share 共有モード
         * @return ファイルストリームのユニークポインタ
         */
        static FileStreamUniquePtr CreateUnique(const String &filePath, FileMode mode, FileAccess access = FileAccess::ReadWrite, FileShare share = FileShare::Read);

        // IFileStreamインターフェースの実装
        bool Open(const String &filePath, FileMode mode, FileAccess access = FileAccess::ReadWrite, FileShare share = FileShare::Read) override;
        void Close() override;
        bool IsOpen() const override;
        size_t Read(void *buffer, size_t size) override;
        size_t Write(const void *buffer, size_t size) override;
        int64_t Seek(int64_t offset, SeekOrigin origin) override;
        int64_t Tell() const override;
        int64_t GetSize() const override;
        bool IsEOF() const override;
        void Flush() override;
        String ReadString() override;
        String ReadLine() override;
        size_t WriteString(const String &str) override;
        size_t WriteLine(const String &line) override;

    private:
        mutable std::fstream m_stream;
        String m_filePath;
        FileMode m_mode = FileMode::Read;
        FileAccess m_access = FileAccess::Read;
        bool m_bIsOpen = false;

        /**
         * @brief ファイルモードを標準ライブラリ形式に変換
         * @param mode ファイルモード
         * @param access アクセス権
         * @return 標準ライブラリのオープンモード
         */
        std::ios_base::openmode ConvertToStdMode(FileMode mode, FileAccess access) const;
    };

} // namespace NorvesLib::FileStream
