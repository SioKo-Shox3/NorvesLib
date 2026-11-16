// スキンメッシュの LOD 簡略化に必要な前処理と部位分割を行う。
// GR80 の修復と G11 の影専用メッシュでも利用できる入力と出力を持つ。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Resource/SkeletalGltfData.h"

#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    // サブメッシュが占める三角形索引の範囲。
    struct SkinnedLodSubMeshRange
    {
        uint32_t IndexStart = 0;
        uint32_t IndexCount = 0;
    };

    // スキンメッシュと、頂点と同じ空間にある関節のバインド位置。
    struct SkinnedLodInput
    {
        Core::Container::Span<const Core::Skeletal::SkeletalVertex> Vertices;
        Core::Container::Span<const uint32_t> Indices; // 三角形ごとに 3 個。
        // 空の場合は全索引を一つとし、指定する場合は順にすき間なく全索引を覆う。
        Core::Container::Span<const SkinnedLodSubMeshRange> SubMeshes;
        uint32_t JointCount = 0; // 関節番号はこの値未満。
        Core::Container::Span<const float> JointBindPositions; // 関節ごとの XYZ、要素数は 3×JointCount。
    };

    // 部位数と関節数の目標、および後続の簡略化に使う設定。
    struct SkinnedLodParams
    {
        uint32_t MinParts = 16; // 部位数の目標下限。
        uint32_t MaxParts = 64; // 部位数の目標上限。
        uint32_t MaxPartJoints = 16; // 一部位に影響する関節数の上限。
        Core::Container::VariableArray<float> Targets{1.0f, 0.4f, 0.12f, 0.03f}; // LOD ごとの三角形数の比。
        float ThetaRef = 0.5f; // スキン変位の誤差に使う基準角度、単位は rad。
    };

    // 元の頂点を保ちながら、重みと位相と三角形を整理した結果。
    struct SkinnedLodPreprocessed
    {
        // 元の関節番号スロットに対応する 4 本の正規化済み重み。合計 0 の頂点は 0 のまま。
        // 同じ関節の重みは最初のスロットに合算し、残りのスロットは 0 にする。
        Core::Container::VariableArray<float> Weights;
        Core::Container::VariableArray<uint32_t> WeldedVertex; // 同位置の最小頂点番号。
        Core::Container::VariableArray<uint32_t> Indices; // 退化・重複を除いた三角形。
        Core::Container::VariableArray<SkinnedLodSubMeshRange> SubMeshes; // 整理後のサブメッシュ範囲。
        uint32_t ZeroWeightVertexCount = 0;
        uint32_t DegenerateTriangleCount = 0;
        uint32_t DuplicateTriangleCount = 0;
    };

    // 一部位に含まれるサブメッシュ、三角形数、昇順の関節番号。
    struct SkinnedLodPartInfo
    {
        uint32_t SubMeshIndex = 0;
        uint32_t TriangleCount = 0;
        Core::Container::VariableArray<uint32_t> Joints; // 重みが正の関節番号。MaxPartJoints 以下。
    };

    // 三角形の部位と、簡略化時に固定する部位間の境界頂点。
    struct SkinnedLodPartition
    {
        Core::Container::VariableArray<uint32_t> TrianglePart; // 前処理後の三角形ごとの部位番号。
        // サブメッシュ番号、次に部位内の最小三角形番号の順。
        Core::Container::VariableArray<SkinnedLodPartInfo> Parts;
        // 同じ位置を複数部位が参照するとき 1。CookSimplifyParams::VertexLock にそのまま渡せる。
        Core::Container::VariableArray<uint8_t> BoundaryVertex;
    };

    // 入力を検査し、位置の溶接、重みの正規化、退化と重複の除去を行う。
    [[nodiscard]] bool PreprocessSkinnedMesh(const SkinnedLodInput& input,
                                             SkinnedLodPreprocessed& out,
                                             Core::Container::AnsiString& error);

    // 三角形を連結した部位に分ける。部位数は MinParts〜MaxParts を目標とするが、
    // 小さいメッシュでは MinParts に届かず、併合できない場合は MaxParts を超えても成功する。
    [[nodiscard]] bool PartitionSkinnedMesh(const SkinnedLodInput& input,
                                            const SkinnedLodPreprocessed& preprocessed,
                                            const SkinnedLodParams& params,
                                            SkinnedLodPartition& out,
                                            Core::Container::AnsiString& error);
}
