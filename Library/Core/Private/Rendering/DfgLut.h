// ラスタのIBLとパストレーサーが共有するsplit-sum DFG LUTの生成。
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief DFG LUTの一辺（横=NdotV、縦=roughness） */
    inline constexpr uint32_t DfgLutSize = 256u;

    /** @brief LUT各画素を積分するHammersley試料数 */
    inline constexpr uint32_t DfgLutSampleCount = 4096u;

    /**
     * @brief float32をfloat16へ最近接偶数丸めで変換する。
     */
    uint16_t FloatToHalfRne(float value);

    /**
     * @brief GGX（Smith k=roughness^2/2）のsplit-sum係数A・BをR16G16_FLOATで並べたLUTを返す。
     *
     * 画素(x, y)はNdotV=(x+0.5)/Size、roughness=(y+0.5)/Sizeの積分値で、
     * 1画素は(A, B)の2要素（全体でSize*Size*2要素）。ラスタとパストレーサーは同じ値を使い、
     * 多重散乱補償 1+F0(1-Ess)/Ess（Ess=A+B）とエネルギー分配を一致させる。
     * 入力を持たない決定論的な表なので、初回呼び出しで1度だけ積分し、以後は同じ値を返す。
     */
    const uint16_t* GetDfgLutHalfData();
}
