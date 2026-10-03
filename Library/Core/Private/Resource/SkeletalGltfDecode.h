#pragma once

#include "Container/String.h"
#include "Container/Span.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Gltf
{
    class BufferSet;
}

namespace NorvesLib::Core::Skeletal
{
    // GLB/JSONの元bytes入口。outSourceBuffersはhash用の全sourceと借用BINを保持する。
    // GLB入力はoutSourceBuffersより長く保持し、その所有storageを入力に使わないこと。
    // 失敗時はoutSourceBuffersを空にする。decodedの頂点/関節/clipは独立所有する。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeSkeletalGltf(Container::Span<const uint8_t> sourceBytes,
        const Container::String& sourcePath, Gltf::BufferSet* outSourceBuffers = nullptr);

    // 旧String入口の互換用。出力buffer配列を求めた場合は全source bytesを所有コピーする。
    using SkeletalGltfSourceBuffers = Container::VariableArray<Container::VariableArray<uint8_t>>;

    [[nodiscard]] SkeletalGltfDecodeResult DecodeSkeletalGltf(const Container::String& jsonText,
                                                              const Container::String& sourcePath,
                                                              SkeletalGltfSourceBuffers* outSourceBuffers = nullptr);
} // namespace NorvesLib::Core::Skeletal
