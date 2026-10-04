#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    // meshoptimizer(Library/ThirdParty/meshoptimizer)の簡略化を、クッカーの内側に閉じ込める薄い境界。
    // 頂点バッファは書き換えず、簡略化後の三角形を元の頂点番号で返す(固定した頂点の位置は動かない)。
    struct CookSimplifyParams
    {
        // 残したい索引数の目安(3 の倍数)。誤差の上限に達したらそれより多く残る。
        size_t TargetIndexCount = 0;
        // 許す誤差の上限。メッシュの大きさに対する相対値(1.0 でメッシュ全体の大きさ)。
        float TargetErrorRelative = 1.0f;
        // 頂点ごとの固定フラグ(0 でない頂点は消さない)。空なら固定しない。大きさは頂点数と同じ。
        Core::Container::VariableArray<uint8_t> VertexLock;
    };

    struct CookSimplifyResult
    {
        Core::Container::VariableArray<uint32_t> Indices;
        // 結果の誤差。相対値はメッシュの大きさに対する比、絶対値は頂点座標と同じ単位。
        float ErrorRelative = 0.0f;
        float ErrorAbsolute = 0.0f;
    };

    // positions は float3 が連なる配列(stride はバイト数。12 以上、4 の倍数)。
    [[nodiscard]] bool SimplifyMeshTriangles(const float* positions,
                                             size_t vertexCount,
                                             size_t positionStrideBytes,
                                             const uint32_t* indices,
                                             size_t indexCount,
                                             const CookSimplifyParams& params,
                                             CookSimplifyResult& outResult,
                                             Core::Container::AnsiString& error);

    // 三角形の辺のうち、位置が同じ頂点をひとつに数えたうえで 1 枚の三角形にしか属さないものの両端を 1 にした
    // フラグ(頂点数と同じ大きさ)を返す。開いたメッシュの縁を簡略化で動かさないための固定に使う。
    [[nodiscard]] Core::Container::VariableArray<uint8_t> FindBoundaryVertices(const float* positions,
                                                                               size_t vertexCount,
                                                                               size_t positionStrideBytes,
                                                                               const uint32_t* indices,
                                                                               size_t indexCount);
}
