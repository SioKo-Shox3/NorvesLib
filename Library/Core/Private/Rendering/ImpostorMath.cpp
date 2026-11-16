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

        Math::Vector3 NormalizeOrForward(const Math::Vector3& direction)
        {
            if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
            {
                return Math::Vector3(0.0f, 1.0f, 0.0f);
            }

            // 長さの計算で桁あふれや桁落ちが起きないよう、最大成分で縮尺をそろえる。
            const float maxXY = AbsFloat(direction.x) > AbsFloat(direction.y) ? AbsFloat(direction.x) : AbsFloat(direction.y);
            const float scale = maxXY > AbsFloat(direction.z) ? maxXY : AbsFloat(direction.z);
            if (scale <= 0.0f)
            {
                return Math::Vector3(0.0f, 1.0f, 0.0f);
            }

            return Math::Vector3(direction.x / scale, direction.y / scale, direction.z / scale).Normalized();
        }
    } // namespace

    Math::Vector2 EncodeOctahedralDirection(const Math::Vector3& direction, ImpostorOctahedralLayout layout)
    {
        Math::Vector3 normal = NormalizeOrForward(direction);
        if (layout == ImpostorOctahedralLayout::HemiOctahedral)
        {
            normal.y = normal.y < 0.0f ? 0.0f : normal.y;
            normal = NormalizeOrForward(normal);
        }

        normal /= AbsFloat(normal.x) + AbsFloat(normal.y) + AbsFloat(normal.z);
        Math::Vector2 encoded(normal.x, normal.z);
        if (layout == ImpostorOctahedralLayout::HemiOctahedral)
        {
            encoded = Math::Vector2(normal.x + normal.z, normal.x - normal.z);
        }
        else if (normal.y < 0.0f)
        {
            const float x = encoded.x;
            const float y = encoded.y;
            encoded.x = (1.0f - AbsFloat(y)) * SignNotZero(x);
            encoded.y = (1.0f - AbsFloat(x)) * SignNotZero(y);
        }

        return Math::Vector2(Clamp01(encoded.x * 0.5f + 0.5f),
                             Clamp01(encoded.y * 0.5f + 0.5f));
    }

    Math::Vector3 DecodeOctahedralDirection(const Math::Vector2& encodedUV, ImpostorOctahedralLayout layout)
    {
        const Math::Vector2 p(encodedUV.x * 2.0f - 1.0f, encodedUV.y * 2.0f - 1.0f);
        if (layout == ImpostorOctahedralLayout::HemiOctahedral)
        {
            const float x = (p.x + p.y) * 0.5f;
            const float z = (p.x - p.y) * 0.5f;
            const float y = 1.0f - AbsFloat(x) - AbsFloat(z);
            return NormalizeOrForward(Math::Vector3(x, y < 0.0f ? 0.0f : y, z));
        }

        Math::Vector3 normal(p.x, 1.0f - AbsFloat(p.x) - AbsFloat(p.y), p.y);
        if (normal.y < 0.0f)
        {
            normal.x = (1.0f - AbsFloat(p.y)) * SignNotZero(p.x);
            normal.z = (1.0f - AbsFloat(p.x)) * SignNotZero(p.y);
        }
        return NormalizeOrForward(normal);
    }

    ImpostorAtlasCellSelection SelectImpostorAtlasCells(const Math::Vector3& direction,
                                                      uint32_t axisCellCountX,
                                                      uint32_t axisCellCountY,
                                                      ImpostorOctahedralLayout layout)
    {
        ImpostorAtlasCellSelection selection;
        if (axisCellCountX == 0 || axisCellCountY == 0)
        {
            return selection;
        }

        selection.EncodedUV = EncodeOctahedralDirection(direction, layout);

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

    Math::Vector3 ComputeImpostorCellDirection(uint32_t cellX, uint32_t cellY,
                                             uint32_t axisCellCountX, uint32_t axisCellCountY,
                                             ImpostorOctahedralLayout layout)
    {
        const float u = axisCellCountX > 1u ? static_cast<float>(cellX) / static_cast<float>(axisCellCountX - 1u) : 0.5f;
        const float v = axisCellCountY > 1u ? static_cast<float>(cellY) / static_cast<float>(axisCellCountY - 1u) : 0.5f;
        return DecodeOctahedralDirection(Math::Vector2(u, v), layout);
    }

    ImpostorCellBasis ComputeImpostorCellBasis(const Math::Vector3& viewDirection)
    {
        ImpostorCellBasis basis;
        basis.ViewDirection = NormalizeOrForward(viewDirection);
        const Math::Vector3 upHint = AbsFloat(basis.ViewDirection.y) > 0.999f
                                        ? Math::Vector3(0.0f, 0.0f, -1.0f)
                                        : Math::Vector3(0.0f, 1.0f, 0.0f);
        basis.Right = Math::Vector3::Cross(upHint, basis.ViewDirection).Normalized();
        basis.Up = Math::Vector3::Cross(basis.ViewDirection, basis.Right);
        return basis;
    }

} // namespace NorvesLib::Core::Rendering
