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
        // 0は頂点範囲未指定。絶対indexのためVertexOffsetは常に0。
        uint32_t VertexCount = 0;
        bool bNoShadow = false;
        float BoundsCenter[3]{};
        float BoundsRadius = 0.0f;
    };
} // namespace NorvesLib::Core::Skeletal
