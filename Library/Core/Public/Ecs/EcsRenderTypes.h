#pragma once

/**
 * @file EcsRenderTypes.h
 * @brief Mesh・MegaGeometry・SkinnedMesh の Component の行に置く、描画の値の列
 *
 * 値は Object の参照でないもの（資源の handle と bool・enum）だけ。Component の行が World の木にいる間は列が正で、
 * 行が無い間は Component の予備の値が正になる。
 */

#include "Ecs/EcsTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SkinnedMeshTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    /// MeshComponent の行の描画の値（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsMeshRender
    {
        Rendering::MeshDataHandle MeshHandle;
        bool bVisible;
        bool bCastShadow;
        bool bReceiveShadow;
        bool Reserved;
        Rendering::RenderLayer Layer;
    };

    /// MegaGeometryComponent の行の MegaMesh の handle（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsMegaMesh
    {
        Rendering::MegaGeometry::MegaMeshHandle Handle;
    };

    /// SkinnedMeshComponent の行の描画の値。メッシュの handle・可視・影を落とすはプロパティ（ECS_PROPERTY で読み書きする）。
    /// 材質の handle はスロットごとの上書きと一緒に扱うので、プロパティにはせず Component が読み書きする
    struct EcsSkinnedRender
    {
        Rendering::SkinnedMeshHandle MeshHandle;
        Rendering::MaterialHandle Material;
        bool bVisible;
        bool bCastShadow;
        bool Reserved0;
        bool Reserved1;
    };
}
