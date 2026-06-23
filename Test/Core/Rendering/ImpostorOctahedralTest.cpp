#include "Rendering/ImpostorMath.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    void AssertNear(float actual, float expected, float tolerance = 0.0001f)
    {
        assert(std::fabs(actual - expected) <= tolerance);
    }

    void TestOctahedralEncodingNormalizesAndClamps()
    {
        const Math::Vector2 center = EncodeOctahedralDirection(Math::Vector3(0.0f, 0.0f, 5.0f));
        AssertNear(center.x, 0.5f);
        AssertNear(center.y, 0.5f);

        const Math::Vector2 positiveX = EncodeOctahedralDirection(Math::Vector3(10.0f, 0.0f, 0.0f));
        AssertNear(positiveX.x, 1.0f);
        AssertNear(positiveX.y, 0.5f);

        const Math::Vector2 negativeX = EncodeOctahedralDirection(Math::Vector3(-10.0f, 0.0f, 0.0f));
        AssertNear(negativeX.x, 0.0f);
        AssertNear(negativeX.y, 0.5f);

        const Math::Vector2 positiveY = EncodeOctahedralDirection(Math::Vector3(0.0f, 2.0f, 0.0f));
        AssertNear(positiveY.x, 0.5f);
        AssertNear(positiveY.y, 1.0f);

        const Math::Vector2 zero = EncodeOctahedralDirection(Math::Vector3::Zero);
        AssertNear(zero.x, 0.5f);
        AssertNear(zero.y, 0.5f);

        const Math::Vector2 back = EncodeOctahedralDirection(Math::Vector3(0.0f, 0.0f, -1.0f));
        assert(back.x >= 0.0f && back.x <= 1.0f);
        assert(back.y >= 0.0f && back.y <= 1.0f);

        std::cout << "TestOctahedralEncodingNormalizesAndClamps passed\n";
    }

    void TestAtlasCellSelectionBlends()
    {
        const ImpostorAtlasCellSelection center =
            SelectImpostorAtlasCells(Math::Vector3(0.0f, 0.0f, 1.0f), 4u, 4u);
        assert(center.bValid);
        assert(center.CellX0 == 1u);
        assert(center.CellY0 == 1u);
        assert(center.CellX1 == 2u);
        assert(center.CellY1 == 2u);
        AssertNear(center.BlendX, 0.5f);
        AssertNear(center.BlendY, 0.5f);

        const ImpostorAtlasCellSelection positiveX =
            SelectImpostorAtlasCells(Math::Vector3(2.0f, 0.0f, 0.0f), 4u, 4u);
        assert(positiveX.bValid);
        assert(positiveX.CellX0 == 3u);
        assert(positiveX.CellX1 == 3u);
        assert(positiveX.CellY0 == 1u);
        assert(positiveX.CellY1 == 2u);
        AssertNear(positiveX.BlendX, 0.0f);
        AssertNear(positiveX.BlendY, 0.5f);

        const ImpostorAtlasCellSelection singleCell =
            SelectImpostorAtlasCells(Math::Vector3(1.0f, 1.0f, 1.0f), 1u, 1u);
        assert(singleCell.bValid);
        assert(singleCell.CellX0 == 0u);
        assert(singleCell.CellY0 == 0u);
        assert(singleCell.CellX1 == 0u);
        assert(singleCell.CellY1 == 0u);
        AssertNear(singleCell.BlendX, 0.0f);
        AssertNear(singleCell.BlendY, 0.0f);

        const ImpostorAtlasCellSelection invalid =
            SelectImpostorAtlasCells(Math::Vector3(1.0f, 0.0f, 0.0f), 0u, 4u);
        assert(!invalid.bValid);

        std::cout << "TestAtlasCellSelectionBlends passed\n";
    }
}

int main()
{
    std::cout << "ImpostorOctahedralTest start\n";
    TestOctahedralEncodingNormalizesAndClamps();
    TestAtlasCellSelectionBlends();
    std::cout << "ImpostorOctahedralTest passed\n";
    return 0;
}
