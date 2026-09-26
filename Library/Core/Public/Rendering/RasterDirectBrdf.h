#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ラスタの直接光（LightingPass）を評価するBRDF
     *
     * 既定のAnalyticはパストレーサーと共通の解析BRDF。Neuralは学習済みのDisney BRDF近似（重みを
     * 読めないときは解析BRDFへ戻る）で、`--raster-direct-brdf=neural` を指定したときだけ使う。
     */
    enum class RasterDirectBrdf : uint32_t
    {
        Neural = 0,
        Analytic = 1
    };
} // namespace NorvesLib::Core::Rendering
