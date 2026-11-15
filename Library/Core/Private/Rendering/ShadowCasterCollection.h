// CSM と点光源のキューブで共有する MegaGeometry の投影物の選別と警告の記録。
#pragma once

#include "Container/String.h"
#include "Container/UnorderedSet.h"
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
    // CSMの深度範囲には上限を超える物の境界球も残す。粗いカスケードで上限内の段を描くとき、near面で切れるのを防ぐ。
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

    // 三角形数が上限を超えるか（上限 0 は上限なし）
    bool ExceedsShadowCasterTriangleBudget(uint32_t triangleCount, uint32_t budget);

    // 影に使う段の三角形数が上限を超えるメッシュか（gpuData が null なら false）
    bool IsMegaShadowMeshOverTriangleBudget(const MegaGeometry::MegaMeshGPUData* gpuData, uint32_t budget);

    // 上限を超えたメッシュを、MeshId ごとに 1 回だけ警告するための記録
    class ShadowCasterBudgetWarnings
    {
    public:
        // 初めてのメッシュなら記録して true（ログ無効時は記録せず false）
        bool MarkFirstWarning(uint64_t meshId);

    private:
        Container::UnorderedSet<uint64_t> m_WarnedMeshIds;
    };

    // 描く段の三角形数が上限内なら true。超えたら false を返し、メッシュごとに 1 回だけ警告する。
    bool AdmitMegaShadowCasterDraw(const MegaShadowCaster& caster, const MegaShadowRange& range, uint32_t budget,
                                  ShadowCasterBudgetWarnings& warnings);

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
