#pragma once

#include "Resource/GltfBufferSet.h"
#include <filesystem>

namespace NorvesLib::Core::Gltf
{
    // filesystem境界用のprivate context。Resolve中にSourceFileを変更しないこと。
    struct BufferFileContext
    {
        std::filesystem::path SourceFile;
        // 明示上限を渡したreaderだけが実file全体の累積確保を制限する。既定は従来通り。
        uint64_t MaxReadBytes = UINT64_MAX;
        uint64_t ReadBytes = 0;
        bool bLimitExceeded = false;
    };
    // 同じ解決/readに使ったcanonical pathも成功時だけ返す。bytesの失敗規則は標準readerと同じ。
    [[nodiscard]] ExternalBufferReadResult ReadBufferFileWithPath(Container::Span<const uint8_t> decodedUri,
        Container::VariableArray<uint8_t>& bytes, void* context, std::filesystem::path* outResolvedPath);
    // BufferSetの標準reader。相対URIとcanonical component境界を検証して全fileを読む。
    // contextはBufferFileContext、URIはbytesの所有storageを参照しないこと。
    [[nodiscard]] ExternalBufferReadResult ReadBufferFile(Container::Span<const uint8_t> decodedUri,
        Container::VariableArray<uint8_t>& bytes, void* context);
}
