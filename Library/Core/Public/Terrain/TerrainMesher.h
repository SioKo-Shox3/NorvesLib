#pragma once
#include "Rendering/ProceduralMeshGenerator.h"
#include "Terrain/HeightField.h"
namespace NorvesLib::Core::Terrain
{
    // 同じ格子から表示LODを作る。step=1が物理と同じ面、粗いLODの縁はskirtで隙間を隠せる。
    bool BuildTerrainMesh(const HeightField&, Container::VariableArray<Rendering::Mesh3DVertex>& vertices,
                          Container::VariableArray<uint32_t>& indices, uint32_t step = 1, float uvScale = .1f,
                          float skirtDepth = 0);
} // namespace NorvesLib::Core::Terrain
