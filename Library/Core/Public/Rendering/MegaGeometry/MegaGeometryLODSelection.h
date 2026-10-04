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

    /** @brief 焼き込み済みの階層の判定で、球までの距離の下限（m。カメラが球の中・表面なら誤差を非常に大きく見積もる） */
    constexpr float BAKED_LOD_MIN_SPHERE_DISTANCE = 1.0e-4f;

    /**
     * @brief 焼き込み済みの階層（NVMESH v1）で、球の中の誤差を画面へ投影した大きさ（画素）の上限
     *
     * 誤差 e の形のずれは、球のどの点でも最も近い点までの距離 D−R より遠く、視線から外れた点では
     * ComputeLODSpherePerspectiveStretch の倍率で伸びるので、画面で projectionFactor·e·stretch/(D−R) を超えない。
     * 親の球が子の球を包み、親の誤差が子の誤差以上なら、親の値は子の値以上になる（段が粗いほど単調）。
     * cluster_cull.comp の ProjectBakedError と同じ式。
     *
     * @param centerDistance カメラから球の中心までの距離（ワールド）
     * @param centerDepth 球の中心の前方の軸に沿った深さ（クリップ座標の w）
     * @param radius 球の半径（ワールド）
     * @param error 球の中の誤差（ワールドの長さ）
     */
    inline float ComputeBakedSphereErrorPixels(float centerDistance,
                                               float centerDepth,
                                               float radius,
                                               float error,
                                               float projectionFactor,
                                               float tanHalfFovX,
                                               float tanHalfFovY)
    {
        const float nearest = std::max(centerDistance - radius, BAKED_LOD_MIN_SPHERE_DISTANCE);
        const float stretch =
            ComputeLODSpherePerspectiveStretch(centerDistance, centerDepth, radius, tanHalfFovX, tanHalfFovY);
        return error * projectionFactor * stretch / nearest;
    }

    /** @brief 焼き込み済みの階層の判定をCPUで写すときの、カメラと画面の値（cluster_cull.comp の cullData に相当） */
    struct BakedLODView
    {
        float CameraPosition[3] = {0.0f, 0.0f, 0.0f};
        /** @brief カメラの前方向（ワールド、単位ベクトル）。球の中心の深さを求める */
        float Forward[3] = {0.0f, 0.0f, 1.0f};
        float ProjectionFactor = 1.0f; // screenHeight / (2·tan(fovY/2))
        float TanHalfFovX = 1.0f;
        float TanHalfFovY = 1.0f;
        float LODBias = 1.0f; // 許容する画面での誤差（画素）
    };

    /**
     * @brief ワールド行列（行ベクトル規約。並進は要素12〜14）の、行ごとの軸の長さの最大（球の半径・誤差の伸び）
     */
    inline float ComputeWorldMaxAxisScale(const float *worldMatrix)
    {
        float maxScaleSquared = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const float *row = worldMatrix + axis * 4;
            maxScaleSquared = std::max(maxScaleSquared, row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        }
        return std::sqrt(maxScaleSquared);
    }

    /**
     * @brief ローカルの球と誤差を、ワールド行列とカメラから画面へ投影した大きさ（画素）にする
     *
     * cluster_cull.comp の ProjectBakedError と同じ計算。
     */
    inline float ProjectBakedSphereError(const BoundingSphere &sphere,
                                         float error,
                                         const float *worldMatrix,
                                         const BakedLODView &view)
    {
        const float scale = ComputeWorldMaxAxisScale(worldMatrix);
        float center[3] = {};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            center[axis] = sphere.CenterX * worldMatrix[0 + axis] + sphere.CenterY * worldMatrix[4 + axis] +
                           sphere.CenterZ * worldMatrix[8 + axis] + worldMatrix[12 + axis];
        }
        const float dx = center[0] - view.CameraPosition[0];
        const float dy = center[1] - view.CameraPosition[1];
        const float dz = center[2] - view.CameraPosition[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float depth = dx * view.Forward[0] + dy * view.Forward[1] + dz * view.Forward[2];
        return ComputeBakedSphereErrorPixels(distance,
                                             depth,
                                             sphere.Radius * scale,
                                             error * scale,
                                             view.ProjectionFactor,
                                             view.TanHalfFovX,
                                             view.TanHalfFovY);
    }

    /** @brief 焼き込み済みの階層で、自分の誤差の投影が許容以下か（これが偽なら、より詳細な段を使う） */
    inline bool IsBakedClusterWithinError(const MeshCluster &cluster, const float *worldMatrix, const BakedLODView &view)
    {
        return !(ProjectBakedSphereError(cluster.Bounds, cluster.LODError, worldMatrix, view) > view.LODBias);
    }

    /**
     * @brief 焼き込み済みの階層で、親のグループの誤差の投影が許容を超えるか（根は親が無いので常に真）
     *
     * 親の球・誤差は同じグループのクラスタで同じ値なので、結果も同じグループで同じになる。NaN なら真（自分を描く側）。
     */
    inline bool IsBakedParentTooCoarse(const MeshCluster &cluster, const float *worldMatrix, const BakedLODView &view)
    {
        if (cluster.GroupId == INVALID_CLUSTER_GROUP_ID)
        {
            return true;
        }
        return !(ProjectBakedSphereError(cluster.ParentBounds, cluster.ParentError, worldMatrix, view) <=
                 view.LODBias);
    }

    /**
     * @brief 焼き込み済みの階層で、このクラスタを描くか（cluster_cull.comp の ShouldDrawBakedCluster と同じ判定）
     *
     * 自分の誤差を自分の球から投影した値が閾値以下で、親のグループの誤差を親の球から投影した値が閾値を超えるとき描く。
     * 根は親が無いので、自分の値だけで決まる。
     *
     * @param worldMatrix 行ベクトル規約の4x4（並進は要素12〜14）。誤差・半径は行ごとの軸の長さの最大で伸ばす
     */
    inline bool ShouldDrawBakedCluster(const MeshCluster &cluster, const float *worldMatrix, const BakedLODView &view)
    {
        return IsBakedClusterWithinError(cluster, worldMatrix, view) &&
               IsBakedParentTooCoarse(cluster, worldMatrix, view);
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
