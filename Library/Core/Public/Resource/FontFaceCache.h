#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetFileReader.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core
{
    /**
     * @brief フォントのデータを論理パスごとに共有する。
     * 1つのキャッシュは、同じルートを使う1つの読み手に対して使用する。
     */
    class FontFaceCache final
    {
    public:
        /**
         * @brief 論理パスのブロブを取得し、読込に成功した結果を共有する。
         * 保持中の同じ論理パスは再読込しない。失敗は記憶せず次回に読み直す。スレッド安全。
         */
        Asset::AssetBlob Acquire(Container::AnsiStringView logicalPath, const Asset::AssetFileReader& reader);
        /** @brief キャッシュに保持しているブロブをすべて外す。 */
        void Clear();
        /** @brief キャッシュに保持している論理パスの数を返す。 */
        size_t GetCachedCount() const;

    private:
        struct Entry
        {
            Container::AnsiString LogicalPath;
            Asset::AssetBlob Blob;
        };

        mutable Thread::Mutex m_Mutex;
        Container::VariableArray<Entry> m_Entries;
    };
} // namespace NorvesLib::Core
