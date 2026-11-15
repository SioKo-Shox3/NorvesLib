// 環境の値を GPU の共通 UBO に詰める。GLSL 側の写しは Assets/Shaders/Common/WetSurface.glsl の EnvironmentGpuParams。
#pragma once

#include "Rendering/EnvironmentParameters.h"
#include "Rendering/FrameUploadArena.h"
#include "Rendering/WetSurface.h"

#include <cstddef>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /** @brief std140 の環境定数。GLSL 側の weather・surface・reserved に対応する。 */
    struct alignas(16) GPUEnvironmentParams
    {
        // weather
        float Wetness;          // 地表の濡れ [0, 1]。環境が無効なら 0
        float RainIntensity;    // 雨の強さ [0, 1]
        float CloudCover;       // 雲量 [0, 1]
        float GameTimeWrapped;  // 4096 秒で折り返したゲーム内時刻
        // surface
        float CavityUniform;    // 高さ・AO が無い面の一様の凹み
        float ExposureDefault;  // 雨の遮蔽の既定値
        float Seed01;           // Seed を 2^32 で割った値
        float Discontinuity;    // DiscontinuityId の下位 24 ビット
        // 予約
        float Reserved[4];
    };

    static_assert(sizeof(GPUEnvironmentParams) == 48);
    static_assert(alignof(GPUEnvironmentParams) == 16);
    static_assert(offsetof(GPUEnvironmentParams, Wetness) == 0);
    static_assert(offsetof(GPUEnvironmentParams, GameTimeWrapped) == 12);
    static_assert(offsetof(GPUEnvironmentParams, CavityUniform) == 16);
    static_assert(offsetof(GPUEnvironmentParams, ExposureDefault) == 20);
    static_assert(offsetof(GPUEnvironmentParams, Discontinuity) == 28);
    static_assert(offsetof(GPUEnvironmentParams, Reserved) == 32);
    static_assert(std::is_trivially_copyable_v<GPUEnvironmentParams>);
    static_assert(std::is_standard_layout_v<GPUEnvironmentParams>);

    inline constexpr double EnvironmentGameTimeWrapSeconds = 4096.0;

    /** @brief 環境が無効なときの定数を返す。 */
    constexpr GPUEnvironmentParams MakeDefaultEnvironmentGpuParams()
    {
        GPUEnvironmentParams result{};
        result.CavityUniform = WET_SURFACE_DEFAULT_CAVITY;
        result.ExposureDefault = 1.0f;
        return result;
    }

    /** @brief 環境の値を整えて GPU の定数へ詰める。 */
    GPUEnvironmentParams PackEnvironmentGpuParams(const EnvironmentParameters& environment);

    struct ViewRenderContext;

    /**
     * @brief このフレームの環境の UBO を引く。無ければフレームの持ち主から借りる。
     *
     * パスは環境の UBO をこれで引き、キャッシュする記述子の鍵には入れない。
     */
    FrameUploadAllocation AcquireEnvironmentConstants(const ViewRenderContext& context);
} // namespace NorvesLib::Core::Rendering
