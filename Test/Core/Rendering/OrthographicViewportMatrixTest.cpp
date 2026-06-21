#include "Rendering/Viewport.h"
#include "Math/MatrixUtils.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 0.0001f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    void AssertMatrixNear(const NorvesLib::Math::Matrix4x4 &lhs,
                          const NorvesLib::Math::Matrix4x4 &rhs)
    {
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                assert(IsNearlyEqual(lhs.m[row][column], rhs.m[row][column]));
            }
        }
    }
}

int main()
{
    std::cout << "OrthographicViewportMatrixTest start\n";

    Viewport viewport;
    ViewportSettings settings;
    settings.Width = 0.5f;
    settings.Height = 0.25f;
    assert(viewport.Initialize(settings));

    CameraProxy camera;
    camera.Projection = ProjectionType::Orthographic;
    camera.OrthoWidth = 80.0f;
    camera.OrthoHeight = 25.0f;
    camera.NearPlane = 0.5f;
    camera.FarPlane = 9.5f;
    camera.Viewport.Width = 400.0f;
    camera.Viewport.Height = 100.0f;
    viewport.SetCamera(camera);

    const NorvesLib::Math::Matrix4x4 expected =
        NorvesLib::Math::MatrixUtils::CreateOrthographic(80.0f, 25.0f, 0.5f, 9.5f);
    AssertMatrixNear(viewport.GetProjectionMatrix(), expected);
    assert(IsNearlyEqual(viewport.GetProjectionMatrix().m11, 2.0f / 25.0f));
    assert(!IsNearlyEqual(viewport.GetProjectionMatrix().m11, 2.0f / 40.0f));

    std::cout << "OrthographicViewportMatrixTest passed\n";
    return 0;
}
