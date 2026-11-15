// CSM と点光源のキューブで共有する MegaGeometry の投影物の選別の純関数。
#pragma once

#include "Container/String.h"
#include "RHI/RHITypes.h"
#include "Rendering/RenderTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace MegaGeometry
    {
        struct MegaMeshGPUData;
    }

    // MegaGeometryの影のキャスター（影に使う段の範囲を1回で描く。既定はLOD0）。CSMと点光源の影の両方へ描き、
    // CSMの深度範囲にもこの境界球を含める（描く物と深度範囲に含める物を一致させる）。
    // 粗い段を1回の範囲で持つメッシュは、影の地図のテクセルに見合う段を描くごとに選ぶ（ResolveMegaShadowRange）。
    struct MegaShadowCaster
    {
        RHI::BufferPtr VertexBuffer;
        RHI::BufferPtr IndexBuffer;
        // ジオメトリのプールの区画の、頂点・インデックスの先頭（塊のバッファの先頭からのバイト）
        uint64_t VertexBufferOffsetBytes = 0;
        uint64_t IndexBufferOffsetBytes = 0;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        float World[16] = {};
        BoundingSphere Bounds;
        const MegaGeometry::MegaMeshGPUData* GpuData = nullptr;
        uint64_t MeshId = 0;
        // ローカルの長さ（LODの誤差）をワールドへ直す倍率（行列の最大の軸の伸び）
        float WorldScale = 1.0f;
    };

    // MegaGeometryの段の誤差が、影の地図の何テクセル以下なら粗い段を使うか
    constexpr float MegaShadowLODErrorTexels = 1.0f;

    // 1回の描画の段と範囲
    struct MegaShadowRange
    {
        uint32_t Level = 0;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
    };

    // 影の地図の1テクセルのワールドでの大きさに見合う段を選ぶ（影に指定された段より細かくはしない）。
    MegaShadowRange ResolveMegaShadowRange(const MegaShadowCaster& caster, float texelSize);

    // 記録用に「メッシュ名:段/三角形数」を足す
    void AppendMegaShadowLevel(Container::String& out, const MegaShadowCaster& caster, const MegaShadowRange& range);

    // ローカルの境界球をワールド行列（行ベクトル規約、並進は行3）で写す。半径は最大の軸の伸びで広げる。
    BoundingSphere TransformMegaBounds(const BoundingSphere& local, const float* world);

    // ワールドの境界球が描ける大きさか（有限で半径が正）
    bool IsDrawableMegaBounds(const BoundingSphere& bounds);

    // 影に使う頂点・インデックスのバッファと段を持つか
    bool IsMegaShadowMeshUsable(const MegaGeometry::MegaMeshGPUData* gpuData);

    // 投影物 1 つを作る。影を落とさない・影に使う段が無い・境界が描けない物は false を返して外す
    bool TryMakeMegaShadowCaster(const float* world /*16 要素、行ベクトル規約*/, bool bCastShadow,
                                const MegaGeometry::MegaMeshGPUData* gpuData, uint64_t meshId,
                                MegaShadowCaster& outCaster);
} // namespace NorvesLib::Core::Rendering
