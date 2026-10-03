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
     * @brief 視線から外れた点で透視投影が横のずれを伸ばす倍率 1/cos²α の、LOD球の描かれる点での上限
     *
     * 視線に垂直な長さ w のずれは、カメラからの距離 d・視線と前方の軸のなす角 α の点で、
     * 画面で最大 projectionFactor·w/(d·cos²α) になる（角度の変化 w/d に、像面の伸び 1/cos²α が掛かる）。
     * 描かれる点は視錐台の中なので tan²α ≤ tan²(fovX/2) + tan²(fovY/2)。球が前方にあれば、
     * α は中心の方向の角 β と見かけの半径 asin(R/D) の和以下でもあるので、小さい方を使う。
     *
     * @param centerDistance カメラからLOD球の中心までの距離（ワールド）
     * @param centerDepth LOD球の中心の前方の軸に沿った深さ（クリップ座標の w。カメラの後ろなら0以下）
     * @param radius LOD球の半径（ワールド）
     * @param tanHalfFovX 横の画角の半分の正接（1/|projection[0][0]|）
     * @param tanHalfFovY 縦の画角の半分の正接（1/|projection[1][1]|）
     */
    inline float ComputeLODSpherePerspectiveStretch(float centerDistance,
                                                    float centerDepth,
                                                    float radius,
                                                    float tanHalfFovX,
                                                    float tanHalfFovY)
    {
        float stretch = 1.0f + tanHalfFovX * tanHalfFovX + tanHalfFovY * tanHalfFovY;
        if (centerDistance > radius && centerDepth > 0.0f)
        {
            const float cosBeta = std::min(centerDepth / centerDistance, 1.0f);
            const float sinBeta = std::sqrt(std::max(1.0f - cosBeta * cosBeta, 0.0f));
            const float sinGamma = radius / centerDistance;
            const float cosGamma = std::sqrt(std::max(1.0f - sinGamma * sinGamma, 0.0f));
            const float cosAlpha = cosBeta * cosGamma - sinBeta * sinGamma; // cos(β + γ)
            if (cosAlpha > 0.0f)
            {
                stretch = std::min(stretch, 1.0f / (cosAlpha * cosAlpha));
            }
        }
        return stretch;
    }

    /**
     * @brief LOD球の表面の法線方向のずれ1 mが、透視投影で画面の何画素になるかの上限
     *
     * 法線方向のずれ δ のうち画面を動かすのは視線に垂直な成分 δ·sinθ（θ は視線と法線のなす角）で、
     * 画面では projectionFactor·δ·sinθ/(d·cos²α) になる。中心までの距離 D・半径 R の球の見える面で
     * sinθ/d の最大は D/(D²−R²)（cos(中心角) = 2RD/(R²+D²) の点）、1/cos²α の上限は
     * ComputeLODSpherePerspectiveStretch なので、その積を上限とする（変位の前後の点をクリップ座標から
     * 画素へ直した差が、これを超えない）。カメラが球の中や表面のすぐ近くなら非常に大きな値になり、
     * 最も細かい段が選ばれる。cluster_cull.comp の ShouldDrawCluster と同じ式。
     *
     * @param centerDistance カメラからLOD球の中心までの距離（ワールド）
     * @param centerDepth LOD球の中心の前方の軸に沿った深さ（クリップ座標の w）
     * @param radius LOD球の半径（ワールド）
     * @param projectionFactor screenHeight / (2·tan(fovY/2))
     * @param tanHalfFovX 横の画角の半分の正接
     * @param tanHalfFovY 縦の画角の半分の正接
     */
    inline float ComputeLODSphereErrorPixelsPerMeter(float centerDistance,
                                                     float centerDepth,
                                                     float radius,
                                                     float projectionFactor,
                                                     float tanHalfFovX,
                                                     float tanHalfFovY)
    {
        const float denominator = std::max(centerDistance * centerDistance - radius * radius, 1.0e-6f);
        const float angularScale = projectionFactor * std::max(centerDistance, 0.0f) / denominator;
        return angularScale *
               ComputeLODSpherePerspectiveStretch(centerDistance, centerDepth, radius, tanHalfFovX, tanHalfFovY);
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
