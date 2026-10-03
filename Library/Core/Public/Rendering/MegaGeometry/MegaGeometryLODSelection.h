#pragma once

// MegaGeometryのLODの段を、失われる形の誤差を画面・影の地図へ投影した大きさで選ぶ計算。
// GBufferの段はGPU（cluster_cull.comp）が選ぶが、メッシュ共通のLOD球を持つメッシュは全クラスタが
// 同じ段を選ぶので、同じ式でCPUからも段を求めて記録に使う。影の段はCPUがここで選ぶ。

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief LOD球の見える面での、法線方向のずれ1 mが画面で何画素になるかの最大
     *
     * 球の表面の点の法線方向のずれ δ は、視線と法線のなす角を θ、カメラからの距離を d として
     * 画面で δ·sinθ·projectionFactor/d になる。中心までの距離 D・半径 R の球の見える面で、
     * sinθ/d の最大は D/(D²−R²)（cos(中心角) = 2RD/(R²+D²) の点。最も近い点では θ=0 でずれが
     * 視線に沿い、輪郭では最も遠い）。カメラが球の中や表面のすぐ近くなら非常に大きな値になり、
     * 最も細かい段が選ばれる。cluster_cull.comp の ShouldDrawCluster と同じ式。
     *
     * @param centerDistance カメラからLOD球の中心までの距離（ワールド）
     * @param radius LOD球の半径（ワールド）
     * @param projectionFactor screenHeight / (2·tan(fovY/2))
     */
    inline float ComputeLODSphereErrorPixelsPerMeter(float centerDistance, float radius, float projectionFactor)
    {
        const float denominator = std::max(centerDistance * centerDistance - radius * radius, 1.0e-6f);
        return projectionFactor * std::max(centerDistance, 0.0f) / denominator;
    }

    /**
     * @brief 誤差を投影した大きさが閾値以下になる最も粗い段（無ければ0）
     *
     * 段の誤差は粗いほど大きい（単調）前提。cluster_cull.comp の親子の判定（自分の誤差が閾値以下で、
     * 親の誤差が閾値を超える段を描く）が全クラスタ同じ距離で選ぶときに選ぶ段と一致する。
     */
    inline uint32_t SelectCoarsestLODWithinError(const VariableArray<MegaMeshLevelRange> &levels,
                                                 float errorScale,
                                                 float threshold)
    {
        uint32_t selected = 0;
        for (uint32_t level = 1; level < static_cast<uint32_t>(levels.size()); ++level)
        {
            if (levels[level].Error * errorScale <= threshold)
            {
                selected = level;
            }
            else
            {
                break;
            }
        }
        return selected;
    }

    /**
     * @brief 影の地図へ描く段を選ぶ
     *
     * メッシュが影に指定した段（ShadowLODLevel）を最も細かい段とし、それより粗い段のうち、
     * 誤差（ワールドの長さ）が影の地図の1テクセルの thresholdTexels 倍以下で、1回の範囲で描ける
     * 最も粗い段を選ぶ。遠いカスケード・光源から遠い物ほどテクセルが大きく、粗い段になる。
     *
     * @param worldErrorScale ローカルの誤差をワールドの長さへ直す倍率（行列の最大の軸の伸び）
     * @param texelSize 影の地図の1テクセルのワールドでの大きさ（m）
     */
    inline uint32_t SelectShadowLODLevel(const MegaMeshGPUData &gpuData,
                                         float worldErrorScale,
                                         float texelSize,
                                         float thresholdTexels)
    {
        const uint32_t levelCount = static_cast<uint32_t>(gpuData.LevelRanges.size());
        uint32_t selected = gpuData.ShadowLODLevel;
        if (selected >= levelCount || !(texelSize > 0.0f) || !std::isfinite(texelSize))
        {
            return selected;
        }
        for (uint32_t level = selected + 1u; level < levelCount; ++level)
        {
            const MegaMeshLevelRange &range = gpuData.LevelRanges[level];
            if (range.Error * worldErrorScale > texelSize * thresholdTexels)
            {
                break;
            }
            if (range.IndexCount > 0u)
            {
                selected = level;
            }
        }
        return selected;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
