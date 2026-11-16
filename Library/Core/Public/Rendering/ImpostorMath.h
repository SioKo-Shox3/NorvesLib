#pragma once

#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief Y が上のオブジェクト空間で、+Y を極とする八面体の配置。 */
    enum class ImpostorOctahedralLayout : uint8_t
    {
        HemiOctahedral = 0,
        FullOctahedral = 1
    };

    struct ImpostorAtlasCellSelection
    {
        Math::Vector2 EncodedUV;
        uint32_t CellX0 = 0;
        uint32_t CellY0 = 0;
        uint32_t CellX1 = 0;
        uint32_t CellY1 = 0;
        float BlendX = 0.0f;
        float BlendY = 0.0f;
        bool bValid = false;
    };

    /** @brief セルのカメラ基底。Right × Up = ViewDirection の右手系。 */
    struct ImpostorCellBasis
    {
        Math::Vector3 Right;
        Math::Vector3 Up;
        Math::Vector3 ViewDirection;
    };

    /**
     * @brief 物体の中心から見る人へ向かうオブジェクト空間の向きを、極 +Y で符号化する。
     * 上半球では負の Y を 0 に詰めて再正規化する。ゼロ・非有限の向きは +Y とみなす。
     */
    Math::Vector2 EncodeOctahedralDirection(const Math::Vector3& direction,
                                          ImpostorOctahedralLayout layout = ImpostorOctahedralLayout::HemiOctahedral);

    /** @brief 極 +Y の UV を単位方向へ戻す。上半球では復元した負の Y を 0 に詰める。 */
    Math::Vector3 DecodeOctahedralDirection(const Math::Vector2& encodedUV,
                                          ImpostorOctahedralLayout layout = ImpostorOctahedralLayout::HemiOctahedral);

    /** @brief UV × (セル数 - 1) の格子点から隣接セルと補間率を求める。 */
    ImpostorAtlasCellSelection SelectImpostorAtlasCells(const Math::Vector3& direction,
                                                      uint32_t axisCellCountX,
                                                      uint32_t axisCellCountY,
                                                      ImpostorOctahedralLayout layout = ImpostorOctahedralLayout::HemiOctahedral);

    /** @brief セル番号 / (セル数 - 1) の格子点を復号する。セル数が 1 以下の軸は UV = 0.5 とする。 */
    Math::Vector3 ComputeImpostorCellDirection(uint32_t cellX, uint32_t cellY,
                                             uint32_t axisCellCountX, uint32_t axisCellCountY,
                                             ImpostorOctahedralLayout layout = ImpostorOctahedralLayout::HemiOctahedral);

    /** @brief 向きを正規化し、Y を上の目安とするカメラ基底を作る。Y の極付近では -Z を目安とする。 */
    ImpostorCellBasis ComputeImpostorCellBasis(const Math::Vector3& viewDirection);

} // namespace NorvesLib::Core::Rendering
