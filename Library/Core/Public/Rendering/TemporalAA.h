// TAA のジッタ列（Halton(2,3)）と、ジッタを投影へ掛けるずらし量・履歴の混ぜ方の定数。
#pragma once

#include "Rendering/SceneProxy.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief ジッタ列の長さ。Halton(2,3) の1〜8番目を繰り返す。 */
    inline constexpr uint32_t TemporalAAJitterSampleCount = 8u;

    /** @brief 現在のフレームの色を混ぜる割合（残りは再投影した履歴）。 */
    inline constexpr float TemporalAACurrentFrameWeight = 0.1f;

    /** @brief 履歴をクリップする箱の半幅（近傍の色の標準偏差の何倍か、YCoCg）。 */
    inline constexpr float TemporalAAVarianceClipGamma = 1.0f;

    /**
     * @brief 1つのフレームのジッタ
     *
     * Pixel は画素単位のずらし量（-0.5〜0.5）、Ndc は投影後の NDC でのずらし量（2 × 画素 / 寸法）。
     */
    struct TemporalAAJitter
    {
        float PixelX = 0.0f;
        float PixelY = 0.0f;
        float NdcX = 0.0f;
        float NdcY = 0.0f;
    };

    /**
     * @brief 基数 base の radical inverse（Halton 列の index 番目。index は1から数える）
     *
     * index を base 進で表した桁を小数点の右へ逆順に並べた値（0〜1）。base が2未満なら0。
     */
    inline float TemporalAAHalton(uint32_t index, uint32_t base)
    {
        if (base < 2u)
        {
            return 0.0f;
        }
        float fraction = 1.0f;
        float result = 0.0f;
        while (index > 0u)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    /**
     * @brief frameIndex 番目のフレームのジッタ
     *
     * Halton(2,3) の (frameIndex mod 8) + 1 番目から 0.5 を引いた画素のずらし量と、それを
     * width × height の画像の NDC へ直した量。寸法が0なら NDC のずらし量は0。
     */
    inline TemporalAAJitter ComputeTemporalAAJitter(uint64_t frameIndex, uint32_t width, uint32_t height)
    {
        const uint32_t sampleIndex = static_cast<uint32_t>(frameIndex % TemporalAAJitterSampleCount) + 1u;
        TemporalAAJitter jitter;
        jitter.PixelX = TemporalAAHalton(sampleIndex, 2u) - 0.5f;
        jitter.PixelY = TemporalAAHalton(sampleIndex, 3u) - 0.5f;
        jitter.NdcX = width > 0u ? 2.0f * jitter.PixelX / static_cast<float>(width) : 0.0f;
        jitter.NdcY = height > 0u ? 2.0f * jitter.PixelY / static_cast<float>(height) : 0.0f;
        return jitter;
    }

    /**
     * @brief 投影行列へ NDC のずらし量を掛ける
     *
     * 列ベクトル規約の投影 P の前に NDC の平行移動を掛け、クリップ座標の x・y へ w × ずらし量を足す。
     * 透視・正射影のどちらでも、除算後の NDC はどの深度でも同じ量だけずれ、z と w は変わらない。
     */
    inline Math::Matrix4x4 ApplyTemporalAAProjectionJitter(const Math::Matrix4x4& projection,
                                                           float ndcX,
                                                           float ndcY)
    {
        if (ndcX == 0.0f && ndcY == 0.0f)
        {
            return projection;
        }
        return Math::MatrixUtils::CreateTranslation(ndcX, ndcY, 0.0f) * projection;
    }

    /** @brief カメラへジッタを写す（投影を作るとき CameraViewConstants が掛ける）。 */
    inline void ApplyTemporalAAJitter(CameraProxy& camera, const TemporalAAJitter& jitter)
    {
        camera.ProjectionJitterNdcX = jitter.NdcX;
        camera.ProjectionJitterNdcY = jitter.NdcY;
    }
} // namespace NorvesLib::Core::Rendering
