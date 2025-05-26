#pragma once

#include "IFileStream.h"
#include "FileStream.h"
#include "Thread/Public/JobSystem.h"
#include "Thread/Public/Task.h"
#include "Thread/Public/Mutex.h"
#include <functional>
#include <future>

namespace NorvesLib::FileStream
{

    /**
     * @brief 非同期ファイルストリームクラス
     * マルチスレッドを利用した非同期ファイル操作を提供します
     */
    class AsyncFileStream
    {
    public:
        AsyncFileStream() = default;
        ~AsyncFileStream();

        // コピー・ムーブ禁止
        AsyncFileStream(const AsyncFileStream&) = delete;
        AsyncFileStream& operator=(const AsyncFileStream&) = delete;
        AsyncFileStream(AsyncFileStream&&) = delete;
        AsyncFileStream& operator=(AsyncFileStream&&) = delete;

        /**
         * @brief 非同期ファイルストリームを作成
         * @param filePath ファイルパス
         * @param mode ファイルモード
         * @param access アクセス権
         * @param share 共有モード
         * @return 非同期ファイルストリームのスマートポインタ
         */
        static AsyncFileStreamPtr Create(const String& filePath, FileMode mode, 
                                       FileAccess access = FileAccess::ReadWrite, 
                                       FileShare share = FileShare::Read);

        /**
         * @brief 非同期ファイルストリームを作成（ユニークポインタ版）
         */
        static AsyncFileStreamUniquePtr CreateUnique(const String& filePath, FileMode mode,
                                                   FileAccess access = FileAccess::ReadWrite,
                                                   FileShare share = FileShare::Read);

        /**
         * @brief ファイルを開く
         * @param filePath ファイルパス
         * @param mode ファイルモード
         * @param access アクセス権
         * @param share 共有モード
         * @return 成功した場合true
         */
        bool Open(const String& filePath, FileMode mode, 
                 FileAccess access = FileAccess::ReadWrite, 
                 FileShare share = FileShare::Read);

        /**
         * @brief ファイルを閉じる
         */
        void Close();

        /**
         * @brief ファイルが開いているかどうか
         * @return 開いている場合true
         */
        bool IsOpen() const;

        /**
         * @brief 非同期でデータを読み取る
         * @param buffer 読み取り先バッファ
         * @param size 読み取りサイズ
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncReadTask ReadAsync(void* buffer, size_t size, 
                               std::function<void(const AsyncReadResult&)> callback = nullptr);

        /**
         * @brief 非同期でデータを書き込む
         * @param buffer 書き込み元バッファ
         * @param size 書き込みサイズ
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncWriteTask WriteAsync(const void* buffer, size_t size,
                                 std::function<void(const AsyncWriteResult&)> callback = nullptr);

        /**
         * @brief 非同期で文字列を読み取る
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncReadTask ReadStringAsync(std::function<void(const String&, bool)> callback = nullptr);

        /**
         * @brief 非同期で文字列を書き込む
         * @param str 書き込む文字列
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncWriteTask WriteStringAsync(const String& str,
                                       std::function<void(const AsyncWriteResult&)> callback = nullptr);

        /**
         * @brief 非同期で行を読み取る
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncReadTask ReadLineAsync(std::function<void(const String&, bool)> callback = nullptr);

        /**
         * @brief 非同期で行を書き込む
         * @param line 書き込む行
         * @param callback 完了時に呼び出されるコールバック
         * @return 非同期タスク
         */
        AsyncWriteTask WriteLineAsync(const String& line,
                                     std::function<void(const AsyncWriteResult&)> callback = nullptr);

        /**
         * @brief ファイルサイズを取得
         * @return ファイルサイズ
         */
        int64_t GetSize() const;

        /**
         * @brief 全ての非同期操作の完了を待機
         */
        void WaitForAllOperations();

        /**
         * @brief 進行中の非同期操作をキャンセル
         */
        void CancelAllOperations();

    private:
        FileStreamPtr m_fileStream;
        NorvesLib::Thread::Mutex m_mutex;
        VariableArray<AsyncReadTask> m_activeTasks;
        bool m_bCancelling = false;

        /**
         * @brief アクティブなタスクを追加
         */
        void AddActiveTask(const NorvesLib::Thread::TaskPtr& task);

        /**
         * @brief 完了したタスクを削除
         */
        void RemoveCompletedTasks();
    };

} // namespace NorvesLib::FileStream