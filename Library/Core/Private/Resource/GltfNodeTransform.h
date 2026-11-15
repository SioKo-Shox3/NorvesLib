#pragma once

#include "Resource/ImportTransform.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class JsonValue;
}

namespace NorvesLib::Core::Gltf
{
    enum class NodeTransformResult : uint8_t
    {
        Success,               // "success": 変換の解決または焼き込みに成功
        MultipleMeshNodes,     // "mesh_node_multiple": 既定の scene で対象 mesh を参照する node が複数
        SkinnedMeshNode,       // "mesh_node_skinned": 対象 mesh の node に skin がある
        NonFiniteTransform,    // "node_transform_non_finite": 変換または行列式が非有限
        SingularTransform,     // "node_transform_singular": 行列式が 0 または逆転置が非有限
        InvalidNode,           // "node_invalid": node・scene・変換の形式や参照が不正
        NonFiniteBakedVertex,  // "baked_vertex_non_finite": 頂点を有限な float で表せない、または法線を正規化できない
        InvalidLayout,         // "vertex_layout_invalid": 頂点の配置または添字が不正
    };

    [[nodiscard]] const char* NodeTransformResultName(NodeTransformResult result) noexcept;

    struct MeshNodeTransform
    {
        NodeTransformResult Result = NodeTransformResult::InvalidNode;
        bool bIdentity = true;
        uint32_t NodeIndex = UINT32_MAX;
        // glTFと同じ列優先で、根からmesh nodeまで親を左に掛けた積。
        double Matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        double Determinant = 1.0;
    };

    // 既定sceneから対象meshを参照するnodeと、その祖先の変換を解決する。
    [[nodiscard]] MeshNodeTransform ResolveMeshNodeTransform(const JsonValue& root, uint32_t meshIndex);

    // 頂点と添字を追加確保なしで変換する。失敗時はどちらのspanも変更しない。
    // 頂点・添字の領域は互いにも transform・layout とも重ねず、呼び出し中は外部から変更しない。
    // 保持する属性は位置・法線と重ならない配置にする。
    [[nodiscard]] NodeTransformResult BakeMeshNodeTransform(const MeshNodeTransform& transform,
        Container::Span<uint8_t> vertices, size_t vertexCount, const AssetImport::ImportVertexLayout& layout,
        Container::Span<uint32_t> indices) noexcept;
} // namespace NorvesLib::Core::Gltf
