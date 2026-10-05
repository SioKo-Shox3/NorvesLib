#pragma once

#include "Rendering/MeshIndexChunks.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
}

namespace NorvesLib::Core::Rendering
{
    struct ProceduralMeshGPUData
    {
        Container::TSharedPtr<RHI::IBuffer> VertexBuffer;
        Container::TSharedPtr<RHI::IBuffer> IndexBuffer;
        uint32_t IndexCount = 0;
        Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS> SubMeshes;
        uint32_t SubMeshCount = 0;
        // インデックスを128三角形以下に分けた塊（サブメッシュの境目で区切る。全三角形をちょうど1回ずつ覆う）
        Container::VariableArray<MeshIndexChunk> Chunks;
        // 登録した頂点の位置から求めたローカル空間のAABB（Mesh3DVertex の並びで登録されたときだけ有効）
        BoundingBox LocalBounds;
        bool bHasLocalBounds = false;
    };

} // namespace NorvesLib::Core::Rendering
