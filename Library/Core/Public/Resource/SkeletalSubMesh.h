#pragma once

#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    // indexは連結済み頂点の絶対番号。描画のbaseVertexは0とし、二重加算しない。
    // これは所有データの範囲記述であり、wireへ構造体のままコピーしない。
    struct SkeletalSubMesh
    {
        uint32_t IndexStart = 0;
        uint32_t IndexCount = 0;
        uint32_t MaterialSlot = 0;
    };
} // namespace NorvesLib::Core::Skeletal
