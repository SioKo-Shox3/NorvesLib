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

        [[nodiscard]] static Container::AnsiString GetCompiledDefaultAssetRoot();

    private:
        Container::AnsiString m_DefaultAssetRoot;
    };
}
