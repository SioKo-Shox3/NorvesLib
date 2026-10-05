#pragma once

// 三角形リストのインデックスを、最大128三角形の連続した塊に分ける。
// ビジビリティバッファの ID は「描画の記録の番号 << 7 | 記録の中の三角形の番号」なので、
// 1つの記録が覆う三角形が128（7bit）以下であることをここで保証する。
// 手続きメッシュとスキニングのメッシュが登録時にこの塊を持ち、計算シェーダーとラスタが同じ範囲を参照する。

#include "Container/Containers.h"

#include <algorithm>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 1つの塊が持つ三角形の最大数（MegaGeometry の MAX_TRIANGLES_PER_CLUSTER と同じ） */
    constexpr uint32_t MESH_CHUNK_MAX_TRIANGLES = 128;

    /**
     * @brief インデックスバッファ内の連続した範囲（128三角形以下）
     */
    struct MeshIndexChunk
    {
        /** @brief 塊の最初のインデックス（インデックスバッファ内の位置。3の倍数） */
        uint32_t FirstIndex = 0;
        /** @brief 塊のインデックス数（3の倍数。MESH_CHUNK_MAX_TRIANGLES * 3 以下） */
        uint32_t IndexCount = 0;
    };

    /**
     * @brief インデックスの範囲 [0, indexCount) を塊に分ける
     *
     * 端の3で割り切れない余りのインデックスは三角形にならないので覆わない。
     * boundaryIndices に与えた位置（サブメッシュの境目など）では必ず塊を区切るので、
     * 1つの塊が複数のサブメッシュ（材質）をまたがない。位置が 0 以下・indexCount 以上のものは区切りにならないので無視する。
     * 範囲の内側にあって3の倍数でない位置は、隣の区間と合わせた塊を作ってしまうので、勝手に合わせず失敗を返す。
     *
     * @param indexCount インデックスの総数
     * @param boundaryIndices 塊を区切る位置（無くてもよい）
     * @param boundaryCount boundaryIndices の数
     * @param outChunks 出力。先頭から順に並び、全三角形をちょうど1回ずつ覆う。失敗したときは空
     * @return 3の倍数でない区切りがあれば false
     */
    [[nodiscard]] inline bool BuildMeshIndexChunks(uint32_t indexCount,
                                                   const uint32_t* boundaryIndices,
                                                   uint32_t boundaryCount,
                                                   Container::VariableArray<MeshIndexChunk>& outChunks)
    {
        outChunks.clear();
        const uint32_t coveredIndexCount = indexCount - indexCount % 3;

        Container::VariableArray<uint32_t> cuts;
        cuts.push_back(0);
        for (uint32_t i = 0; boundaryIndices != nullptr && i < boundaryCount; ++i)
        {
            const uint32_t boundary = boundaryIndices[i];
            if (boundary == 0 || boundary >= indexCount)
            {
                continue;
            }
            if (boundary % 3 != 0)
            {
                return false;
            }
            cuts.push_back(boundary);
        }
        if (coveredIndexCount == 0)
        {
            return true;
        }
        cuts.push_back(coveredIndexCount);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

        constexpr uint32_t maxIndicesPerChunk = MESH_CHUNK_MAX_TRIANGLES * 3;
        for (size_t cutIndex = 0; cutIndex + 1 < cuts.size(); ++cutIndex)
        {
            // 区間の終わりを超えて進めない形にして、4G 付近でも桁あふれで終わらなくならないようにする
            uint32_t first = cuts[cutIndex];
            const uint32_t end = cuts[cutIndex + 1];
            while (first < end)
            {
                MeshIndexChunk chunk;
                chunk.FirstIndex = first;
                chunk.IndexCount = std::min(maxIndicesPerChunk, end - first);
                outChunks.push_back(chunk);
                first += chunk.IndexCount;
            }
        }
        return true;
    }

} // namespace NorvesLib::Core::Rendering
