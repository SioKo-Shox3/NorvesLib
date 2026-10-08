#pragma once
// GLBの借用元sourceはcallerが保持する。JsonValueは保持せず、移動後にGetRootを取り直す。
#include "Resource/GltfContainer.h"
#include "Resource/GltfBufferSet.h"
#include "Text/JsonDocument.h"
#include <filesystem>
namespace NorvesLib::Core::Skeletal
{
    struct RigGltfImportCapture
    {
        JsonDocument Document;
        Gltf::ContainerView SourceContainer;
        Gltf::BufferSet Buffers;
        // 成功read時のcanonical locatorをbuffer indexで保持。非external要素は空。
        Container::VariableArray<std::filesystem::path> SourceCanonicalFiles;
        Container::VariableArray<uint64_t> SlotSourceMaterialIndices;
    };
} // namespace NorvesLib::Core::Skeletal
