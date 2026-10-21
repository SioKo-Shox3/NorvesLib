// TAA の履歴の混ぜ方の定数を、撮り比べのために環境変数で差し替える（開発ビルドだけ）。
// Game の --taa-current-weight・--taa-clip-gamma・--taa-sharpen が環境変数へ渡し、TemporalAAPass が定数を GPU へ渡すときに掛ける。
// 指定が無ければ Rendering/TemporalAA.h の定数のまま。環境変数は最初の読み出しで 1 回だけ読む。
//   NORVES_TAA_CURRENT_WEIGHT … 現在の色を混ぜる割合（0.01〜1）
//   NORVES_TAA_CLIP_GAMMA     … 履歴をクリップする箱の半幅（標準偏差の倍数、0.25〜8）
//   NORVES_TAA_SHARPEN        … 書き戻すときのシャープ化の強さ（0〜1）
//   NORVES_TAA_JITTER_SCALE   … サブピクセルのジッタに掛ける倍率（0〜1。0 はジッタを掛けずに履歴を貯める。
//                               Game の --taa-jitter-scale。ジッタで描かれる中身が変わる経路の切り分けに使う）
//   NORVES_TAA_GEOMETRY_LOD_SCALE … TAA の View で MegaGeometry の LOD の誤差の閾値に掛ける倍率（0.125〜1。1 は TAA の無い
//                               View と同じ段。Game の --taa-geometry-lod-scale）
//   NORVES_TAA_HISTORY_SHARPNESS … 履歴を再投影して読む 3 次の補間の鋭さ（0〜1.5。0.5 が Catmull-Rom。
//                               Game の --taa-history-sharpness）
//   NORVES_TAA_RESAMPLE_CURRENT_WEIGHT … 補間で最もぼける画素の現在の色の割合（0.01〜1。Game の --taa-resample-current-weight）
#pragma once

#include "Logging/LogMacros.h"
#include "Rendering/TemporalAA.h"

#include <cmath>
#include <cstdlib>

namespace NorvesLib::Core::Rendering
{
    /** @brief TAA の解決と書き戻しに渡す、履歴の混ぜ方の値。 */
    struct TemporalAABlendParams
    {
        float CurrentFrameWeight = TemporalAACurrentFrameWeight;
        float VarianceClipGamma = TemporalAAVarianceClipGamma;
        float SharpenStrength = TemporalAASharpenStrength;
        float JitterScale = 1.0f;
        float GeometryLodScale = TemporalAAGeometryLodScale;
        float HistoryCubicSharpness = TemporalAAHistoryCubicSharpness;
        float ResampleCurrentFrameWeight = TemporalAAResampleCurrentFrameWeight;
    };

#if NORVES_BUILD_DEVELOPMENT
    namespace TemporalAAParamOverrideDetail
    {
        // name の環境変数が minimum〜maximum の有限の数なら value を替えて真
        inline bool TryReadBoundedEnv(const char *name, float minimum, float maximum, float &value)
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char *text = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (text == nullptr || text[0] == '\0')
            {
                return false;
            }
            char *end = nullptr;
            const float parsed = std::strtof(text, &end);
            if (end == text || *end != '\0' || !std::isfinite(parsed) || parsed < minimum || parsed > maximum)
            {
                LOG_WARNING_F("%s=%s は範囲（%.2f から %.2f）の外なので無視します", name, text, static_cast<double>(minimum),
                              static_cast<double>(maximum));
                return false;
            }
            value = parsed;
            return true;
        }

        inline TemporalAABlendParams ReadOverride()
        {
            TemporalAABlendParams params;
            bool bOverridden = false;
            bOverridden |= TryReadBoundedEnv("NORVES_TAA_CURRENT_WEIGHT", 0.01f, 1.0f, params.CurrentFrameWeight);
            bOverridden |= TryReadBoundedEnv("NORVES_TAA_CLIP_GAMMA", 0.25f, 8.0f, params.VarianceClipGamma);
            bOverridden |= TryReadBoundedEnv("NORVES_TAA_SHARPEN", 0.0f, 1.0f, params.SharpenStrength);
            bOverridden |= TryReadBoundedEnv("NORVES_TAA_JITTER_SCALE", 0.0f, 1.0f, params.JitterScale);
            bOverridden |= TryReadBoundedEnv("NORVES_TAA_GEOMETRY_LOD_SCALE", 0.125f, 1.0f, params.GeometryLodScale);
            bool bMotionOverridden = false;
            bMotionOverridden |= TryReadBoundedEnv("NORVES_TAA_HISTORY_SHARPNESS", 0.0f, 1.5f, params.HistoryCubicSharpness);
            bMotionOverridden |=
                TryReadBoundedEnv("NORVES_TAA_RESAMPLE_CURRENT_WEIGHT", 0.01f, 1.0f, params.ResampleCurrentFrameWeight);
            if (bMotionOverridden)
            {
                // 止まった画の定数の行（TAA_PARAMS）は撮影の照合が読むので、動きの定数は別の行に出す
                LOG_INFO_F("TAA_MOTION_PARAMS history_sharpness=%.3f resample_current_weight=%.3f source=env",
                           static_cast<double>(params.HistoryCubicSharpness),
                           static_cast<double>(params.ResampleCurrentFrameWeight));
            }
            if (bOverridden)
            {
                LOG_INFO_F("TAA_PARAMS current_weight=%.3f clip_gamma=%.3f sharpen=%.3f jitter_scale=%.3f geometry_lod_scale=%.3f "
                           "source=env",
                           static_cast<double>(params.CurrentFrameWeight), static_cast<double>(params.VarianceClipGamma),
                           static_cast<double>(params.SharpenStrength), static_cast<double>(params.JitterScale),
                           static_cast<double>(params.GeometryLodScale));
            }
            return params;
        }
    } // namespace TemporalAAParamOverrideDetail
#endif

    /** @brief 今の TAA の混ぜ方の値（開発ビルドでは環境変数の差し替えを掛けたもの）。 */
    inline const TemporalAABlendParams &GetTemporalAABlendParams()
    {
#if NORVES_BUILD_DEVELOPMENT
        static const TemporalAABlendParams s_Params = TemporalAAParamOverrideDetail::ReadOverride();
#else
        static const TemporalAABlendParams s_Params{};
#endif
        return s_Params;
    }
} // namespace NorvesLib::Core::Rendering
