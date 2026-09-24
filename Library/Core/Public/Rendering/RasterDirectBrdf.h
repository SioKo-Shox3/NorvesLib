#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ラスタの直接光（LightingPass）を評価するBRDF
     *
     * 既定のNeuralは学習済みのDisney BRDF近似（重みを読めないときは解析BRDFへ戻る）。Analyticは
     * パストレーサーと共通の解析BRDFで、ラスタとPTの相互比較で直接光の近似差を除くときに使う。
     */
    enum class RasterDirectBrdf : uint32_t
    {
        Neural = 0,
        Analytic = 1
    };
} // namespace NorvesLib::Core::Rendering
