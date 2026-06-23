#include "Rendering/ImpostorMath.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float AbsFloat(float value)
        {
            return value < 0.0f ? -value : value;
        }

        float Clamp01(float value)
        {
            if (value < 0.0f)
            {
                return 0.0f;
            }

            if (value > 1.0f)
            {
                return 1.0f;
            }

            return value;
        }

        float SignNotZero(float value)
        {
            return value < 0.0f ? -1.0f : 1.0f;
        }

        Math::Vector3 NormalizeOrForward(const Math::Vector3 &direction)
        {
            const float lengthSq = direction.x * direction.x +
                                   direction.y * direction.y +
                                   direction.z * direction.z;
            if (!std::isfinite(lengthSq) || lengthSq <= 0.0f)
            {
                return Math::Vector3(0.0f, 0.0f, 1.0f);
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return Math::Vector3(direction.x * invLength,
                                 direction.y * invLength,
                                 direction.z * invLength);
        }
    } // namespace

    Math::Vector2 EncodeOctahedralDirection(const Math::Vector3 &direction)
    {
        Math::Vector3 normal = NormalizeOrForward(direction);
        const float denominator = AbsFloat(normal.x) + AbsFloat(normal.y) + AbsFloat(normal.z);
        if (denominator > 0.0f)
        {
            normal.x /= denominator;
            normal.y /= denominator;
            normal.z /= denominator;
        }

        Math::Vector2 encoded(normal.x, normal.y);
        if (normal.z < 0.0f)
        {
            const float x = encoded.x;
            const float y = encoded.y;
            encoded.x = (1.0f - AbsFloat(y)) * SignNotZero(x);
            encoded.y = (1.0f - AbsFloat(x)) * SignNotZero(y);
        }

        return Math::Vector2(Clamp01(encoded.x * 0.5f + 0.5f),
                             Clamp01(encoded.y * 0.5f + 0.5f));
    }

    ImpostorAtlasCellSelection SelectImpostorAtlasCells(const Math::Vector3 &direction,
                                                        uint32_t axisCellCountX,
                                                        uint32_t axisCellCountY)
    {
        ImpostorAtlasCellSelection selection;
        if (axisCellCountX == 0 || axisCellCountY == 0)
        {
            return selection;
        }

        selection.EncodedUV = EncodeOctahedralDirection(direction);

        const float maxCellX = static_cast<float>(axisCellCountX - 1u);
        const float maxCellY = static_cast<float>(axisCellCountY - 1u);
        const float cellSpaceX = selection.EncodedUV.x * maxCellX;
        const float cellSpaceY = selection.EncodedUV.y * maxCellY;
        const float floorX = std::floor(cellSpaceX);
        const float floorY = std::floor(cellSpaceY);

        selection.CellX0 = static_cast<uint32_t>(floorX);
        selection.CellY0 = static_cast<uint32_t>(floorY);
        selection.CellX1 = selection.CellX0 + 1u < axisCellCountX ? selection.CellX0 + 1u : selection.CellX0;
        selection.CellY1 = selection.CellY0 + 1u < axisCellCountY ? selection.CellY0 + 1u : selection.CellY0;
        selection.BlendX = axisCellCountX > 1u ? Clamp01(cellSpaceX - floorX) : 0.0f;
        selection.BlendY = axisCellCountY > 1u ? Clamp01(cellSpaceY - floorY) : 0.0f;
        selection.bValid = true;
        return selection;
    }

} // namespace NorvesLib::Core::Rendering
