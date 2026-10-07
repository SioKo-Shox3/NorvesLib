#pragma once

#include "Asset/AssetReadRequest.h"
#include "Container/StringView.h"

namespace NorvesLib::Core::Asset
{
    class AssetFileReader
    {
    public:
        AssetFileReader();
        explicit AssetFileReader(const Container::AnsiString &defaultAssetRoot);

        [[nodiscard]] AssetReadResult Read(const AssetReadRequest &request) const;
        [[nodiscard]] AssetReadResult Read(Container::AnsiStringView inputPath) const;

        /**
         * @brief ファイルの offset から size バイトだけを読む（全体は読まない）。
         *
         * 結果の Blob は読んだ範囲だけを持ち、FileSize はファイル全体の大きさ。範囲がファイルの外、または size が 0 の
         * ときは失敗する（ReadFailed / InvalidRequest）。
         */
        [[nodiscard]] AssetReadResult ReadRange(const AssetReadRequest &request, uint64_t offset, size_t size) const;

        [[nodiscard]] static Container::AnsiString GetCompiledDefaultAssetRoot();

    private:
        struct RangeSpec
        {
            uint64_t Offset = 0;
            size_t Size = 0;
        };

        [[nodiscard]] AssetReadResult ReadInternal(const AssetReadRequest &request, const RangeSpec *range) const;

        Container::AnsiString m_DefaultAssetRoot;
    };
}
