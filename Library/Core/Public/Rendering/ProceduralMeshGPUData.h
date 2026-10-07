#pragma once

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
        // 登録した頂点の位置から求めたローカル空間のAABB（Mesh3DVertex の並びで登録されたときだけ有効）
        BoundingBox LocalBounds;
        bool bHasLocalBounds = false;
        // インデックスを 384 個（128 三角形）ごとに、メッシュの先頭から整列して区切ったブロックのローカル空間の AABB。
        // VSM が塊ごとの投影物の境界に使う。頂点の基点が 0 の描画でだけ引ける。求められなかったときは空
        Container::VariableArray<BoundingBox> BlockBounds;
    };

} // namespace NorvesLib::Core::Rendering
