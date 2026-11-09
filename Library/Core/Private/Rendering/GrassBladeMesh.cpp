#include "Rendering/GrassBladeMesh.h"

namespace NorvesLib::Core::Rendering
{
    void BuildGrassBladeIndices(const uint32_t (&lodSegments)[3],
        Container::VariableArray<uint32_t>& outIndices, uint32_t (&outFirstIndex)[3])
    {
        outIndices.clear();
        const size_t indexCount = (static_cast<size_t>(lodSegments[0]) + lodSegments[1] + lodSegments[2]) * 6u;
        outIndices.reserve(indexCount);
        uint32_t firstIndex = 0;
        for (uint32_t lod = 0; lod < 3; ++lod)
        {
            outFirstIndex[lod] = firstIndex;
            const uint32_t lodBits = lod << 16;
            for (uint32_t row = 0; row < lodSegments[lod]; ++row)
            {
                const uint32_t left = lodBits | (row << 1);
                const uint32_t right = left | 1u;
                const uint32_t nextLeft = lodBits | ((row + 1u) << 1);
                const uint32_t nextRight = nextLeft | 1u;
                // 隣り合う左右の行を、同じ巻き順の 2 三角形で結ぶ。
                outIndices.push_back(left);
                outIndices.push_back(right);
                outIndices.push_back(nextLeft);
                outIndices.push_back(right);
                outIndices.push_back(nextRight);
                outIndices.push_back(nextLeft);
            }
            firstIndex += lodSegments[lod] * 6u;
        }
    }
} // namespace NorvesLib::Core::Rendering
