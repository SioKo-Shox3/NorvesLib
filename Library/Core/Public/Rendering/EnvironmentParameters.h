#pragma once

#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 描画で使う環境の値。FramePacket へ値で写す。
     *
     * 風の値は持たない（風は WindParameters）。revision のハッシュには DiscontinuityId だけを入れ、
     * 雨量・濡れ・雲量・時刻は入れない。これらが変わるたびに RTGI の履歴が捨てられることを防ぐ。
     */
    struct EnvironmentParameters
    {
        /// 環境の値を描画が使うか。
        bool bEnabled = false;
        /// 雨の強さ。範囲 [0, 1]。
        float RainIntensity = 0.0f;
        /// 地表の濡れ。範囲 [0, 1]。
        float Wetness = 0.0f;
        /// 雲量。範囲 [0, 1]。
        float CloudCover = 0.0f;
        /// ゲーム内の経過時刻[s]。0 以上。
        double GameTimeSeconds = 0.0;
        /// 天気の乱数の種。
        uint32_t Seed = 0;
        /// 天気の不連続（瞬時の切り替え・読み込み）のたびに進め、時間の履歴を切る印。
        uint32_t DiscontinuityId = 0;
    };

    /// 環境の値を描画が使わない既定の設定を返す。
    EnvironmentParameters MakeDefaultEnvironmentParameters();

    /// 非有限値を既定値に戻し、雨量・濡れ・雲量を [0, 1]、時刻を 0 以上に整える。
    EnvironmentParameters SanitizeEnvironmentParameters(const EnvironmentParameters& in);

    static_assert(std::is_trivially_copyable_v<EnvironmentParameters>);
    static_assert(std::is_standard_layout_v<EnvironmentParameters>);
} // namespace NorvesLib::Core::Rendering
