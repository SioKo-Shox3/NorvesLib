#pragma once

#include "Container/String.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Skeletal
{
    using SkeletalGltfSourceBuffers = Container::VariableArray<Container::VariableArray<uint8_t>>;

    [[nodiscard]] SkeletalGltfDecodeResult DecodeSkeletalGltf(const Container::String& jsonText,
                                                              const Container::String& sourcePath,
                                                              SkeletalGltfSourceBuffers* outSourceBuffers = nullptr);
} // namespace NorvesLib::Core::Skeletal
