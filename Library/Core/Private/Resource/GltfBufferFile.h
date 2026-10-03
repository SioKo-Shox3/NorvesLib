#pragma once

#include "Resource/GltfBufferSet.h"
#include <filesystem>

namespace NorvesLib::Core::Gltf
{
    // filesystem境界用のprivate context。Resolve中にSourceFileを変更しないこと。
    struct BufferFileContext
    {
        std::filesystem::path SourceFile;
    };
    // BufferSetの標準reader。相対URIとcanonical component境界を検証して全fileを読む。
    // contextはBufferFileContext、URIはbytesの所有storageを参照しないこと。
    [[nodiscard]] ExternalBufferReadResult ReadBufferFile(Container::Span<const uint8_t> decodedUri,
        Container::VariableArray<uint8_t>& bytes, void* context);
}
