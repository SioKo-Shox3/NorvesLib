// 距離に応じたファーの殻数と描画定数を定義する。
#pragma once

#include "Rendering/FurTypes.h"

namespace NorvesLib::Core::Rendering
{
    /** @brief GBuffer の殻描画プログラムを登録簿で識別する名前。 */
    inline constexpr const char* FurShellGBufferProgramName = "fur_shell";

    /** @brief 材質に距離段がないときに使う既定の段数。 */
    inline constexpr uint32_t FurDefaultShellLodLevelCount = 5u;

    /** @brief 材質に距離段がないときに使う仮の表。各殻数は材質の LayerCount で頭打ちにする。 */
    inline constexpr FurLodLevel FurDefaultShellLodLevels[FurDefaultShellLodLevelCount] =
    {
        {3.0f, 32u},
        {10.0f, 16u},
        {25.0f, 8u},
        {50.0f, 4u},
        {100.0f, 1u},
    };

    /**
     * @brief 距離段を線形補間し、連続した殻数を返す。
     * @param params IsValidFurMaterialData で検査済みの材質の値。
     * @param distanceMeters 距離（m）。負値は 0、非有限値は材質の殻数を使う。
     * @return 1〜FurMaxLayerCount に収めた連続の殻数。
     */
    float ComputeFurShellCountContinuous(const FurParams& params, float distanceMeters);

    /** @brief 距離から決めた殻数と、最後の殻を残す割合。 */
    struct FurShellSelection
    {
        uint32_t ShellCount = 1u; ///< 描く殻数（1〜64）。
        float LastShellFraction = 1.0f; ///< 最後の殻を残す割合（0 より大きく 1 以下）。
        float ContinuousCount = 1.0f; ///< 切り上げ前の連続した殻数。
    };

    /**
     * @brief 連続した殻数を切り上げ、最後の殻を残す割合とともに返す。
     * @param params IsValidFurMaterialData で検査済みの材質の値。
     * @param distanceMeters 距離（m）。
     * @return 描く殻数・最後の殻の割合・連続した殻数。
     */
    FurShellSelection SelectFurShells(const FurParams& params, float distanceMeters);

    /**
     * @brief 一番外の殻が体の面から離れる距離の保守的な上限を返す。
     * @param fur IsValidFurMaterialData で検査済みの材質。
     * @return 最大長と全体および使用中の領域の倍率から求めた距離（m）。
     */
    float ComputeFurMaxShellLengthMeters(const FurMaterialData& fur);

    /**
     * @brief per-object UBO の FurMaterialGpuParams の後ろに置く、描画ごとの std140 定数。
     * Force の xyz は世界空間の毛の力（この項目では常に 0、風と重力は後の項目が設定）、w は 0。
     * Shell の x は殻数、y は最後の殻の割合、z は連続の殻数、w は皮膚と毛の粗さ。
     */
    struct FurShellDrawGpuParams
    {
        float Force[4]; ///< 世界空間の毛の力と空き成分。
        float Shell[4]; ///< 殻数、最後の殻の割合、連続の殻数、皮膚と毛の粗さ。
    };

    static_assert(sizeof(FurShellDrawGpuParams) == 32u);
} // namespace NorvesLib::Core::Rendering
